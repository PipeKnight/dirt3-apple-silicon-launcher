/* Freestanding i386 fixture for host callbacks and ABI-sized out parameters. */
typedef const void *CFTypeRef;
typedef struct { int location, length; } range;
typedef struct {
    int version;
    void *info;
    const void *(*retain)(const void *);
    void (*release)(const void *);
    CFTypeRef (*describe)(const void *);
} timer_context;
typedef struct {
    int version;
    void *info;
    const void *(*retain)(const void *);
    void (*release)(const void *);
    CFTypeRef (*describe)(const void *);
    void *(*allocate)(int, unsigned, void *);
    void *(*reallocate)(void *, int, unsigned, void *);
    void (*deallocate)(void *, void *);
    int (*preferred)(int, unsigned, void *);
} allocator_context;

CFTypeRef CFAllocatorCreate(CFTypeRef, const allocator_context *);
CFTypeRef CFDataCreate(CFTypeRef, const unsigned char *, int);
int CFDataGetLength(CFTypeRef);
const unsigned char *CFDataGetBytePtr(CFTypeRef);
CFTypeRef CFStringCreateWithCString(CFTypeRef, const char *, unsigned);
CFTypeRef CFStringCreateMutableCopy(CFTypeRef, int, CFTypeRef);
unsigned char CFStringFindWithOptions(CFTypeRef, CFTypeRef, range, unsigned, range *);
void CFStringAppendFormat(CFTypeRef, CFTypeRef, CFTypeRef, ...);
void CFRelease(CFTypeRef);
CFTypeRef CFRunLoopTimerCreate(CFTypeRef, double, double, unsigned, int,
    void (*)(CFTypeRef, void *), const timer_context *);
void CFRunLoopTimerGetContext(CFTypeRef, timer_context *);
void *malloc(unsigned);
void free(void *);

typedef void *guest_object;
typedef struct { unsigned event_class, kind; } event_type;
typedef int (*event_handler)(guest_object, guest_object, void *);
guest_object dispatch_semaphore_create(int);
int dispatch_semaphore_wait(guest_object, unsigned long long);
int dispatch_semaphore_signal(guest_object);
void dispatch_release(guest_object);
int CreateEvent(CFTypeRef, unsigned, unsigned, double, unsigned, guest_object *);
unsigned GetEventClass(guest_object);
unsigned GetEventKind(guest_object);
double GetEventTime(guest_object);
guest_object RetainEvent(guest_object);
void ReleaseEvent(guest_object);
int GetEventParameter(guest_object, unsigned, unsigned, unsigned *, unsigned, unsigned *, void *);
int SetEventParameter(guest_object, unsigned, unsigned, unsigned, const void *);
int HIObjectRegisterSubclass(CFTypeRef, CFTypeRef, unsigned, event_handler,
    unsigned, const event_type *, void *, guest_object *);
int HIObjectCreate(CFTypeRef, guest_object, guest_object *);

enum { hi_object = 0x68696f62, hi_instance = 0x68696f69, void_pointer = 0x766f6964 };
static int constructed, destructed;

static int hi_handler(guest_object call, guest_object event, void *context)
{
    unsigned kind = GetEventKind(event);
    if (kind == 1) {
        guest_object instance = 0;
        unsigned size = 0;
        if (context != &constructed ||
            GetEventParameter(event, hi_instance, hi_object, 0, sizeof instance, &size, &instance) ||
            size != sizeof instance || !instance) return -50;
        void *data = &destructed;
        int status = SetEventParameter(event, hi_instance, void_pointer, sizeof data, &data);
        if (!status) constructed++;
        return status;
    }
    if (kind == 3 && context == &destructed) {
        destructed++;
        return 0;
    }
    return -9874;
}

static struct {
    unsigned allocate_calls;
    unsigned deallocate_calls;
    unsigned retain_calls;
    unsigned release_calls;
} callbacks;

static const void *retain_info(const void *info)
{
    if (info == &callbacks) callbacks.retain_calls++;
    return info;
}

static void release_info(const void *info)
{
    if (info == &callbacks) callbacks.release_calls++;
}

static void *allocate(int size, unsigned hint, void *info)
{
    if (info != &callbacks) return 0;
    callbacks.allocate_calls++;
    return malloc((unsigned)size);
}

static void deallocate(void *pointer, void *info)
{
    if (info == &callbacks) callbacks.deallocate_calls++;
    free(pointer);
}

