#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

struct Parser {
    const unsigned char *text; size_t size, at, objects, arrays, strings, numbers, booleans, nulls, codepoints, depth;
};
static void space(struct Parser *p) {
    while (p->at < p->size && (p->text[p->at] == ' ' || p->text[p->at] == '\t' || p->text[p->at] == '\r' || p->text[p->at] == '\n')) ++p->at;
}
static int take(struct Parser *p, unsigned char c) {
    if (p->at == p->size || p->text[p->at] != c) return 0;
    ++p->at; return 1;
}
static int hex4(struct Parser *p, unsigned *value) {
    unsigned result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (p->at == p->size) return 0;
        unsigned char c = p->text[p->at++]; unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A') + 10;
        else return 0;
        result = result * 16 + digit;
    }
    *value = result; return 1;
}
static int string_value(struct Parser *p) {
    if (!take(p, '"')) return 0;
    while (p->at < p->size) {
        unsigned char c = p->text[p->at++];
        if (c == '"') { ++p->strings; return 1; }
        if (c < 32 || c >= 128) return 0;
        if (c == '\\') {
            if (p->at == p->size) return 0;
            c = p->text[p->at++];
            if (c == 'u') {
                unsigned value;
                if (!hex4(p, &value)) return 0;
                if (value >= 0xd800 && value <= 0xdbff) {
                    unsigned low;
                    if (!take(p, '\\') || !take(p, 'u') || !hex4(p, &low) || low < 0xdc00 || low > 0xdfff) return 0;
                } else if (value >= 0xdc00 && value <= 0xdfff) return 0;
            } else if (c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' && c != 'n' && c != 'r' && c != 't') return 0;
        }
        ++p->codepoints;
    }
    return 0;
}
static int digit(const struct Parser *p) { return p->at < p->size && p->text[p->at] >= '0' && p->text[p->at] <= '9'; }
static int number_value(struct Parser *p) {
    (void)take(p, '-');
    if (take(p, '0')) { if (digit(p)) return 0; }
    else {
        if (!digit(p) || p->text[p->at] == '0') return 0;
        do { ++p->at; } while (digit(p));
    }
    if (take(p, '.')) {
        if (!digit(p)) return 0;
        do { ++p->at; } while (digit(p));
    }
    if (take(p, 'e') || take(p, 'E')) {
        if (!take(p, '+')) (void)take(p, '-');
        if (!digit(p)) return 0;
        do { ++p->at; } while (digit(p));
    }
    ++p->numbers; return 1;
}
static int literal(struct Parser *p, const char *word) {
    for (; *word; ++word) if (!take(p, (unsigned char)*word)) return 0;
    return 1;
}
static int value(struct Parser *p, size_t depth) {
    if (depth > 128) return 0;
    if (depth > p->depth) p->depth = depth;
    space(p); if (p->at == p->size) return 0;
    unsigned char c = p->text[p->at];
    if (c == '"') return string_value(p);
    if (c == 'n') { if (!literal(p, "null")) return 0; ++p->nulls; return 1; }
    if (c == 't' || c == 'f') { if (!literal(p, c == 't' ? "true" : "false")) return 0; ++p->booleans; return 1; }
    if (c == '-' || (c >= '0' && c <= '9')) return number_value(p);
    if (c != '[' && c != '{') return 0;
    ++p->at; unsigned char close = c == '[' ? ']' : '}';
    if (c == '[') ++p->arrays; else ++p->objects;
    space(p); if (take(p, close)) return 1;
    for (;;) {
        if (c == '{') { if (!string_value(p)) return 0; space(p); if (!take(p, ':')) return 0; }
        if (!value(p, depth + 1)) return 0;
        space(p); if (take(p, close)) return 1;
        if (!take(p, ',')) return 0;
        space(p);
    }
}
int main(int argc, char **argv) {
    if (argc > 2) { fputs("json: expected at most one input path\n", stderr); return 2; }
    FILE *file = argc == 2 ? fopen(argv[1], "rb") : stdin;
    if (!file) { fputs("json: cannot open input\n", stderr); return 2; }
    size_t capacity = 4096, size = 0; unsigned char *text = malloc(capacity); int status = 0, c;
    if (!text) status = 3;
    while (!status && (c = fgetc(file)) != EOF) {
        if (size == 1048576) { status = 2; break; }
        if (size == capacity) {
            unsigned char *next = realloc(text, capacity * 2);
            if (!next) { status = 3; break; }
            text = next; capacity *= 2;
        }
        text[size++] = (unsigned char)c;
    }
    if (ferror(file)) status = 2;
    if (file != stdin && fclose(file) != 0) status = 2;
    struct Parser parser = {0}; parser.text = text; parser.size = size;
    if (!status) { if (!value(&parser, 1)) status = 2; else { space(&parser); if (parser.at != size) status = 2; } }
    if (!status) printf("objects=%zu arrays=%zu strings=%zu numbers=%zu booleans=%zu nulls=%zu codepoints=%zu depth=%zu\n", parser.objects, parser.arrays, parser.strings, parser.numbers, parser.booleans, parser.nulls, parser.codepoints, parser.depth);
    else fputs(status == 3 ? "json: allocation failed\n" : "json: invalid or unsupported input\n", stderr);
    free(text); return status;
}
