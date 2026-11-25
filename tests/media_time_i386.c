struct media_time {
    long long value;
    int scale;
    unsigned flags;
    long long epoch;
};
struct media_range { struct media_time start, duration; };

extern const struct media_time kCMTimeZero;
extern struct media_time CMTimeMakeWithSeconds(double, int);
extern struct media_range CMTimeRangeMake(struct media_time, struct media_time);
extern double CMTimeGetSeconds(struct media_time);
extern int CMTimeCompare(struct media_time, struct media_time);

int main(void)
{
    struct media_time start = CMTimeMakeWithSeconds(1.5, 600);
    if (start.value != 900 || start.scale != 600 || CMTimeGetSeconds(start) != 1.5) return 1;
    if (CMTimeGetSeconds(kCMTimeZero) != 0 || CMTimeCompare(kCMTimeZero, start) >= 0) return 2;
    struct media_time duration = CMTimeMakeWithSeconds(3.0, 1000);
    struct media_range range = CMTimeRangeMake(start, duration);
    return CMTimeCompare(range.start, start) || CMTimeCompare(range.duration, duration) ? 3 : 0;
}
