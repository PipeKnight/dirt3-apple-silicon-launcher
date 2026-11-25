// Build for i386-apple-macos10.9 against the mac32 libSystem stub.
extern int getpagesize(void);
extern void *mmap(void *, unsigned int, int, int, int, long long);
extern int munmap(void *, unsigned int);
extern int madvise(void *, unsigned int, int);

enum {
    PROT_READ_WRITE = 3,
    MAP_PRIVATE_ANON = 0x1002,
    MADV_FREE = 5,
};

int main(void) {
    unsigned int page = getpagesize();
    volatile unsigned char *region = mmap(0, page * 3, PROT_READ_WRITE,
                                          MAP_PRIVATE_ANON, -1, 0);
    if (region == (void *)-1) return 1;

    region[0] = 0x71;
    region[page] = 0x14;
    region[page * 2] = 0x29;
    int result = madvise((void *)(region + page), page, MADV_FREE);
    int neighbors_intact = region[0] == 0x71 && region[page * 2] == 0x29;
    int unmap_result = munmap((void *)region, page * 3);

    if (result != 0) return 2;
    if (!neighbors_intact) return 3;
    return unmap_result != 0;
}
