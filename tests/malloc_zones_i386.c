/* Freestanding i386 Mach-O fixture. Declarations use the guest ABI. */
typedef unsigned size_t;
typedef struct zone zone;
struct zone {
    void *reserved[2];
    size_t (*size)(zone *, const void *);
    void *(*malloc)(zone *, size_t);
    void *(*calloc)(zone *, size_t, size_t);
    void *(*valloc)(zone *, size_t);
    void (*free)(zone *, void *);
    void *(*realloc)(zone *, void *, size_t);
    void (*destroy)(zone *);
    const char *name;
    void *batch_malloc, *batch_free, *introspect;
    unsigned version;
    void *(*memalign)(zone *, size_t, size_t);
    void (*free_definite_size)(zone *, void *, size_t);
};

void *malloc(size_t);
void *calloc(size_t, size_t);
void free(void *);
void *realloc(void *, size_t);
void *valloc(size_t);
size_t malloc_size(const void *);
zone *malloc_default_zone(void);
void malloc_zone_register(zone *);
void malloc_zone_unregister(zone *);
zone *malloc_zone_from_ptr(const void *);
void *malloc_zone_malloc(zone *, size_t);
void *malloc_zone_memalign(zone *, size_t, size_t);
void *sbrk(int);

static unsigned char storage[2][4096] __attribute__((aligned(4096)));
static size_t lengths[2];
static unsigned released;

static size_t custom_size(zone *z, const void *p)
{
    for (unsigned i = 0; i < 2; i++) {
        if (p == storage[i]) return lengths[i];
    }
    return 0;
}

static void *custom_malloc(zone *z, size_t n)
{
    // The guest callback can request separate backing storage via libc.
    if (n == 173) {
        void *backing = malloc(17);
        if (!backing || malloc_size(backing) < 17) return 0;
        free(backing);
    }
    if (n > sizeof storage[0]) return 0;
    for (unsigned i = 0; i < 2; i++) {
        if (!lengths[i]) {
            lengths[i] = n ? n : 1;
            return storage[i];
        }
    }
    return 0;
}

static void *custom_calloc(zone *z, size_t count, size_t n)
{
    unsigned char *p = custom_malloc(z, count * n);
    for (size_t i = 0; p && i < count * n; i++) p[i] = 0;
    return p;
}

static void custom_free(zone *z, void *p)
{
    for (unsigned i = 0; i < 2; i++) {
        if (p == storage[i] && lengths[i]) {
            lengths[i] = 0;
            released++;
            return;
        }
    }
}

static void *custom_realloc(zone *z, void *p, size_t n)
{
    size_t old = custom_size(z, p);
    unsigned char *moved = custom_malloc(z, n);
    for (size_t i = 0; moved && i < old && i < n; i++) {
        moved[i] = ((unsigned char *)p)[i];
    }
    if (moved) custom_free(z, p);
    return moved;
}

static void *custom_memalign(zone *z, size_t align, size_t n)
{
    return align <= sizeof storage[0] ? custom_malloc(z, n) : 0;
}

static zone custom = {
    .size = custom_size, .malloc = custom_malloc, .calloc = custom_calloc,
    .valloc = custom_malloc, .free = custom_free, .realloc = custom_realloc,
    .name = "Fixture", .version = 6, .memalign = custom_memalign,
};

int main(void)
{
    #define VERIFY(condition, error) do { if (!(condition)) return (error); } while (0)
    zone *original = malloc_default_zone();
    void *existing = malloc(23);
    VERIFY(original && existing && malloc_size(existing) >= 23, 1);

    malloc_zone_register(&custom);
    malloc_zone_register(&custom);
    VERIFY(malloc_default_zone() == original, 2);
    malloc_zone_unregister(original);
    VERIFY(malloc_default_zone() == &custom, 3);
    malloc_zone_register(original);

    unsigned char *p = malloc(63);
    VERIFY(p == storage[0] && malloc_size(p) == 63, 4);
    VERIFY(malloc_zone_from_ptr(p) == &custom, 5);
    VERIFY(malloc_zone_from_ptr(existing) == original, 6);
    p[22] = 71;
    VERIFY(!realloc(p, 8192) && malloc_size(p) == 63, 7);
    p = realloc(p, 110);
    VERIFY(p == storage[1] && p[22] == 71 && released == 1, 8);
    free(p);
    free(existing);
    VERIFY(released == 2 && !malloc_size(p), 9);

    p = calloc(5, 20);
    VERIFY(p == storage[0] && !p[22], 10);
    free(p);
    p = valloc(30);
    VERIFY(p && !((size_t)p & 4095), 11);
    free(p);
    p = malloc_zone_memalign(&custom, 64, 40);
    VERIFY(p && !((size_t)p & 63), 12);
    free(p);
    VERIFY(!malloc_zone_memalign(&custom, 7, 40), 13);

    p = malloc_zone_malloc(original, 128);
    VERIFY(p && malloc_zone_from_ptr(p) == original, 14);
    free(p);
    p = malloc(173);
    VERIFY(p == storage[0] && malloc_size(p) == 173, 17);
    free(p);

    malloc_zone_unregister(&custom);
    VERIFY(malloc_default_zone() == original, 15);
    p = malloc(21);
    VERIFY(p && p != storage[0] && p != storage[1], 16);
    free(p);

    unsigned char *base = sbrk(0);
    VERIFY(base != (void *)-1 && !((size_t)base & 4095), 18);
    VERIFY(sbrk(8193) == base && sbrk(0) == base + 8193, 19);
    base[0] = 19;
    base[8192] = 91;
    VERIFY(sbrk(-1) == base + 8193 && sbrk(0) == base + 8192, 20);
    VERIFY(sbrk(1) == base + 8192 && base[0] == 19 && base[8192] == 0, 21);
    VERIFY(sbrk(-8193) == base + 8193 && sbrk(0) == base, 22);
    VERIFY(sbrk(-1) == (void *)-1 && sbrk(0) == base, 23);
    return 0;
}
