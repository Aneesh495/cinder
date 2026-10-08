#include "cinder.h"

#include <string.h>

size_t cinder_init_first(const CinderType *type) {
    size_t index = 0U;
    if (type->kind == TYPE_STRUCT || type->kind == TYPE_UNION)
        while (index < type->fields.len && type->fields.data[index].is_bitfield && type->fields.data[index].name == NULL) ++index;
    return index;
}

size_t cinder_init_child_count(const CinderType *type) {
    if (type->kind == TYPE_ARRAY) return type->complete ? type->array_len : SIZE_MAX;
    if (type->kind == TYPE_STRUCT && type->fields.len != 0U) {
        const CinderType *last = type->fields.data[type->fields.len - 1U].type;
        return type->fields.len - (size_t)(last->kind == TYPE_ARRAY && !last->complete);
    }
    if (type->kind == TYPE_UNION) return type->fields.len;
    return 1U;
}

CinderType *cinder_init_child(CinderInitFrame frame, size_t *offset) {
    *offset = frame.offset;
    if (frame.type->kind == TYPE_ARRAY) { *offset += frame.index * frame.type->base->size; return frame.type->base; }
    if (frame.type->kind == TYPE_STRUCT || frame.type->kind == TYPE_UNION) { *offset += frame.type->fields.data[frame.index].offset; return frame.type->fields.data[frame.index].type; }
    return frame.type;
}

CinderInitMemberResult cinder_init_member(CinderInitFrame *frames, size_t *depth, size_t capacity, const char *name) {
    size_t path[64]; size_t length = cinder_type_member_path(frames[*depth - 1U].type, name, path, CINDER_ARRAY_LEN(path));
    if (length == 0U || length == SIZE_MAX) return INIT_MEMBER_UNKNOWN;
    for (size_t p = 0U; p < length; ++p) {
        CinderInitFrame *frame = &frames[*depth - 1U];
        if (path[p] >= cinder_init_child_count(frame->type)) return INIT_MEMBER_FLEXIBLE;
        frame->index = path[p];
        if (p + 1U < length) {
            if (*depth == capacity) return INIT_MEMBER_DEPTH;
            size_t offset; CinderType *child = cinder_init_child(*frame, &offset);
            frames[(*depth)++] = (CinderInitFrame){child, cinder_init_first(child), offset};
        }
    }
    return INIT_MEMBER_OK;
}

void cinder_init_advance(CinderInitFrame *frames, size_t *depth) {
    while (*depth != 0U) {
        CinderInitFrame *frame = &frames[*depth - 1U];
        if (frame->type->kind == TYPE_UNION) frame->index = frame->type->fields.len;
        else {
            ++frame->index;
            if (frame->type->kind == TYPE_STRUCT)
                while (frame->index < frame->type->fields.len && frame->type->fields.data[frame->index].is_bitfield && frame->type->fields.data[frame->index].name == NULL) ++frame->index;
        }
        if (frame->index < cinder_init_child_count(frame->type) || *depth == 1U) return;
        --*depth;
    }
}

static bool aggregate(const CinderType *type) {
    return type->kind == TYPE_ARRAY || type->kind == TYPE_STRUCT || type->kind == TYPE_UNION;
}

static bool same_object_type(const CinderType *left, const CinderType *right) {
    if (right == NULL) return false;
    CinderType a = *left, b = *right; a.qualifiers = 0U; b.qualifiers = 0U;
    return cinder_type_compatible(&a, &b);
}

/* Type queries walk initializer structure, never evaluate initializer values.
 * Semantic planning later checks every value and uses the same cursor rules. */
