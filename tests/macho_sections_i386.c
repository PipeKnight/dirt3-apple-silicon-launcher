/* Freestanding i386 Mach-O fixture for the two section lookup conventions. */
typedef unsigned size_t;
const void *_dyld_get_image_header(unsigned);
int _dyld_get_image_vmaddr_slide(unsigned);
const unsigned char *getsectdata(const char *, const char *, size_t *);
const unsigned char *getsectdatafromheader(const void *, const char *, const char *, size_t *);
const void *getsegbyname(const char *);

static const unsigned char marker[]
    __attribute__((used, section("__DATA,__probe_test"))) = { 21, 32, 67 };

int main(void)
{
    const void *header = _dyld_get_image_header(0);
    int slide = _dyld_get_image_vmaddr_slide(0);
    if (!header || !slide) return 1;
    size_t size = 0;
    const unsigned char *loaded = getsectdata("__DATA", "__probe_test", &size);
    if (loaded != marker || size != sizeof marker || loaded[1] != 32) return 2;
    size = 0;
    const unsigned char *unslid = getsectdatafromheader(header, "__DATA", "__probe_test", &size);
    if ((unsigned)unslid + (unsigned)slide != (unsigned)loaded || size != sizeof marker) return 3;
    if (getsectdatafromheader(header, "__DATA", "__absent", &size) || size) return 4;
    if (getsectdata("__DATA", "__absent", 0)) return 5;
    const unsigned *segment = getsegbyname("__DATA");
    const unsigned *mach_header = header;
    if (!segment || segment[0] != 1 || (unsigned)segment <= (unsigned)header ||
        (unsigned)segment >= (unsigned)header + 28 + mach_header[5]) return 6;
    if (getsegbyname("__absent")) return 7;
    const unsigned segment_address = segment[6];
    const unsigned segment_size = segment[7];
    if ((unsigned)unslid - segment_address >= segment_size) return 8;
    if ((unsigned)loaded - (segment_address + (unsigned)slide) >= segment_size) return 9;
    return 0;
}
