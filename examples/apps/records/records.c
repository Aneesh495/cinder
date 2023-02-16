#include "records.h"
#include <stdlib.h>
#include <string.h>

static int append(struct Records *records, char *line) {
    char *comma = strchr(line, ',');
    if (!comma || comma == line || (size_t)(comma - line) >= 32) return 0;
    *comma = 0;
    for (char *p = line; *p; ++p) if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return 0;
    char *end; long value = strtol(comma + 1, &end, 10);
    if (end == comma + 1 || value < -1000000 || value > 1000000) return 0;
    while (*end == ' ' || *end == '\t' || *end == '\r') ++end;
    if (*end || records->size == 100000) return 0;
    if (records->size == records->capacity) {
        size_t capacity = records->capacity ? records->capacity * 2 : 16;
        struct Record *data = realloc(records->data, capacity * sizeof(*data));
        if (!data) return 0;
        records->data = data; records->capacity = capacity;
    }
    struct Record *record = &records->data[records->size++];
    strcpy(record->key, line); record->value = value; return 1;
}
int records_read(FILE *file, struct Records *records) {
    char line[128]; size_t used = 0; int c;
    while ((c = fgetc(file)) != EOF) {
        if (c == 0) return 0;
        if (c == '\n') {
            line[used] = 0; if (used && !append(records, line)) return 0; used = 0;
        } else { if (used == sizeof(line) - 1) return 0; line[used++] = (char)c; }
    }
    line[used] = 0;
    return !ferror(file) && (!used || append(records, line));
}
static int by_key(const void *left, const void *right) {
    const struct Record *a = left, *b = right;
    int order = strcmp(a->key, b->key);
    return order ? order : (a->value > b->value) - (a->value < b->value);
}
static int by_value(const void *left, const void *right) {
    const struct Record *a = left, *b = right;
    int order = (a->value > b->value) - (a->value < b->value);
    return order ? order : strcmp(a->key, b->key);
}
void records_group(FILE *output, struct Records *records) {
    if (records->size) qsort(records->data, records->size, sizeof(*records->data), by_key);
    size_t groups = 0;
    for (size_t i = 0; i < records->size; ++i) if (!i || strcmp(records->data[i - 1].key, records->data[i].key) != 0) ++groups;
    fprintf(output, "records=%zu groups=%zu\n", records->size, groups);
    for (size_t begin = 0; begin < records->size;) {
        size_t end = begin; long sum = 0;
        while (end < records->size && strcmp(records->data[begin].key, records->data[end].key) == 0) sum += records->data[end++].value;
        fprintf(output, "%s count=%zu sum=%ld min=%ld max=%ld\n", records->data[begin].key, end - begin, sum, records->data[begin].value, records->data[end - 1].value);
        begin = end;
    }
}
void records_select(FILE *output, struct Records *records, long minimum, long maximum) {
    if (records->size) qsort(records->data, records->size, sizeof(*records->data), by_value);
    size_t selected = 0;
    for (size_t i = 0; i < records->size; ++i) if (records->data[i].value >= minimum && records->data[i].value <= maximum) {
        fprintf(output, "%s,%ld\n", records->data[i].key, records->data[i].value); ++selected;
    }
    fprintf(output, "selected=%zu\n", selected);
}
void records_destroy(struct Records *records) { free(records->data); *records = (struct Records){0}; }
