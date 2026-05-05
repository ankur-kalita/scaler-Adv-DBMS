// Lab 1 — POSIX file I/O demo. Each syscall is a doorway from user space into the kernel.
// Diagram boxes referenced: (1) syscall  (2) fd  (4) inode  (5) page cache  (6) driver  (7) DMA  (8) eviction

#include <fcntl.h>      // open() and the O_* flags
#include <sys/fcntl.h>  // (redundant on most systems — fcntl.h covers it)
#include <unistd.h>     // read(), write(), close(), fsync()
#include <cstring>      // std::strlen()
#include <cstdio>       // perror() — prints the OS error message for the last failed syscall
#include <iostream>     // std::cout

int main() {
    const char* path = "demo.txt";  // relative path -> resolved against current working dir

    // ---------- WRITE PHASE ----------

    // open() = first syscall. Kernel resolves filename -> inode (4), creates an entry in the
    // open-file table, and returns an fd (2): a small int that indexes into THIS process's fd table.
    //   O_WRONLY: write only       O_CREAT: create if missing       O_TRUNC: wipe to length 0
    //   0644: permission bits (rw-r--r--), used only when O_CREAT actually creates the file
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open for write"); return 1; }  // <0 means syscall failed

    const char* msg = "hello from kernel side\n";

    // write() copies bytes from user buffer -> kernel page cache (5). Disk is NOT touched yet.
    // The page is marked "dirty" and will be flushed lazily. ssize_t = signed size_t (can hold -1).
    ssize_t n = write(fd, msg, std::strlen(msg));
    if (n < 0) { perror("write"); return 1; }
    std::cout << "wrote" << n << " bytes, fd=" << fd << "\n";

    // fsync() = force the dirty pages for this fd onto the physical disk NOW.
    // Kernel hands the page to the device driver (6), which sets up a DMA (7) transfer.
    // This is THE durability primitive — databases fsync the WAL on every commit.
    fsync(fd);

    // close() releases this process's fd slot. Does NOT fsync — that's why we called fsync first.
    close(fd);

    // ---------- READ PHASE ----------

    // Same path -> same inode (4). O_RDONLY: read only. No mode arg since we're not creating.
    // fd will likely be 3 again (the previous slot was freed by close()).
    fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open for read"); return 1; }

    // Stack buffer, zero-initialized. Reading sizeof(buf)-1 leaves room for the trailing '\0'
    // so std::cout can treat buf as a C-string without running off the end.
    char buf[128] = {0};

    // read() walks the diagram: fd -> inode -> "which disk block?" -> page cache (5) lookup.
    //   HIT  -> copy from cache to buf (RAM-speed, microseconds).
    //   MISS -> driver (6) issues DMA (7) into a free RAM page; if no free page, evict (8) LRU first.
    // Returns bytes actually read (0 = EOF).
    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) { perror("read"); return 1; }
    std::cout << "read" << n << " bytes, fd=" << fd << ": " << buf;

    close(fd);
    return 0;
}