static void timer_callback(CFTypeRef timer, void *info)
{
    (void)timer;
    (void)info;
}

int main(void)
{
    allocator_context context = {
        .info = &callbacks, .retain = retain_info, .release = release_info,
        .allocate = allocate, .deallocate = deallocate,
    };
    static const unsigned char bytes[512] = { 29, 17, 41 };
    CFTypeRef allocator = CFAllocatorCreate(0, &context);
    if (!allocator || callbacks.retain_calls != 1) return 1;
    CFTypeRef data = CFDataCreate(allocator, bytes, sizeof bytes);
    if (!data || !callbacks.allocate_calls || CFDataGetLength(data) != sizeof bytes ||
        CFDataGetBytePtr(data)[2] != 41) return 2;
    CFRelease(data);
    CFRelease(allocator);
    if (!callbacks.deallocate_calls || callbacks.release_calls != 1) return 3;

    const unsigned utf8 = 0x08000100;
    CFTypeRef text = CFStringCreateWithCString(0, "rally rally", utf8);
    CFTypeRef needle = CFStringCreateWithCString(0, "al", utf8);
    struct { range match; unsigned sentinel; } output = { { -1, -1 }, 0xDEADBEEF };
    if (!CFStringFindWithOptions(text, needle, (range){ 6, 5 }, 0, &output.match) ||
        output.match.location != 7 || output.match.length != 2 || output.sentinel != 0xDEADBEEF) return 4;
    if (CFStringFindWithOptions(text, needle, (range){ 0, 1 }, 0, &output.match) ||
        output.match.location != 7) return 5;
    CFTypeRef format = CFStringCreateWithCString(0, " %d", utf8);
    CFTypeRef suffix = CFStringCreateWithCString(0, " 27", utf8);
    CFTypeRef mutable = CFStringCreateMutableCopy(0, 0, text);
    CFStringAppendFormat(mutable, 0, format, 27);
    if (!CFStringFindWithOptions(mutable, suffix, (range){ 11, 3 }, 0, &output.match)) return 6;
    CFRelease(mutable);
    CFRelease(suffix);
    CFRelease(format);
    CFRelease(needle);
    CFRelease(text);

    guest_object semaphore = dispatch_semaphore_create(0);
    if (!semaphore || !dispatch_semaphore_wait(semaphore, 0)) return 7;
    dispatch_semaphore_signal(semaphore);
    if (dispatch_semaphore_wait(semaphore, 0)) return 8;
    dispatch_release(semaphore);

    guest_object event = 0;
    if (CreateEvent(0, 0x74657374, 31, 12.25, 0, &event) || !event ||
        GetEventClass(event) != 0x74657374 || GetEventKind(event) != 31 ||
        GetEventTime(event) != 12.25 || RetainEvent(event) != event) return 9;
    ReleaseEvent(event);
    ReleaseEvent(event);

    CFTypeRef id = CFStringCreateWithCString(0, "local.dirt3.probe.HIObject", utf8);
    event_type types[] = { { hi_object, 1 }, { hi_object, 3 } };
    guest_object class_ref = 0;
    if (HIObjectRegisterSubclass(id, 0, 0, hi_handler, 2, types, &constructed, &class_ref) ||
        !class_ref) return 10;
    guest_object object = 0;
    if (HIObjectCreate(id, 0, &object) || !object || constructed != 1) return 11;
    CFRelease(object);
    CFRelease(id);
    if (destructed != 1) return 12;

    timer_context timer_input = { .info = &callbacks, .retain = retain_info, .release = release_info };
    CFTypeRef timer = CFRunLoopTimerCreate(0, 1000000000, 1, 0, 0, timer_callback, &timer_input);
    if (!timer || callbacks.retain_calls != 2) return 13;
    struct { timer_context context; unsigned sentinel; } timer_output = { { .version = -1 }, 0xDEADBEEF };
    CFRunLoopTimerGetContext(timer, &timer_output.context);
    if (timer_output.context.version != 0 || timer_output.context.info != &callbacks ||
        timer_output.context.retain != retain_info || timer_output.context.release != release_info ||
        timer_output.context.describe || timer_output.sentinel != 0xDEADBEEF) return 14;
    CFRelease(timer);
    if (callbacks.release_calls != 2) return 15;
    return 0;
}
