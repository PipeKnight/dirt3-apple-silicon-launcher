extern int mkstemp(char *);
extern int unlink(const char *);
extern int close(int);
extern int memcmp(const void *, const void *, unsigned long);

struct guest_aiocb {
    int fd;
    long long offset;
    volatile void *buffer;
    unsigned long size;
    int priority;
    int notify, signal;
    unsigned value;
    void *notify_function, *notify_attributes;
    int opcode;
};
_Static_assert(sizeof(struct guest_aiocb) == 48, "i386 aiocb size");

extern int aio_read(struct guest_aiocb *);
extern int aio_write(struct guest_aiocb *);
extern int aio_error(const struct guest_aiocb *);
extern long aio_return(struct guest_aiocb *);
extern int aio_suspend(const struct guest_aiocb *const *, int, const void *);

static int complete(struct guest_aiocb *request, unsigned long expected)
{
    const struct guest_aiocb *list[] = {0, request};
    return aio_suspend(list, 2, 0) == 0 && aio_error(request) == 0 &&
        aio_return(request) == (long)expected;
}

int main(void)
{
    char path[] = "/tmp/dirt3-aio-test.XXXXXX";
    int file = mkstemp(path);
    if (file < 0) return 1;
    if (unlink(path)) return 2;

    char input[] = "read and write";
    char output[sizeof input] = {0};
    struct guest_aiocb request = {
        .fd = file, .offset = (1LL << 32) + 9, .buffer = input, .size = sizeof input,
    };
    if (aio_write(&request) || !complete(&request, sizeof input)) return 3;
    request.buffer = output;
    if (aio_read(&request) || !complete(&request, sizeof output)) return 4;
    if (close(file)) return 5;
    return memcmp(input, output, sizeof input) ? 6 : 0;
}
