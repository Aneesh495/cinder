#include "cinder.h"

#include <limits.h>
#include <string.h>

static CinderABIClass merge_class(CinderABIClass left, CinderABIClass right) {
    if (left == right || right == ABI_NONE) return left;
    if (left == ABI_NONE) return right;
    if (left == ABI_MEMORY || right == ABI_MEMORY) return ABI_MEMORY;
    if (left == ABI_INTEGER || right == ABI_INTEGER) return ABI_INTEGER;
    return ABI_SSE;
}

static bool classify_part(const CinderType *type, size_t offset, CinderABIValue *value, unsigned depth) {
    if (type == NULL || !type->complete || type->size == 0U || type->align == 0U || depth >= 64U || offset > 16U || type->size > 16U - offset) return false;
    if (offset % type->align != 0U) return false;
    if (type->kind == TYPE_ARRAY) {
        if (type->base == NULL || type->base->size == 0U || type->array_len > type->size / type->base->size) return false;
        for (size_t i = 0U; i < type->array_len; ++i)
            if (!classify_part(type->base, offset + i * type->base->size, value, depth + 1U)) return false;
        return true;
    }
    if (type->kind == TYPE_STRUCT || type->kind == TYPE_UNION) {
        for (size_t f = 0U; f < type->fields.len; ++f) {
            const CinderField *field = &type->fields.data[f];
            if (field->offset > type->size || field->type == NULL || field->type->size > type->size - field->offset || !classify_part(field->type, offset + field->offset, value, depth + 1U)) return false;
        }
        return type->fields.len != 0U;
    }
    CinderABIClass part;
    if (type->kind == TYPE_FLOAT || type->kind == TYPE_DOUBLE) part = ABI_SSE;
    else if (type->kind == TYPE_BOOL || type->kind == TYPE_CHAR || type->kind == TYPE_SHORT || type->kind == TYPE_INT || type->kind == TYPE_LONG || type->kind == TYPE_LLONG || type->kind == TYPE_ENUM || type->kind == TYPE_POINTER) part = ABI_INTEGER;
    else return false;
    if (type->size > 8U) return false;
    size_t first = offset / 8U, last = (offset + type->size - 1U) / 8U;
    for (size_t p = first; p <= last; ++p) value->classes[p] = merge_class(value->classes[p], part);
    return true;
}

bool cinder_abi_classify(const CinderType *type, CinderABIValue *value) {
    memset(value, 0, sizeof(*value));
    if (type == NULL) return false;
    value->size = type->size; value->align = type->align;
    if (type->kind == TYPE_VOID) return true;
    if (!type->complete || type->size == 0U || type->size > 64U * 1024U * 1024U || type->align == 0U || type->align > 16U || (type->align & (type->align - 1U)) != 0U) return false;
    bool aggregate = type->kind == TYPE_STRUCT || type->kind == TYPE_UNION || type->kind == TYPE_ARRAY;
    if (aggregate && (type->size > 16U || !classify_part(type, 0U, value, 0U))) {
        value->memory = true; value->classes[0] = ABI_MEMORY; value->classes[1] = ABI_NONE;
        return true;
    }
    if (!aggregate && !classify_part(type, 0U, value, 0U)) return false;
    value->count = (unsigned)((type->size + 7U) / 8U);
    return true;
}

bool cinder_abi_place(const CinderType *type, CinderABIState *state, CinderABIArgument *argument) {
    memset(argument, 0, sizeof(*argument));
    argument->stack_offset = SIZE_MAX;
    argument->registers[0] = UINT_MAX; argument->registers[1] = UINT_MAX;
    if (state->gpr > 6U || state->sse > 8U || !cinder_abi_classify(type, &argument->value) || argument->value.size == 0U) return false;
    unsigned gpr = 0U, sse = 0U;
    for (unsigned p = 0U; p < argument->value.count; ++p) {
        if (argument->value.classes[p] == ABI_INTEGER) ++gpr;
        else if (argument->value.classes[p] == ABI_SSE) ++sse;
    }
    if (argument->value.memory || gpr > 6U - state->gpr || sse > 8U - state->sse) {
        size_t align = argument->value.align < 8U ? 8U : argument->value.align;
        size_t size = (argument->value.size + 7U) & ~(size_t)7U;
        if (state->stack > 64U * 1024U * 1024U - (align - 1U)) return false;
        size_t offset = (state->stack + align - 1U) & ~(align - 1U);
        if (size > 64U * 1024U * 1024U - offset) return false;
        argument->stack_offset = offset; state->stack = offset + size;
        return true;
    }
    for (unsigned p = 0U; p < argument->value.count; ++p) {
        if (argument->value.classes[p] == ABI_INTEGER) argument->registers[p] = state->gpr++;
        else if (argument->value.classes[p] == ABI_SSE) argument->registers[p] = state->sse++;
    }
    return true;
}

bool cinder_va_pointer_type(const CinderType *type) {
    if (type == NULL || type->kind != TYPE_POINTER || type->base == NULL) return false;
    const CinderType *state = type->base;
    if (state->kind != TYPE_STRUCT || !state->complete || state->size != 24U || state->align != 8U || state->qualifiers != 0U || state->fields.len != 4U || state->tag == NULL || strcmp(state->tag, "__cinder_va_state") != 0) return false;
    static const char *names[] = {"gp_offset", "fp_offset", "overflow_arg_area", "reg_save_area"};
    static const size_t offsets[] = {0U, 4U, 8U, 16U};
    for (size_t i = 0U; i < 4U; ++i) {
        const CinderField *field = &state->fields.data[i];
        if (field->name == NULL || field->type == NULL || strcmp(field->name, names[i]) != 0 || field->offset != offsets[i] || field->type->qualifiers != 0U) return false;
        if (i < 2U) { if (field->type->kind != TYPE_INT || !field->type->is_unsigned || field->type->size != 4U) return false; }
        else if (field->type->kind != TYPE_POINTER || field->type->base == NULL || field->type->base->kind != TYPE_VOID) return false;
    }
    return true;
}
