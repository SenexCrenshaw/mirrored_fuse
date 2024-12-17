CXX = g++
CXXFLAGS = -Wall -O2 -std=c++17 `pkg-config --cflags fuse3 libcurl`
LDFLAGS = `pkg-config --libs fuse3 libcurl`

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

all: mirrored_fuse

mirrored_fuse: mirrored_fuse.cpp
	$(CXX) $(CXXFLAGS) -o mirrored_fuse mirrored_fuse.cpp $(LDFLAGS)

install: mirrored_fuse
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 mirrored_fuse $(DESTDIR)$(BINDIR)/mirrored_fuse

clean:
	rm -f mirrored_fuse
