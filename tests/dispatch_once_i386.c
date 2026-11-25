typedef void (^work_t)(void);
extern void dispatch_once(int *predicate, work_t work);
extern void *dispatch_group_create(void);
extern void *dispatch_get_global_queue(int priority, unsigned flags);
extern void dispatch_group_async(void *group, void *queue, work_t work);
extern int dispatch_group_wait(void *group, unsigned long long deadline);
extern void dispatch_release(void *group);
static struct { int predicate, sentinel; } sample = {0, 12345};
static int calls;
int main(void)
{
    void *group = dispatch_group_create();
    void *queue = dispatch_get_global_queue(0, 0);
    if (!group || !queue) return 1;
    for (int index = 0; index < 12; index++)
        dispatch_group_async(group, queue, ^{
            dispatch_once(&sample.predicate, ^{ calls++; });
        });
    if (dispatch_group_wait(group, ~0ULL)) return 2;
    dispatch_release(group);
    return sample.predicate == -1 && sample.sentinel == 12345 && calls == 1 ? 0 : 3;
}
