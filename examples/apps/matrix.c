#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static int argument(const char *text, unsigned long limit, unsigned long *value) {
    if (!*text || *text == '-') return 0;
    char *end; *value = strtoul(text, &end, 10);
    return !*end && *value <= limit;
}
static uint32_t next_value(uint32_t *state) { *state = *state * UINT32_C(1664525) + UINT32_C(1013904223); return *state; }
static void multiply(const double *a, const double *b, double *c, size_t n) {
    for (size_t i = 0; i < n * n; ++i) c[i] = 0;
    for (size_t ii = 0; ii < n; ii += 8) for (size_t kk = 0; kk < n; kk += 8) for (size_t jj = 0; jj < n; jj += 8) {
        size_t imax = ii + 8 < n ? ii + 8 : n, kmax = kk + 8 < n ? kk + 8 : n, jmax = jj + 8 < n ? jj + 8 : n;
        for (size_t i = ii; i < imax; ++i) for (size_t k = kk; k < kmax; ++k) {
            double x = a[i * n + k];
            for (size_t j = jj; j < jmax; ++j) c[i * n + j] += x * b[k * n + j];
        }
    }
}
int main(int argc, char **argv) {
    unsigned long dimension, rounds, seed;
    if (argc != 4 || !argument(argv[1], 64, &dimension) || dimension == 0 || !argument(argv[2], 10, &rounds) || rounds == 0 || !argument(argv[3], 4294967295UL, &seed)) {
        fputs("matrix: DIMENSION[1..64] ROUNDS[1..10] SEED[uint32]\n", stderr); return 2;
    }
    size_t n = (size_t)dimension, cells = n * n;
    double *a = malloc(cells * sizeof(*a)), *b = malloc(cells * sizeof(*b)), *c = malloc(cells * sizeof(*c));
    if (!a || !b || !c) { free(a); free(b); free(c); fputs("matrix: allocation failed\n", stderr); return 3; }
    uint32_t state = (uint32_t)seed;
    for (size_t i = 0; i < cells; ++i) {
        a[i] = (double)((int)(next_value(&state) % 65) - 32) / 8;
        b[i] = (double)((int)(next_value(&state) % 65) - 32) / 8;
    }
    double checksum = 0, trace = 0;
    for (unsigned long round = 0; round < rounds; ++round) {
        multiply(a, b, c, n);
        for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) {
            double expected = 0;
            for (size_t k = 0; k < n; ++k) expected += a[i * n + k] * b[k * n + j];
            if (expected != c[i * n + j]) { free(a); free(b); free(c); fputs("matrix: kernel mismatch\n", stderr); return 1; }
            checksum += c[i * n + j] * (double)(i * n + j + 1);
            if (i == j) trace += c[i * n + j];
        }
    }
    printf("dimension=%zu rounds=%lu checksum=%.6f trace=%.6f\n", n, rounds, checksum, trace);
    free(a); free(b); free(c); return 0;
}
