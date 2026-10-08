#include <time.h>
#include <stddef.h>
int main(void) { time_t epoch = 0; struct tm *p = gmtime(&epoch); if (!p) return 1; struct tm copy = *p; return copy.tm_year != 70 || copy.tm_mon != 0 || copy.tm_mday != 1 || copy.tm_wday != 4 || copy.tm_yday != 0 || copy.tm_sec != 0 || copy.tm_isdst != 0; }
