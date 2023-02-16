#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

struct Item { long value; size_t original; };
static int compare(const void *left, const void *right) {
    const struct Item *a = left, *b = right;
    int order = (a->value > b->value) - (a->value < b->value);
    return order ? order : (a->original > b->original) - (a->original < b->original);
}
static void merge_sort(struct Item *items, struct Item *scratch, size_t begin, size_t end) {
    if (end - begin < 2) return;
    size_t middle = begin + (end - begin) / 2;
    merge_sort(items, scratch, begin, middle); merge_sort(items, scratch, middle, end);
    size_t left = begin, right = middle, out = begin;
    while (left < middle || right < end) {
        if (right == end || (left < middle && items[left].value <= items[right].value)) scratch[out++] = items[left++];
        else scratch[out++] = items[right++];
    }
    for (size_t i = begin; i < end; ++i) items[i] = scratch[i];
}
static size_t lower(const struct Item *items, size_t count, long value, int upper) {
    size_t begin = 0, end = count;
    while (begin < end) {
        size_t middle = begin + (end - begin) / 2;
        if (items[middle].value < value || (upper && items[middle].value == value)) begin = middle + 1;
        else end = middle;
    }
    return begin;
}
static int parse_number(const char *text, long *value) {
    char *end; *value = strtol(text, &end, 10);
    return end != text && !*end && *value >= -1000000 && *value <= 1000000;
}
int main(int argc, char **argv) {
    if (argc > 33) { fputs("sort-search: at most 32 queries\n", stderr); return 2; }
    long queries[32];
    for (int i = 1; i < argc; ++i) if (!parse_number(argv[i], &queries[i - 1])) { fputs("sort-search: invalid query\n", stderr); return 2; }
    size_t capacity = 16, count = 0, used = 0; struct Item *items = malloc(capacity * sizeof(*items));
    char token[32]; int c, status = items ? 0 : 3;
    while (!status) {
        c = fgetc(stdin);
        if (c != EOF && c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            if (used == sizeof(token) - 1 || c == 0) { status = 2; break; }
            token[used++] = (char)c; continue;
        }
        if (used) {
            token[used] = 0; long value;
            if (!parse_number(token, &value) || count == 100000) { status = 2; break; }
            if (count == capacity) {
                struct Item *next = realloc(items, capacity * 2 * sizeof(*items));
                if (!next) { status = 3; break; }
                items = next; capacity *= 2;
            }
            items[count] = (struct Item){value, count}; ++count; used = 0;
        }
        if (c == EOF) break;
    }
    if (ferror(stdin)) status = 2;
    struct Item *scratch = NULL, *reference = NULL;
    if (!status) {
        scratch = malloc((count ? count : 1) * sizeof(*scratch)); reference = malloc((count ? count : 1) * sizeof(*reference));
        if (!scratch || !reference) status = 3;
    }
    if (!status) {
        for (size_t i = 0; i < count; ++i) reference[i] = items[i];
        qsort(reference, count, sizeof(*reference), compare); merge_sort(items, scratch, 0, count);
        uint64_t checksum = 0;
        for (size_t i = 0; i < count; ++i) {
            if (compare(&items[i], &reference[i]) != 0) { status = 1; break; }
            checksum += (uint64_t)(i + 1) * (uint64_t)(items[i].value + 1000000) + (uint64_t)items[i].original;
        }
        if (!status) {
            printf("count=%zu min=%ld max=%ld checksum=%" PRIu64 "\n", count, count ? items[0].value : 0, count ? items[count - 1].value : 0, checksum);
            for (int i = 1; i < argc; ++i) printf("query=%ld lower=%zu upper=%zu\n", queries[i - 1], lower(items, count, queries[i - 1], 0), lower(items, count, queries[i - 1], 1));
        }
    }
    free(items); free(scratch); free(reference);
    if (status) fputs(status == 1 ? "sort-search: sort mismatch\n" : "sort-search: invalid input or allocation limit\n", stderr);
    return status;
}
