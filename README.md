# **Mirrored FUSE Filesystem**

## **Overview**
The **Mirrored FUSE Filesystem** is a custom FUSE-based virtual filesystem that mirrors `.strm` files as `.ts` files. It dynamically streams the content referenced by `.strm` files (typically URLs) and exposes them as `.ts` files for seamless playback. This allows applications to access and stream video or audio content directly as if it were a local file.

Key features:
- **Dynamic URL Streaming**: `.ts` files are virtual representations of `.strm` files that fetch data in real-time using CURL.
- **Read-Only Filesystem**: Designed for read-only access to prevent accidental writes.
- **Directory Mirroring**: The source directory containing `.strm` files is mirrored dynamically to expose `.ts` files.
- **Inotify Support**: Watches the source directory for changes to `.strm` files and updates the mirror accordingly.

---

## **How It Works**

1. Place `.strm` files in a source directory. Each `.strm` file contains a single URL pointing to a video/audio stream.
2. Mount the FUSE filesystem to a desired directory.
3. Access corresponding `.ts` files in the mount directory. For example:
   - `source_directory/video.strm` ➡️ `mount_point/video.ts`
4. Stream `.ts` files using any media player or tool (e.g., `ffplay`, VLC).

---

## **Dependencies**

Ensure the following dependencies are installed:
- **FUSE3**: For the FUSE filesystem interface.
- **libcurl**: For streaming data from URLs.
- **GCC/G++**: Compiler for C++17 code.

On Ubuntu/Debian-based systems, you can install the dependencies:

```bash
sudo apt update
sudo apt install -y fuse3 libfuse3-dev libcurl4-openssl-dev g++ pkg-config
```

---

## **Building the Project**

Clone the repository:

```bash
git clone https://github.com/yourusername/mirrored-fuse.git
cd mirrored-fuse
```

Build the project using `g++`:

```bash
g++ -Wall -O2 -std=c++17 `pkg-config --cflags fuse3 libcurl` -o mirrored_fuse mirrored_fuse.cpp `pkg-config --libs fuse3 libcurl`
```

This will produce an executable named `mirrored_fuse`.

---

## **Usage**

### **Basic Usage**

Run the following command to mount the filesystem:

```bash
./mirrored_fuse <source_directory> <mount_point>
```

- **`<source_directory>`**: Directory containing `.strm` files.
- **`<mount_point>`**: Target directory where the virtual `.ts` files will appear.

#### Example

```bash
mkdir ~/strm_source ~/fuse_mount
./mirrored_fuse ~/strm_source ~/fuse_mount
```

- Place `.strm` files in `~/strm_source`.
- Access `.ts` files in `~/fuse_mount`.

---

### **Unmounting the Filesystem**

To unmount the filesystem:

```bash
fusermount3 -u <mount_point>
```

Example:

```bash
fusermount3 -u ~/fuse_mount
```

---

### **Streaming Content**

Once mounted, use any media player to stream the `.ts` files. For example:

```bash
ffplay ~/fuse_mount/video.ts
```

---

## **Directory and File Behavior**

- **`.strm` files**: Must contain a valid URL.
- **`.ts` files**: Virtual files generated from `.strm` files. These files do not exist on disk but stream content in real-time.
- Non-`.strm` files are mirrored as-is.

---

## **Advanced Options**

You can pass additional FUSE options when mounting. For example, to enable debug output:

```bash
./mirrored_fuse ~/strm_source ~/fuse_mount -o debug
```

---

## **Known Issues**
1. Streaming large `.ts` files may consume bandwidth as they are streamed in real-time.
2. Currently, seeking within `.ts` files is not supported. Playback is sequential.

---

## **Contributing**

Contributions are welcome! To contribute:
1. Fork this repository.
2. Create a new branch: `git checkout -b my-feature`.
3. Commit your changes: `git commit -m "Add new feature"`.
4. Push to the branch: `git push origin my-feature`.
5. Submit a pull request.

---

## **License**

This project is licensed under the **MIT License**. See the `LICENSE` file for details.

---

## **Contact**

For questions or suggestions, feel free to open an issue or reach out via:

- GitHub Issues: [https://github.com/yourusername/mirrored-fuse/issues](https://github.com/yourusername/mirrored-fuse/issues)
- Email: yourname@example.com

---

## **Acknowledgments**

- **libcurl**: For providing a robust way to fetch data from URLs.
- **FUSE3**: For the ability to create custom filesystems.

---

Enjoy streaming with Mirrored FUSE Filesystem! 🎥