bool cinder_infer_initializer_shape(CinderAst *ast, CinderDecl *decl, unsigned nesting) {
    CinderType *type = decl->type;
    if (type->kind != TYPE_ARRAY || type->complete) return true;
    if (nesting >= 64U || type->base == NULL || !type->base->complete || type->base->size == 0U || type->base->size > 64U * 1024U * 1024U || decl->initializer == NULL) return false;
    if (decl->initializer->kind == EX_STRING && type->base->kind == TYPE_CHAR) {
        size_t size = decl->initializer->literal_length + 1U;
        if (size > 64U * 1024U * 1024U) return false;
        type->array_len = size; type->size = size; type->complete = true; type->completion_index = decl->initializer_index;
        return true;
    }
    if (decl->initializer->kind != EX_INIT_LIST) return false;
    const CinderExpr *list = decl->initializer;
    size_t limit = 64U * 1024U * 1024U / type->base->size, extent = 0U;
    CinderInitFrame frames[64]; size_t depth = 1U; frames[0] = (CinderInitFrame){type, cinder_init_first(type), 0U};
    if (type->base->kind == TYPE_CHAR && list->as.initializer.entries.len == 1U && list->as.initializer.entries.data[0].designators.len == 0U && list->as.initializer.entries.data[0].value->kind == EX_STRING) extent = list->as.initializer.entries.data[0].value->literal_length + 1U;
    else for (size_t e = 0U; e < list->as.initializer.entries.len; ++e) {
        const CinderInitEntry *entry = &list->as.initializer.entries.data[e];
        if (entry->designators.len != 0U) {
            depth = 1U; frames[0] = (CinderInitFrame){type, cinder_init_first(type), 0U};
            for (size_t d = 0U; d < entry->designators.len; ++d) {
                CinderInitFrame *frame = &frames[depth - 1U];
                const CinderInitDesignator *designator = &entry->designators.data[d];
                if (designator->member != NULL && (frame->type->kind == TYPE_STRUCT || frame->type->kind == TYPE_UNION)) {
                    if (cinder_init_member(frames, &depth, CINDER_ARRAY_LEN(frames), designator->member) != INIT_MEMBER_OK) return false;
                    frame = &frames[depth - 1U];
                } else if (designator->index != NULL && frame->type->kind == TYPE_ARRAY) {
                    int64_t index; CinderType *index_type;
                    if (!cinder_constant_integer(ast, designator->index, &index, &index_type) || index < 0 || (uint64_t)index >= cinder_init_child_count(frame->type) || frame->type->base->size == 0U || (uint64_t)index > 64U * 1024U * 1024U / frame->type->base->size) return false;
                    frame->index = (size_t)index;
                } else return false;
                if (d + 1U < entry->designators.len) {
                    size_t offset; CinderType *child = cinder_init_child(*frame, &offset);
                    if (!aggregate(child) || depth == CINDER_ARRAY_LEN(frames)) return false;
                    frames[depth++] = (CinderInitFrame){child, cinder_init_first(child), offset};
                }
            }
        }
        if (frames[0].index >= limit || frames[depth - 1U].index >= cinder_init_child_count(frames[depth - 1U].type)) return false;
        if (frames[0].index >= extent) extent = frames[0].index + 1U;
        size_t offset; CinderType *child = cinder_init_child(frames[depth - 1U], &offset);
        if (entry->value->kind != EX_INIT_LIST && !(entry->value->kind == EX_STRING && child->kind == TYPE_ARRAY && child->base->kind == TYPE_CHAR)) {
            CinderType *value = cinder_expression_type(ast, entry->value, nesting + 1U);
            while (aggregate(child) && !same_object_type(child, value)) {
                if (!child->complete || depth == CINDER_ARRAY_LEN(frames) || cinder_init_child_count(child) == 0U) return false;
                frames[depth++] = (CinderInitFrame){child, cinder_init_first(child), offset}; child = cinder_init_child(frames[depth - 1U], &offset);
            }
        }
        cinder_init_advance(frames, &depth);
    }
    if (extent == 0U || extent > limit) return false;
    type->array_len = extent; type->size = extent * type->base->size; type->complete = true; type->completion_index = decl->initializer_index;
    return true;
}
