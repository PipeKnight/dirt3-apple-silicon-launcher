extern int snprintf(char *, unsigned long, const char *, ...);
extern int strcmp(const char *, const char *);

int main(void)
{
    char result[24];
    const char *padded = "%018p";
    const char *left_aligned = "%-12p";
    void *guest_pointer = (void *)0x1234;

    if (snprintf(result, sizeof result, padded, guest_pointer) != 18 ||
        strcmp(result, "0x0000000000001234")) return 1;
    if (snprintf(result, sizeof result, "%p", guest_pointer) != 6 ||
        strcmp(result, "0x1234")) return 2;
    if (snprintf(result, sizeof result, left_aligned, guest_pointer) != 12 ||
        strcmp(result, "0x1234      ")) return 3;

    const char *wide = "%600p";
    return snprintf(result, sizeof result, wide, guest_pointer) == 600 &&
        !strcmp(result, "                       ") ? 0 : 4;
}
