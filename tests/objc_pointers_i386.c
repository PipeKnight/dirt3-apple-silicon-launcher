extern void *CFStringCreateWithCString(void *allocator, const char *string, unsigned encoding);
extern void CFRelease(void *value);
extern void *NSClassFromString(void *name);
extern void *sel_registerName(const char *name);
extern void *objc_msgSend(void *receiver, void *selector, ...);

static int callback(int value)
{
    return value + 7;
}

int main(void)
{
    void *name = CFStringCreateWithCString(0, "NSValue", 0x08000100);
    void *value_class = NSClassFromString(name);
    CFRelease(name);
    void *box = sel_registerName("valueWithPointer:");
    void *unbox = sel_registerName("pointerValue");
    void *value = objc_msgSend(value_class, box, (void *)callback);
    int (*result)(int) = (int (*)(int))objc_msgSend(value, unbox);
    if (result != callback || result(10) != 17)
        return 1;
    value = objc_msgSend(value_class, box, (void *)0);
    if (!value || objc_msgSend(value, unbox))
        return 2;
    return 0;
}
