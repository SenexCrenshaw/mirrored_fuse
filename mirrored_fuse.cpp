#define FUSE_USE_VERSION 35

#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>
#include <curl/curl.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

#include <sys/inotify.h>
#include <poll.h>
#include <thread>
#include <atomic>

// Utility functions
static bool ends_with(const std::string &value, const std::string &ending)
{
    if (ending.size() > value.size())
        return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

struct FileData
{
    std::string url;
    CURL *curl{nullptr};
    std::vector<char> buffer;
    std::mutex bufferMutex;
    std::atomic<size_t> bufferSize{0};
    bool loadError{false};
};

struct MirrorFS
{
    std::string sourceDir;
    std::mutex fileDataMutex;
    std::map<std::string, std::shared_ptr<FileData>> fileCache;

    int inotifyFd{-1};
    int watchFd{-1};
    std::thread inotifyThread;
    std::atomic<bool> runInotify{true};
};

static MirrorFS *g_state = nullptr;

// Get the corresponding .strm file path for a .ts file
static std::string ts_to_strm(const std::string &path)
{
    // Strip the mount point prefix "/mnt/fuse/"
    std::string relativePath = path;
    if (!relativePath.empty() && relativePath[0] == '/')
    {
        relativePath.erase(0, 1); // Remove leading '/'
    }

    // Replace the mount point root with the source directory root
    const std::string fuseRoot = "OnDemand/";
    if (relativePath.find(fuseRoot) == 0)
    {
        relativePath = relativePath.substr(fuseRoot.length());
    }

    // Append the relative path to the source directory root
    std::string fullPath = g_state->sourceDir + "/OnDemand/" + relativePath;

    // Replace .ts extension with .strm
    if (ends_with(fullPath, ".ts"))
    {
        fullPath = fullPath.substr(0, fullPath.size() - 2) + "strm";
    }
    return fullPath;
}

/// Get the corresponding .ts file name for a .strm file
static std::string strm_to_ts(const std::string &filename)
{
    return filename.substr(0, filename.size() - 4) + "ts";
}

/// Check if path is a directory
static bool is_dir(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        return true;
    return false;
}

/// Check if path is a file
static bool is_file(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
        return true;
    return false;
}

/// Load URL from a .strm file
static std::string load_url_from_strm(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
        return std::string();
    std::string url;
    std::getline(in, url);
    return url;
}

/// Fuse Callbacks

static int fs_getattr(const char *path, struct stat *stbuf, fuse_file_info *fi)
{
    (void)fi;
    memset(stbuf, 0, sizeof(struct stat));

    std::string relPath = path; // Convert to std::string for easier handling
    std::cout << "fs_getattr Path: " << path << std::endl;
    if (!relPath.empty() && relPath[0] == '/')
    {
        relPath.erase(0, 1); // Remove leading '/'
    }

    std::string sourcePath = g_state->sourceDir + "/" + relPath;
    std::cout << "fs_getattr sourcePath: " << sourcePath << std::endl;
    if (ends_with(relPath, ".ts"))
    {
        std::string strmPath = ts_to_strm(path);
        std::cout << "fs_getattr strmPath: " << strmPath << std::endl;
        if (!is_file(strmPath))
        {
            return -ENOENT; // File not found
        }
        stbuf->st_mode = S_IFREG | 0444; // Read-only regular file
        stbuf->st_nlink = 1;
        stbuf->st_size = 0; // Size will be handled in `fs_read`
        return 0;
    }
    else
    {
        if (stat(sourcePath.c_str(), stbuf) == -1)
        {
            return -errno; // Propagate system error
        }
    }

    if (ends_with(relPath, ".strm"))
    {
        return -ENOENT; // Hide .strm files
    }

    return 0;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, fuse_file_info *fi, fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    std::string relPath = path;
    if (!relPath.empty() && relPath[0] == '/')
    {
        relPath.erase(0, 1); // Remove leading '/'
    }

    std::string sourcePath = g_state->sourceDir + "/" + relPath;
    if (!is_dir(sourcePath))
    {
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0));

    DIR *dp = opendir(sourcePath.c_str());
    if (!dp)
    {
        return -errno; // Propagate error
    }

    struct dirent *de;
    while ((de = readdir(dp)) != NULL)
    {
        std::string name = de->d_name;
        if (name == "." || name == "..")
            continue;

        std::string fullPath = sourcePath + "/" + name;
        struct stat st;
        memset(&st, 0, sizeof(st));
        if (stat(fullPath.c_str(), &st) == -1)
        {
            continue; // Skip files with stat issues
        }

        if (ends_with(name, ".strm"))
        {
            std::string tsName = strm_to_ts(name);
            st.st_mode = S_IFREG | 0444;
            filler(buf, tsName.c_str(), &st, 0, static_cast<fuse_fill_dir_flags>(0));
        }
        else
        {
            filler(buf, name.c_str(), &st, 0, static_cast<fuse_fill_dir_flags>(0));
        }
    }

    closedir(dp);
    return 0;
}

static size_t stream_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto fileData = static_cast<FileData *>(userdata);
    size_t total = size * nmemb;

    {
        std::lock_guard<std::mutex> lock(fileData->bufferMutex);
        fileData->buffer.insert(fileData->buffer.end(), ptr, ptr + total);
        fileData->bufferSize += total;
    }

    return total;
}

