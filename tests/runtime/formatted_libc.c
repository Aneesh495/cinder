#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
int main(void) { char data[96]; int64_t n = -INT64_C(1234567890123); uint64_t u = UINT64_C(0xfedcba9876543210); int size = snprintf(data, sizeof(data), "%" PRId64 ":%" PRIx64 ":%.2f", n, u, 1.25); return size != 36 || strcmp(data, "-1234567890123:fedcba9876543210:1.25"); }
