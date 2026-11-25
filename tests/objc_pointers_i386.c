extern void *CFStringCreateWithCString(void *allocator, const char *string, unsigned encoding);
extern void CFRelease(void *value);
extern void *NSClassFromString(void *name);
extern void *sel_registerName(const char *name);
extern void *objc_msgSend(void *receiver, void *selector, ...);

static int callback(int value)
{
    return value + 7;
}

static void *look_up_class(const char *class_name)
{
    void *name = CFStringCreateWithCString(0, class_name, 0x08000100);
    void *class = NSClassFromString(name);
    CFRelease(name);
    return class;
}

int main(void)
{
    void *value_class = look_up_class("NSValue");
    void *box = sel_registerName("valueWithPointer:");
    void *unbox = sel_registerName("pointerValue");
    void *value = objc_msgSend(value_class, box, (void *)callback);
    int (*result)(int) = (int (*)(int))objc_msgSend(value, unbox);
    if (result != callback || result(10) != 17)
        return 1;
    value = objc_msgSend(value_class, box, (void *)0);
    if (!value || objc_msgSend(value, unbox))
        return 2;

    void *values[2] = {CFStringCreateWithCString(0, "alpha", 0x08000100), value};
    void *array = objc_msgSend(look_up_class("NSArray"), sel_registerName("arrayWithObjects:count:"), values, 2U);
    if (objc_msgSend(array, sel_registerName("objectAtIndex:"), 1U) != value ||
        objc_msgSend(array, sel_registerName("objectAtIndex:"), 0U) != values[0])
        return 3;

    void *keys[2] = {values[0], CFStringCreateWithCString(0, "beta", 0x08000100)};
    void *dictionary = objc_msgSend(look_up_class("NSDictionary"),
        sel_registerName("dictionaryWithObjects:forKeys:count:"), values, keys, 2U);
    if (objc_msgSend(dictionary, sel_registerName("objectForKey:"), keys[1]) != value ||
        objc_msgSend(dictionary, sel_registerName("objectForKey:"), keys[0]) != values[0])
        return 4;
    CFRelease(keys[1]);
    CFRelease(values[0]);
    return 0;
}
