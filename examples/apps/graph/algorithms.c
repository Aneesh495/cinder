#include "algorithms.h"
#include <stdlib.h>

struct Candidate { size_t node; unsigned long distance; };
struct Heap { struct Candidate *items; size_t used, capacity; };
static int heap_push(struct Heap *heap, struct Candidate item) {
    if (heap->used == heap->capacity) {
        size_t capacity = heap->capacity ? heap->capacity * 2 : 16;
        struct Candidate *items = realloc(heap->items, capacity * sizeof(*items));
        if (!items) return 0;
        heap->items = items; heap->capacity = capacity;
    }
    size_t at = heap->used++;
    while (at && heap->items[(at - 1) / 2].distance > item.distance) {
        heap->items[at] = heap->items[(at - 1) / 2]; at = (at - 1) / 2;
    }
    heap->items[at] = item; return 1;
}
static struct Candidate heap_pop(struct Heap *heap) {
    struct Candidate result = heap->items[0], last = heap->items[--heap->used];
    size_t at = 0;
    while (at * 2 + 1 < heap->used) {
        size_t child = at * 2 + 1;
        if (child + 1 < heap->used && heap->items[child + 1].distance < heap->items[child].distance) ++child;
        if (last.distance <= heap->items[child].distance) break;
        heap->items[at] = heap->items[child]; at = child;
    }
    if (heap->used) heap->items[at] = last;
    return result;
}
int graph_create(struct Graph *graph, size_t nodes, size_t edges) {
    *graph = (struct Graph){0};
    graph->heads = malloc(nodes * sizeof(*graph->heads));
    graph->edges = malloc((edges ? edges : 1) * sizeof(*graph->edges));
    if (!graph->heads || !graph->edges) { graph_destroy(graph); return 0; }
    graph->nodes = nodes; graph->capacity = edges;
    for (size_t i = 0; i < nodes; ++i) graph->heads[i] = (size_t)-1;
    return 1;
}
int graph_add(struct Graph *graph, size_t from, size_t to, unsigned long weight) {
    if (from >= graph->nodes || to >= graph->nodes || graph->used == graph->capacity) return 0;
    graph->edges[graph->used] = (struct Edge){to, graph->heads[from], weight};
    graph->heads[from] = graph->used++; return 1;
}
struct Paths graph_paths(const struct Graph *graph, size_t source) {
    struct Paths paths = {0}; struct Heap heap = {0};
    size_t *queue = malloc(graph->nodes * sizeof(*queue));
    paths.distance = malloc(graph->nodes * sizeof(*paths.distance));
    paths.hops = malloc(graph->nodes * sizeof(*paths.hops));
    if (!queue || !paths.distance || !paths.hops) goto done;
    for (size_t i = 0; i < graph->nodes; ++i) { paths.distance[i] = GRAPH_UNREACHABLE; paths.hops[i] = (size_t)-1; }
    size_t read = 0, write = 0; queue[write++] = source; paths.hops[source] = 0;
    while (read < write) {
        size_t from = queue[read++];
        for (size_t e = graph->heads[from]; e != (size_t)-1; e = graph->edges[e].next) {
            size_t to = graph->edges[e].to;
            if (paths.hops[to] == (size_t)-1) { paths.hops[to] = paths.hops[from] + 1; queue[write++] = to; }
        }
    }
    paths.distance[source] = 0;
    if (!heap_push(&heap, (struct Candidate){source, 0})) goto done;
    while (heap.used) {
        struct Candidate candidate = heap_pop(&heap);
        if (candidate.distance != paths.distance[candidate.node]) continue;
        for (size_t e = graph->heads[candidate.node]; e != (size_t)-1; e = graph->edges[e].next) {
            const struct Edge *edge = &graph->edges[e];
            unsigned long distance = candidate.distance + edge->weight;
            if (distance < paths.distance[edge->to]) {
                paths.distance[edge->to] = distance;
                if (!heap_push(&heap, (struct Candidate){edge->to, distance})) goto done;
            }
        }
    }
    paths.valid = 1;
done:
    free(queue); free(heap.items); return paths;
}
void paths_destroy(struct Paths *paths) {
    free(paths->distance); free(paths->hops); *paths = (struct Paths){0};
}
void graph_destroy(struct Graph *graph) {
    free(graph->heads); free(graph->edges); *graph = (struct Graph){0};
}
