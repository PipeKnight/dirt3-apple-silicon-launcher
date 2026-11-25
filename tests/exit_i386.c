extern int atexit(void (*handler)(void));
extern _Noreturn void _exit(int status);
extern _Noreturn void _Exit(int status);

static void on_normal_exit(void)
{
    _exit(2);
}

int main(void)
{
    if (atexit(on_normal_exit))
        return 1;
    _Exit(0);
}
