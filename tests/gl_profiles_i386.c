enum {
    profile_attribute = 99,
    legacy_profile = 0x1000,
    core_41_profile = 0x4100,
    gl_version = 0x1f02,
    gl_extensions = 0x1f03,
    gl_num_extensions = 0x821d,
};

extern int CGLChoosePixelFormat(const int *attributes, void **format, int *count);
extern int CGLCreateContext(void *format, void *share, void **context);
extern int CGLDestroyPixelFormat(void *format);
extern int CGLDestroyContext(void *context);
extern int CGLSetCurrentContext(void *context);
extern const unsigned char *glGetString(unsigned name);
extern const unsigned char *glGetStringi(unsigned name, unsigned index);
extern void glGetIntegerv(unsigned name, int *data);

static int create_context(int profile, void **context)
{
    const int attributes[] = {profile_attribute, profile, 0};
    void *format = 0;
    int count = 0;
    if (CGLChoosePixelFormat(attributes, &format, &count) || !count || !format)
        return 1;
    int status = CGLCreateContext(format, 0, context);
    CGLDestroyPixelFormat(format);
    return status;
}

int main(void)
{
    void *legacy = 0, *core = 0;
    if (create_context(legacy_profile, &legacy) || CGLSetCurrentContext(legacy))
        return 1;
    const unsigned char *legacy_version = glGetString(gl_version);
    if (!legacy_version || legacy_version[0] != '2')
        return 2;
    if (create_context(core_41_profile, &core) || CGLSetCurrentContext(core))
        return 3;
    const unsigned char *core_version = glGetString(gl_version);
    if (!core_version || core_version[0] != '4' || legacy_version[0] != '2')
        return 4;
    int count = 0;
    glGetIntegerv(gl_num_extensions, &count);
    if (count <= 0 || !glGetStringi(gl_extensions, 0) ||
        !glGetStringi(gl_extensions, (unsigned)count - 1) ||
        glGetStringi(gl_extensions, (unsigned)count))
        return 5;
    if (CGLSetCurrentContext(legacy) || glGetString(gl_version) != legacy_version)
        return 6;
    CGLSetCurrentContext(0);
    CGLDestroyContext(core);
    CGLDestroyContext(legacy);
    return 0;
}
