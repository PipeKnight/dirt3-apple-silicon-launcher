typedef signed short sample_t;

extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);

enum {
    alc_format_channels = 0x1990, alc_format_type = 0x1991, alc_frequency = 0x1007,
    al_mono16 = 0x1101, al_stereo16 = 0x1103,
    al_mono = 0x1500, al_stereo = 0x1501, al_short = 0x1402,
    rate = 48000, large_frames = 65539
};

static sample_t input[large_frames * 2 + 2], output[large_frames * 2 + 2];

int main(int argc, char **argv)
{
    if (argc != 2)
        return 1;
    void *library = dlopen(argv[1], 2);
    if (!library)
        return 2;
    void *(*open_device)(const char *) = dlsym(library, "alcLoopbackOpenDeviceSOFT");
    void *(*create_context)(void *, const int *) = dlsym(library, "alcCreateContext");
    char (*current_context)(void *) = dlsym(library, "alcMakeContextCurrent");
    void (*destroy_context)(void *) = dlsym(library, "alcDestroyContext");
    char (*close_device)(void *) = dlsym(library, "alcCloseDevice");
    void (*create_buffer)(int, unsigned *) = dlsym(library, "alGenBuffers");
    void (*delete_buffer)(int, unsigned *) = dlsym(library, "alDeleteBuffers");
    void (*upload)(unsigned, int, const void *, int, int) = dlsym(library, "alBufferData");
    void (*download)(unsigned, int, int, int, int, void *) = dlsym(library, "alGetBufferSamplesSOFT");
    int (*error)(void) = dlsym(library, "alGetError");
    if (!open_device || !create_context || !current_context || !destroy_context || !close_device ||
        !create_buffer || !delete_buffer || !upload || !download || !error)
        return 3;

    const int attributes[] = {alc_format_channels, al_stereo, alc_format_type, al_short, alc_frequency, rate, 0};
    void *device = open_device(0);
    if (!device)
        return 4;
    void *context = create_context(device, attributes);
    if (!context || !current_context(context))
        return 5;

    unsigned buffer = 0;
    create_buffer(1, &buffer);
    if (!buffer || error())
        return 6;
    const int frame_counts[] = {0, 1, 2, 7, 8, 9, 17, 256, large_frames};
    for (int channels = 1; channels <= 2; ++channels) {
        for (unsigned index = 0; index < sizeof frame_counts / sizeof frame_counts[0]; ++index) {
            int frames = frame_counts[index], n = frames * channels;
            for (int i = 0; i < n; ++i)
                input[i + 1] = (sample_t)(i * 7919 + 0x8000);
            upload(buffer, channels == 1 ? al_mono16 : al_stereo16, input + 1, n * sizeof(*input), rate);
            if (error())
                return 7;
            if (!frames)
                continue;
            output[0] = 0x4131;
            output[n + 1] = 0x4132;
            download(buffer, 0, frames, channels == 1 ? al_mono : al_stereo, al_short, output + 1);
            if (error())
                return 8;
            for (int i = 0; i < n; ++i)
                if (input[i + 1] != output[i + 1])
                    return 9;
            if (output[0] != 0x4131 || output[n + 1] != 0x4132)
                return 10;
        }
    }
    delete_buffer(1, &buffer);
    if (error())
        return 11;
    current_context(0);
    destroy_context(context);
    return close_device(device) ? 0 : 12;
}
