#include "wordfreq/table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int word_byte(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
int main(int argc, char **argv) {
    if (argc > 2) { fputs("word-frequency: expected at most one input path\n", stderr); return 2; }
    FILE *input = stdin;
    if (argc == 2 && strcmp(argv[1], "-") != 0) input = fopen(argv[1], "rb");
    if (!input) { fputs("word-frequency: cannot open input\n", stderr); return 2; }
    struct WordTable table = {0}; char word[256]; size_t used = 0, bytes = 0; int status = 0, c;
    while ((c = fgetc(input)) != EOF) {
        if (++bytes > 1048576) { status = 2; break; }
        if (word_byte(c)) {
            if (used == sizeof(word) - 1) { status = 2; break; }
            word[used++] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        } else if (used) {
            word[used] = 0; if (!words_add(&table, word)) { status = 3; break; } used = 0;
        }
    }
    if (!status && used) { word[used] = 0; if (!words_add(&table, word)) status = 3; }
    if (ferror(input)) status = 2;
    if (input != stdin && fclose(input) != 0) status = 2;
    if (!status) {
        struct Word **sorted = words_sorted(&table);
        if (!sorted) status = 3;
        else {
            printf("tokens=%lu unique=%zu\n", table.tokens, table.used);
            for (size_t i = 0; i < table.used; ++i) printf("%s %lu\n", sorted[i]->text, sorted[i]->count);
            free(sorted);
        }
    }
    if (status) fputs(status == 3 ? "word-frequency: allocation limit\n" : "word-frequency: invalid or oversized input\n", stderr);
    words_destroy(&table); return status;
}
