#ifndef APP_GRAPH_ALGORITHMS_H
#define APP_GRAPH_ALGORITHMS_H
#include <stddef.h>
#define GRAPH_UNREACHABLE 1000000000UL
struct Edge { size_t to, next; unsigned long weight; };
struct Graph { size_t nodes, used, capacity; size_t *heads; struct Edge *edges; };
struct Paths { unsigned long *distance; size_t *hops; int valid; };
int graph_create(struct Graph *graph, size_t nodes, size_t edges);
int graph_add(struct Graph *graph, size_t from, size_t to, unsigned long weight);
struct Paths graph_paths(const struct Graph *graph, size_t source);
void paths_destroy(struct Paths *paths);
void graph_destroy(struct Graph *graph);
#endif
