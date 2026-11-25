typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

struct descriptor {
    uint32_t type;
    void *handle;
} __attribute__((packed, aligned(2)));

extern short AECreateDesc(uint32_t type, const void *data, long size, struct descriptor *result);
extern short AEGetDescData(const struct descriptor *desc, void *data, long size);
extern long AEGetDescDataSize(const struct descriptor *desc);
extern short AEDisposeDesc(struct descriptor *desc);
extern short AEInstallEventHandler(uint32_t event_class, uint32_t event_id,
                                   short (*handler)(const struct descriptor *, struct descriptor *, long),
                                   long context, uint8_t system);
extern short AERemoveEventHandler(uint32_t event_class, uint32_t event_id,
                                  short (*handler)(const struct descriptor *, struct descriptor *, long),
                                  uint8_t system);
extern short UCConvertCFAbsoluteTimeToLongDateTime(double time, long long *date);
extern short UCConvertLongDateTimeToCFAbsoluteTime(long long date, double *time);

struct gregorian_date {
    int year;
    signed char month, day, hour, minute;
    double second;
};

extern struct gregorian_date CFAbsoluteTimeGetGregorianDate(double time, void *timezone);
extern double CFGregorianDateGetAbsoluteTime(struct gregorian_date date, void *timezone);

static short handler(const struct descriptor *event, struct descriptor *reply, long context)
{
    (void)event;
    (void)reply;
    return (short)context;
}

int main(void)
{
    const uint32_t type = 0x54455854; /* TEXT */
    const char input[] = "Guest";
    char output[sizeof(input)] = {0};
    struct descriptor desc = {0};
    if (AECreateDesc(type, input, sizeof(input), &desc) || desc.type != type || !desc.handle)
        return 1;
    if (AEGetDescDataSize(&desc) != sizeof(input) || AEGetDescData(&desc, output, sizeof(output)))
        return 2;
    for (unsigned i = 0; i < sizeof(input); i++) {
        if (input[i] != output[i])
            return 3;
    }
    if (AEDisposeDesc(&desc) || desc.handle)
        return 4;
    const uint32_t event_class = 0x54657374; /* Test */
    const uint32_t event_id = 0x44697274;    /* Dirt */
    if (AEInstallEventHandler(event_class, event_id, handler, 12, 0))
        return 5;
    if (AERemoveEventHandler(event_class, event_id, handler, 0))
        return 6;

    long long date = 0;
    double time = 0;
    if (UCConvertCFAbsoluteTimeToLongDateTime(17342241.0, &date) ||
        UCConvertLongDateTimeToCFAbsoluteTime(date, &time) || time != 17342241.0)
        return 7;
    struct gregorian_date gregorian = CFAbsoluteTimeGetGregorianDate(1.5, 0);
    if (gregorian.year != 2001 || gregorian.month != 1 || gregorian.day != 1 ||
        gregorian.hour || gregorian.minute || gregorian.second != 1.5)
        return 8;
    if (CFGregorianDateGetAbsoluteTime(gregorian, 0) != 1.5)
        return 9;
    return 0;
}
