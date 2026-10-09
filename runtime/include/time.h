#ifndef CINDER_TIME_H
#define CINDER_TIME_H

typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000000L
clock_t clock(void);
/* Linux x86-64 glibc/musl ABI, including the extensions needed for its extent. */
struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long __cinder_gmtoff;
    const char *__cinder_zone;
};
time_t time(time_t *result);
struct tm *gmtime(const time_t *value);

#endif
