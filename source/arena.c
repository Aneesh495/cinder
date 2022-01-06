#include "cinder.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct CinderArenaBlock {
    struct CinderArenaBlock *next;
    size_t used;
    size_t capacity;
    unsigned char data[];
};

static size_t checked_add(size_t a, size_t b) {
    if (b > SIZE_MAX - a) {
        fprintf(stderr, "cinder: allocation size overflow\n");
        abort();
    }
    return a + b;
}

static size_t checked_mul(size_t a, size_t b) {
    if (a != 0U && b > SIZE_MAX / a) {
        fprintf(stderr, "cinder: allocation size overflow\n");
        abort();
    }
    return a * b;
}

void *cinder_alloc(size_t size) {
    size_t actual = size == 0U ? 1U : size;
    void *result = malloc(actual);
    if (result == NULL) {
        fprintf(stderr, "cinder: out of memory while allocating %zu bytes\n", actual);
        abort();
    }
    return result;
}

void *cinder_realloc(void *ptr, size_t size) {
    size_t actual = size == 0U ? 1U : size;
    void *result = realloc(ptr, actual);
    if (result == NULL) {
        fprintf(stderr, "cinder: out of memory while reallocating %zu bytes\n", actual);
        abort();
    }
    return result;
}

char *cinder_strndup(const char *text, size_t len) {
    char *result = cinder_alloc(checked_add(len, 1U));
    memcpy(result, text, len);
    result[len] = '\0';
    return result;
}

void cinder_vec_init(CinderVec *v, size_t elem_size) {
    (void)elem_size;
    v->data = NULL;
    v->len = 0U;
    v->cap = 0U;
}

void cinder_vec_free(CinderVec *v) {
    free(v->data);
    v->data = NULL;
    v->len = 0U;
    v->cap = 0U;
}

void *cinder_vec_push_impl(CinderVec *v, const void *elem, size_t elem_size) {
    if (v->len == v->cap) {
        size_t next = v->cap == 0U ? 8U : checked_mul(v->cap, 2U);
        v->data = cinder_realloc(v->data, checked_mul(next, elem_size));
        v->cap = next;
    }
    unsigned char *slot = v->data + checked_mul(v->len, elem_size);
    if (elem != NULL) memcpy(slot, elem, elem_size); else memset(slot, 0, elem_size);
    v->len++;
    return slot;
}

void *cinder_vec_at_impl(CinderVec *v, size_t index, size_t elem_size) {
    if (index >= v->len) return NULL;
    return v->data + checked_mul(index, elem_size);
}

const void *cinder_vec_cat_impl(const CinderVec *v, size_t index, size_t elem_size) {
    if (index >= v->len) return NULL;
    return v->data + checked_mul(index, elem_size);
}

static size_t align_offset(const unsigned char *base, size_t used, size_t align) {
    if (align == 0U || (align & (align - 1U)) != 0U) {
        fprintf(stderr, "cinder: invalid arena alignment\n");
        abort();
    }
    uintptr_t current = (uintptr_t)base + used;
    uintptr_t mask = (uintptr_t)align - 1U;
    if (current > UINTPTR_MAX - mask) {
        fprintf(stderr, "cinder: arena alignment overflow\n");
        abort();
    }
    uintptr_t aligned = (current + mask) & ~mask;
    return (size_t)(aligned - (uintptr_t)base);
}

void cinder_arena_init(CinderArena *arena, size_t block_size) {
    arena->first = NULL;
    arena->last = NULL;
    arena->block_size = block_size < 4096U ? 4096U : block_size;
}

void cinder_arena_destroy(CinderArena *arena) {
    CinderArenaBlock *block = arena->first;
    while (block != NULL) {
        CinderArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    arena->first = NULL;
    arena->last = NULL;
}

void *cinder_arena_alloc(CinderArena *arena, size_t size, size_t align) {
    size_t actual = size == 0U ? 1U : size;
    CinderArenaBlock *block = arena->last;
    if (block != NULL) {
        size_t aligned = align_offset(block->data, block->used, align);
        if (aligned <= block->capacity && actual <= block->capacity - aligned) {
            void *result = block->data + aligned;
            block->used = aligned + actual;
            return result;
        }
    }
    size_t capacity = arena->block_size > actual ? arena->block_size : actual;
    size_t total = checked_add(sizeof(CinderArenaBlock), capacity);
    block = cinder_alloc(total);
    block->next = NULL;
    block->used = 0U;
    block->capacity = capacity;
    if (arena->last == NULL) {
        arena->first = block;
    } else {
        arena->last->next = block;
    }
    arena->last = block;
    size_t aligned = align_offset(block->data, block->used, align);
    void *result = block->data + aligned;
    block->used = aligned + actual;
    return result;
}

char *cinder_arena_strndup(CinderArena *arena, const char *text, size_t len) {
    char *result = cinder_arena_alloc(arena, checked_add(len, 1U), _Alignof(char));
    memcpy(result, text, len);
    result[len] = '\0';
    return result;
}
