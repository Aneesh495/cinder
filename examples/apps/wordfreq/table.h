#ifndef APP_WORD_TABLE_H
#define APP_WORD_TABLE_H
#include <stddef.h>
struct Word { char *text; unsigned long count; };
struct WordTable { struct Word *slots; size_t capacity, used; unsigned long tokens; };
int words_add(struct WordTable *table, const char *word);
struct Word **words_sorted(const struct WordTable *table);
void words_destroy(struct WordTable *table);
#endif
