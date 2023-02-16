#include "graph/algorithms.h"
#include <stdio.h>
#include <string.h>

static int number(FILE *file, unsigned long limit, unsigned long *value) {
    int c; do { c = fgetc(file); } while (c == ' ' || c == '\n' || c == '\t' || c == '\r');
    if (c < '0' || c > '9') return 0;
    unsigned long result = 0;
    do {
        unsigned long digit = (unsigned long)(c - '0');
        if (digit > limit || result > (limit - digit) / 10) return 0;
        result = result * 10 + digit; c = fgetc(file);
    } while (c >= '0' && c <= '9');
    if (c != EOF && c != ' ' && c != '\n' && c != '\t' && c != '\r') return 0;
    *value = result; return 1;
}
int main(int argc, char **argv) {
    if (argc > 2) { fputs("graph: expected at most one input path\n", stderr); return 2; }
    FILE *file = argc == 2 && strcmp(argv[1], "-") != 0 ? fopen(argv[1], "rb") : stdin;
    if (!file) { fputs("graph: cannot open input\n", stderr); return 2; }
    struct Graph graph = {0}; struct Paths paths = {0}; int status = 2;
    unsigned long nodes, edges, source;
    if (!number(file, 256, &nodes) || nodes == 0 || !number(file, 4096, &edges) || !number(file, nodes - 1, &source)) goto done;
    if (!graph_create(&graph, (size_t)nodes, (size_t)edges)) { status = 3; goto done; }
    for (size_t i = 0; i < edges; ++i) {
        unsigned long from, to, weight;
        if (!number(file, nodes - 1, &from) || !number(file, nodes - 1, &to) || !number(file, 1000000, &weight)) goto done;
        if (!graph_add(&graph, (size_t)from, (size_t)to, weight)) goto done;
    }
    int c; do { c = fgetc(file); } while (c == ' ' || c == '\n' || c == '\t' || c == '\r');
    if (c != EOF || ferror(file)) goto done;
    paths = graph_paths(&graph, (size_t)source);
    if (!paths.valid) { status = 3; goto done; }
    printf("nodes=%lu edges=%lu source=%lu\n", nodes, edges, source);
    for (size_t i = 0; i < nodes; ++i) {
        if (paths.distance[i] == GRAPH_UNREACHABLE) printf("%zu unreachable\n", i);
        else printf("%zu distance=%lu hops=%zu\n", i, paths.distance[i], paths.hops[i]);
    }
    status = 0;
done:
    paths_destroy(&paths); graph_destroy(&graph);
    if (file != stdin && fclose(file) != 0) status = 2;
    if (status) fputs(status == 3 ? "graph: allocation failed\n" : "graph: invalid input\n", stderr);
    return status;
}
