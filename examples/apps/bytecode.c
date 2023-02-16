#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Instruction { int opcode; long operand; };
static const char *const names[] = {"push","load","store","add","sub","mul","lt","jz","jmp","dup","print","halt"};
static int parse(FILE *input, struct Instruction *code, size_t *count) {
    char line[160]; size_t used = 0; int c;
    for (;;) {
        c = fgetc(input);
        if (c == 0) return 0;
        if (c != EOF && c != '\n') { if (used == sizeof(line) - 1) return 0; line[used++] = (char)c; continue; }
        line[used] = 0; char *p = line; while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
        if (*p && *p != '#') {
            char *word = p; while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '#') ++p;
            char delimiter = *p; *p = 0;
            int opcode = -1;
            for (int i = 0; i < 12; ++i) if (strcmp(word, names[i]) == 0) opcode = i;
            if (opcode < 0 || *count == 1024) return 0;
            if (delimiter && delimiter != '#') ++p;
            while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
            long operand = 0;
            if (opcode <= 2 || opcode == 7 || opcode == 8) {
                char *end; if (!*p || *p == '#') return 0;
                operand = strtol(p, &end, 10);
                if (end == p || operand < -1000000 || operand > 1000000) return 0;
                p = end; while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
            }
            if (*p && *p != '#') return 0;
            code[(*count)++] = (struct Instruction){opcode, operand};
        }
        used = 0; if (c == EOF) break;
    }
    return *count != 0 && !ferror(input);
}
static int execute(const struct Instruction *code, size_t count) {
    long stack[256], locals[16] = {0}; size_t top = 0, pc = 0, steps = 0;
    while (pc < count && steps++ < 100000) {
        struct Instruction inst = code[pc++]; long a, b, result;
        switch (inst.opcode) {
        case 0:
            if (top == 256) return 0;
            stack[top++] = inst.operand; break;
        case 1:
            if (inst.operand < 0 || inst.operand >= 16 || top == 256) return 0;
            stack[top++] = locals[inst.operand]; break;
        case 2:
            if (inst.operand < 0 || inst.operand >= 16 || !top) return 0;
            locals[inst.operand] = stack[--top]; break;
        case 3: case 4: case 5: case 6:
            if (top < 2) return 0;
            b = stack[--top]; a = stack[--top];
            result = inst.opcode == 3 ? a + b : inst.opcode == 4 ? a - b : inst.opcode == 5 ? a * b : a < b;
            if (result < -1000000000L || result > 1000000000L) return 0;
            stack[top++] = result; break;
        case 7: case 8:
            if (inst.operand < 0 || (size_t)inst.operand >= count) return 0;
            if (inst.opcode == 7) { if (!top) return 0; if (stack[--top]) break; }
            pc = (size_t)inst.operand; break;
        case 9:
            if (!top || top == 256) return 0;
            stack[top] = stack[top - 1]; ++top; break;
        case 10:
            if (!top) return 0;
            printf("%ld\n", stack[--top]); break;
        case 11:
            printf("halt steps=%zu stack=%zu\n", steps, top); return 1;
        default: return 0;
        }
    }
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 2) { fputs("bytecode: expected program path\n", stderr); return 2; }
    FILE *input = fopen(argv[1], "rb");
    if (!input) { fputs("bytecode: cannot open program\n", stderr); return 2; }
    struct Instruction *code = malloc(1024 * sizeof(*code)); size_t count = 0; int valid = 0;
    if (code) valid = parse(input, code, &count);
    if (fclose(input) != 0) valid = 0;
    if (valid) valid = execute(code, count);
    free(code);
    if (!valid) { fputs("bytecode: invalid program or execution limit\n", stderr); return 2; }
    return 0;
}
