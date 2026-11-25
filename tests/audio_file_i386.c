/* Freestanding i386 fixture for AudioToolbox callbacks and AudioBufferList. */
typedef void *audio_file;
typedef int (*reader)(void *, long long, unsigned, void *, unsigned *);
typedef long long (*get_size)(void *);
int AudioFileOpenWithCallbacks(void *, reader, void *, get_size, void *, unsigned, audio_file *);
int AudioFileClose(audio_file);
int ExtAudioFileWrapAudioFileID(audio_file, unsigned char, audio_file *);
int ExtAudioFileGetProperty(audio_file, unsigned, unsigned *, void *);
int ExtAudioFileSetProperty(audio_file, unsigned, unsigned, const void *);
int ExtAudioFileRead(audio_file, unsigned *, void *);
int ExtAudioFileDispose(audio_file);

static const unsigned char wave[] = {
    'R','I','F','F', 44,0,0,0, 'W','A','V','E',
    'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x40,0x1f,0,0,
    0x80,0x3e,0,0, 2,0, 16,0,
    'd','a','t','a', 8,0,0,0, 1,0, 2,0, 3,0, 4,0,
};

static unsigned reads;

static int read_frame_data(void *context, long long offset, unsigned want, void *out, unsigned *got)
{
    if (context != &reads || offset < 0) return -1;
    *got = 0;
    for (unsigned char *bytes = out; offset + *got < sizeof wave && *got < want; (*got)++) {
        bytes[*got] = wave[offset + *got];
    }
    reads++;
    return 0;
}

static long long size_of_wave(void *context)
{
    return context == &reads ? sizeof wave : 0;
}

int main(void)
{
    audio_file raw = 0, decoded = 0;
    if (AudioFileOpenWithCallbacks(&reads, read_frame_data, 0, size_of_wave, 0, 0, &raw) || !raw || !reads)
        return 1;
    if (ExtAudioFileWrapAudioFileID(raw, 0, &decoded) || !decoded) return 2;

    struct { double rate; unsigned fields[8]; } format;
    unsigned bytes = sizeof format;
    if (ExtAudioFileGetProperty(decoded, 0x66666d74, &bytes, &format) ||
        bytes != 40 || format.rate != 8000.0) return 3;
    if (ExtAudioFileSetProperty(decoded, 0x63666d74, bytes, &format)) return 4;
    long long length = 0;
    bytes = sizeof length;
    // Callback-backed WAVs can report zero frames before the first read.
    if (ExtAudioFileGetProperty(decoded, 0x2366726d, &bytes, &length) ||
        bytes != 8 || (length != 0 && length != 4)) return 5;

    short samples[4] = {0};
    struct { unsigned count, channels, bytes; void *buffer; unsigned sentinel; } buffers = {
        1, 1, sizeof samples, samples, 0xDEADBEEF,
    };
    unsigned frames = 4;
    if (ExtAudioFileRead(decoded, &frames, &buffers) || frames != 4 ||
        samples[0] != 1 || samples[3] != 4 || buffers.sentinel != 0xDEADBEEF) return 6;
    if (ExtAudioFileDispose(decoded) || AudioFileClose(raw)) return 7;
    return 0;
}
