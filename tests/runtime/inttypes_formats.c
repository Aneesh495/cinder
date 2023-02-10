#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char buffer[256];
    int8_t a = -128; int16_t b = -32768; int32_t c = -2147483647 - 1;
    uint8_t u = 255; uint16_t v = 65535; uint32_t w = UINT32_C(4294967295);
    if (snprintf(buffer, sizeof(buffer), "%" PRId8 " %" PRIi16 " %" PRId32 " %" PRIi32, a, b, c, c) != 35) return 1;
    if (strcmp(buffer, "-128 -32768 -2147483648 -2147483648") != 0) return 2;
    if (snprintf(buffer, sizeof(buffer), "%" PRIu8 " %" PRIu16 " %" PRIu32 " %" PRIo32 " %" PRIx32 " %" PRIX32, u, v, w, w, w, w) != 50) return 3;
    if (strcmp(buffer, "255 65535 4294967295 37777777777 ffffffff FFFFFFFF") != 0) return 4;
    if (snprintf(buffer, sizeof(buffer), "%" PRId16 " %" PRIi8 " %" PRIo8 " %" PRIx16 " %" PRIX16, b, a, u, v, v) != 25) return 5;
    if (strcmp(buffer, "-32768 -128 377 ffff FFFF") != 0) return 6;
    intmax_t maximum = 17; intptr_t pointer_word = 31;
    if (snprintf(buffer, sizeof(buffer), "%" PRIiMAX " %" PRIoMAX " %" PRIXMAX " %" PRIiPTR " %" PRIoPTR " %" PRIXPTR, maximum, (uintmax_t)maximum, (uintmax_t)maximum, pointer_word, (uintptr_t)pointer_word, (uintptr_t)pointer_word) != 17) return 7;
    if (strcmp(buffer, "17 21 11 31 37 1F") != 0) return 8;
    return 0;
}
