#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

static void crc_table(uint32_t *table) {
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t value = i;
        for (unsigned bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? UINT32_C(0xedb88320) : 0);
        table[i] = value;
    }
}
int main(int argc, char **argv) {
    if (argc > 2) { fputs("checksum: expected at most one input path\n", stderr); return 2; }
    FILE *input = argc == 2 ? fopen(argv[1], "rb") : stdin;
    if (!input) { fputs("checksum: cannot open input\n", stderr); return 2; }
    uint32_t table[256], crc = UINT32_C(0xffffffff), bitwise = UINT32_C(0xffffffff), a = 1, b = 0;
    uint64_t hash = UINT64_C(14695981039346656037); size_t bytes = 0;
    unsigned char buffer[4096]; size_t count; int status = 0; crc_table(table);
    while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0) {
        if (bytes > 16777216 - count) { status = 2; break; }
        bytes += count;
        for (size_t i = 0; i < count; ++i) {
            unsigned char byte = buffer[i];
            crc = table[(crc ^ byte) & 255] ^ (crc >> 8);
            bitwise ^= byte;
            for (unsigned bit = 0; bit < 8; ++bit) bitwise = (bitwise >> 1) ^ ((bitwise & 1) ? UINT32_C(0xedb88320) : 0);
            a = (a + byte) % 65521; b = (b + a) % 65521;
            hash = (hash ^ byte) * UINT64_C(1099511628211);
        }
    }
    if (ferror(input)) status = 2;
    if (input != stdin && fclose(input) != 0) status = 2;
    if (!status && crc != bitwise) { fputs("checksum: CRC implementation mismatch\n", stderr); return 1; }
    if (!status) printf("bytes=%zu crc32=%08" PRIx32 " adler32=%08" PRIx32 " fnv1a=%016" PRIx64 "\n", bytes, crc ^ UINT32_C(0xffffffff), (b << 16) | a, hash);
    else fputs("checksum: input failure or size limit\n", stderr);
    return status;
}
