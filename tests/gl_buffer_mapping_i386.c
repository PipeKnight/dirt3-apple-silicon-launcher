enum {
    profile_attribute = 99, legacy_profile = 0x1000,
    array_buffer = 0x8892, static_draw = 0x88e4,
    read_only = 0x88b8, write_only = 0x88b9,
    invalid_enum = 0x0500
};

extern int CGLChoosePixelFormat(const int *, void **, int *);
extern int CGLCreateContext(void *, void *, void **);
extern int CGLDestroyPixelFormat(void *);
extern int CGLDestroyContext(void *);
extern int CGLSetCurrentContext(void *);
extern void glGenBuffers(int, unsigned *);
extern void glBindBuffer(unsigned, unsigned);
extern void glBufferData(unsigned, int, const void *, unsigned);
extern void *glMapBuffer(unsigned, unsigned);
extern unsigned char glUnmapBuffer(unsigned);
extern void glDeleteBuffers(int, const unsigned *);
extern unsigned glGetError(void);

int main(void)
{
    const int attributes[] = {profile_attribute, legacy_profile, 0};
    void *format = 0, *context = 0;
    int count = 0;
    if (CGLChoosePixelFormat(attributes, &format, &count) || count == 0 ||
        CGLCreateContext(format, 0, &context))
        return 1;
    CGLDestroyPixelFormat(format);
    if (CGLSetCurrentContext(context))
        return 2;

    unsigned int buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(array_buffer, buffer);
    glBufferData(array_buffer, 64, 0, static_draw);
    unsigned int *mapped = glMapBuffer(array_buffer, write_only);
    if (!mapped || glGetError())
        return 3;
    for (unsigned int i = 0; i < 16; i++)
        mapped[i] = 0x35a00000u + i;
    if (!glUnmapBuffer(array_buffer) || glGetError())
        return 4;

    const unsigned int *readback = glMapBuffer(array_buffer, read_only);
    if (!readback || glGetError())
        return 5;
    for (unsigned int i = 0; i < 16; i++) {
        if (readback[i] != 0x35a00000u + i)
            return 6;
    }
    if (!glUnmapBuffer(array_buffer) || glGetError())
        return 7;
    if (glMapBuffer(array_buffer, 0) || glGetError() != invalid_enum || glGetError())
        return 8;

    glDeleteBuffers(1, &buffer);
    CGLSetCurrentContext(0);
    CGLDestroyContext(context);
    return 0;
}