static int fs_open(const char *path, fuse_file_info *fi)
{
    std::string relPath = path;
    if (!relPath.empty() && relPath[0] == '/')
        relPath.erase(0, 1);

    if (ends_with(relPath, ".ts"))
    {
        std::string strmPath = ts_to_strm(path);
        if (!is_file(strmPath))
            return -ENOENT;

        std::string url = load_url_from_strm(strmPath);
        if (url.empty())
            return -EIO;

        auto fileData = std::make_shared<FileData>();
        fileData->url = url;

        // Initialize CURL for streaming
        fileData->curl = curl_easy_init();
        curl_easy_setopt(fileData->curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(fileData->curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
        curl_easy_setopt(fileData->curl, CURLOPT_WRITEDATA, fileData.get());
        curl_easy_setopt(fileData->curl, CURLOPT_NOPROGRESS, 1L);

        g_state->fileCache[relPath] = fileData;
        fi->fh = reinterpret_cast<uint64_t>(fileData.get());

        return 0;
    }

    return -ENOENT;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset, fuse_file_info *fi)
{
    auto fileData = reinterpret_cast<FileData *>(fi->fh);
    size_t bytesRead = 0;

    if (offset < 0)
        return -EINVAL; // Guard against negative offsets

    while (bytesRead < size)
    {
        {
            std::lock_guard<std::mutex> lock(fileData->bufferMutex);

            // Safely compare bufferSize and offset
            if (fileData->bufferSize > static_cast<size_t>(offset))
            {
                size_t available = fileData->bufferSize - static_cast<size_t>(offset);
                size_t toRead = std::min(size - bytesRead, available);

                memcpy(buf + bytesRead, fileData->buffer.data() + offset, toRead);

                bytesRead += toRead;
                offset += toRead;
            }
        }

        if (bytesRead < size)
        {
            // Trigger CURL to fetch more data
            CURLcode res = curl_easy_perform(fileData->curl);
            if (res != CURLE_OK)
            {
                std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
                return -EIO; // Input/output error
            }
        }
    }

    return bytesRead;
}

static int fs_release(const char *path, fuse_file_info *fi)
{
    auto fileData = reinterpret_cast<FileData *>(fi->fh);
    if (fileData && fileData->curl)
    {
        curl_easy_cleanup(fileData->curl);
    }
    return 0;
}

/// Inotify thread (optional)
static void inotify_thread_func()
{
    char buffer[1024];
    struct pollfd pfd[1];
    pfd[0].fd = g_state->inotifyFd;
    pfd[0].events = POLLIN;
    while (g_state->runInotify.load())
    {
        int ret = poll(pfd, 1, 1000);
        if (ret > 0 && (pfd[0].revents & POLLIN))
        {
            int len = read(g_state->inotifyFd, buffer, sizeof(buffer));
            (void)len;
            // Events are read but we do nothing; changes show up in readdir.
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <source_dir> <mount_point> [FUSE options]\n";
        return 1;
    }

    std::string sourceDir = argv[1];
    if (!is_dir(sourceDir))
    {
        std::cerr << "Source directory does not exist.\n";
        return 1;
    }

    g_state = new MirrorFS();
    g_state->sourceDir = sourceDir;

    curl_global_init(CURL_GLOBAL_ALL);

    g_state->inotifyFd = inotify_init1(IN_NONBLOCK);
    if (g_state->inotifyFd >= 0)
    {
        g_state->watchFd = inotify_add_watch(g_state->inotifyFd, sourceDir.c_str(), IN_CREATE | IN_DELETE | IN_MODIFY);
        g_state->inotifyThread = std::thread(inotify_thread_func);
    }

    struct fuse_operations fs_ops = {};
    fs_ops.getattr = fs_getattr;
    fs_ops.readdir = fs_readdir;
    fs_ops.open = fs_open;
    fs_ops.read = fs_read;
    fs_ops.release = fs_release;

    std::vector<char *> fuseArgs;
    fuseArgs.push_back(argv[0]); // program name, e.g. "./mirrored_fuse"
    fuseArgs.push_back(argv[2]); // the mount point, "/mnt/fuse"
    for (int i = 3; i < argc; i++)
    { // any fuse options like -o debug -o foreground
        fuseArgs.push_back(argv[i]);
    }
    fuseArgs.push_back(nullptr);

    // int fuse_argc = (int)fuseArgs.size() - 1;

    // // Debug: Print arguments
    // for (int i = 0; i < fuse_argc; i++)
    // {
    //     std::cout << "Arg[" << i << "]: " << fuseArgs[i] << std::endl;
    // }

    int ret = fuse_main((int)fuseArgs.size() - 1, fuseArgs.data(), &fs_ops, nullptr);

    g_state->runInotify = false;
    if (g_state->inotifyThread.joinable())
    {
        g_state->inotifyThread.join();
    }
    if (g_state->watchFd >= 0)
    {
        inotify_rm_watch(g_state->inotifyFd, g_state->watchFd);
    }
    if (g_state->inotifyFd >= 0)
    {
        close(g_state->inotifyFd);
    }

    curl_global_cleanup();
    delete g_state;
    return ret;
}
