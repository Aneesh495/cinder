#include "table.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static size_t hash_word(const char *text) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (; *text; ++text) { hash ^= (unsigned char)*text; hash *= UINT64_C(1099511628211); }
    return (size_t)hash;
}
static size_t position(const struct Word *slots, size_t capacity, const char *text) {
    size_t index = hash_word(text) & (capacity - 1);
    while (slots[index].text && strcmp(slots[index].text, text) != 0) index = (index + 1) & (capacity - 1);
    return index;
}
static int grow(struct WordTable *table) {
    size_t capacity = table->capacity ? table->capacity * 2 : 16;
    if (capacity > 131072) return 0;
    struct Word *slots = calloc(capacity, sizeof(*slots));
    if (!slots) return 0;
    for (size_t i = 0; i < table->capacity; ++i) if (table->slots[i].text) {
        size_t index = position(slots, capacity, table->slots[i].text);
        slots[index] = table->slots[i];
    }
    free(table->slots); table->slots = slots; table->capacity = capacity;
    return 1;
}
int words_add(struct WordTable *table, const char *word) {
    if (!table->capacity || (table->used + 1) * 4 >= table->capacity * 3) if (!grow(table)) return 0;
    size_t index = position(table->slots, table->capacity, word);
    if (!table->slots[index].text) {
        size_t length = strlen(word) + 1;
        char *text = malloc(length);
        if (!text) return 0;
        memcpy(text, word, length); table->slots[index].text = text; ++table->used;
    }
    ++table->slots[index].count; ++table->tokens; return 1;
}
static int compare_words(const void *left, const void *right) {
    const struct Word *const *a = left, *const *b = right;
    return strcmp((*a)->text, (*b)->text);
}
struct Word **words_sorted(const struct WordTable *table) {
    struct Word **sorted = malloc((table->used ? table->used : 1) * sizeof(*sorted));
    if (!sorted) return NULL;
    size_t used = 0;
    for (size_t i = 0; i < table->capacity; ++i) if (table->slots[i].text) sorted[used++] = &table->slots[i];
    qsort(sorted, used, sizeof(*sorted), compare_words); return sorted;
}
void words_destroy(struct WordTable *table) {
    for (size_t i = 0; i < table->capacity; ++i) free(table->slots[i].text);
    free(table->slots); *table = (struct WordTable){0};
}
