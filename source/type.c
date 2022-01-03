#include "cinder.h"

#include <stdlib.h>
#include <string.h>

static void set_scalar(CinderType *type, size_t size, size_t align, bool is_unsigned) {
    type->size = size;
    type->align = align;
    type->complete = true;
    type->is_unsigned = is_unsigned;
}

static CinderType *type_scalar(CinderTypeContext *types, CinderTypeKind kind, size_t size, size_t align, bool is_unsigned) {
    CinderType *type = cinder_type_new(types, kind);
    set_scalar(type, size, align, is_unsigned);
    return type;
}

void cinder_types_init(CinderTypeContext *types) {
    cinder_arena_init(&types->arena, 8192U);
    types->error_type = cinder_type_new(types, TYPE_ERROR);
    types->error_type->complete = false;
    types->void_type = cinder_type_new(types, TYPE_VOID);
    types->void_type->complete = true;
    types->void_type->size = 0U;
    types->void_type->align = 1U;
    types->bool_type = type_scalar(types, TYPE_BOOL, 1U, 1U, true);
    types->char_type = type_scalar(types, TYPE_CHAR, 1U, 1U, false);
    types->short_type = type_scalar(types, TYPE_SHORT, 2U, 2U, false);
    types->int_type = type_scalar(types, TYPE_INT, 4U, 4U, false);
    types->uint_type = type_scalar(types, TYPE_INT, 4U, 4U, true);
    types->long_type = type_scalar(types, TYPE_LONG, 8U, 8U, false);
    types->ulong_type = type_scalar(types, TYPE_LONG, 8U, 8U, true);
    types->llong_type = type_scalar(types, TYPE_LLONG, 8U, 8U, false);
    types->ullong_type = type_scalar(types, TYPE_LLONG, 8U, 8U, true);
    types->float_type = type_scalar(types, TYPE_FLOAT, 4U, 4U, false);
    types->double_type = type_scalar(types, TYPE_DOUBLE, 8U, 8U, false);
}

void cinder_types_destroy(CinderTypeContext *types) {
    cinder_arena_destroy(&types->arena);
}

CinderType *cinder_type_new(CinderTypeContext *types, CinderTypeKind kind) {
    CinderType *type = cinder_arena_alloc(&types->arena, sizeof(*type), _Alignof(CinderType));
    memset(type, 0, sizeof(*type));
    type->kind = kind;
    type->align = 1U;
    type->complete = false;
    type->params.data = NULL;
    type->params.len = 0U;
    type->params.cap = 0U;
    type->fields.data = NULL;
    type->fields.len = 0U;
    type->fields.cap = 0U;
    return type;
}

CinderType *cinder_type_pointer(CinderTypeContext *types, CinderType *base) {
    CinderType *type = cinder_type_new(types, TYPE_POINTER);
    type->base = base;
    type->size = 8U;
    type->align = 8U;
    type->complete = true;
    return type;
}

CinderType *cinder_type_array(CinderTypeContext *types, CinderType *base, size_t length) {
    CinderType *type = cinder_type_new(types, TYPE_ARRAY);
    type->base = base;
    type->array_len = length;
    type->size = base->size * length;
    type->align = base->align;
    type->complete = base->complete;
    return type;
}

CinderType *cinder_type_function(CinderTypeContext *types, CinderType *ret, const CinderParamVec *params) {
    CinderType *type = cinder_type_new(types, TYPE_FUNCTION);
    type->return_type = ret;
    type->complete = true;
    for (size_t i = 0U; i < params->len; ++i) {
        CinderParam param = params->data[i];
        cinder_vec_push((CinderVec *)&type->params, &param);
    }
    return type;
}

bool cinder_type_equal(const CinderType *a, const CinderType *b) {
    if (a == b) return true;
    if (a == NULL || b == NULL || a->kind != b->kind || a->is_unsigned != b->is_unsigned || a->qualifiers != b->qualifiers) return false;
    if (a->kind == TYPE_POINTER) return cinder_type_equal(a->base, b->base);
    if (a->kind == TYPE_ARRAY) return a->array_len == b->array_len && cinder_type_equal(a->base, b->base);
    if (a->kind == TYPE_FUNCTION) {
        if (!cinder_type_equal(a->return_type, b->return_type) || a->params.len != b->params.len) return false;
        for (size_t i = 0U; i < a->params.len; ++i) if (!cinder_type_equal(a->params.data[i].type, b->params.data[i].type)) return false;
        return true;
    }
    if (a->kind == TYPE_STRUCT || a->kind == TYPE_UNION || a->kind == TYPE_ENUM) return a->tag != NULL && b->tag != NULL && strcmp(a->tag, b->tag) == 0;
    return true;
}

bool cinder_type_compatible(const CinderType *a, const CinderType *b) {
    if (cinder_type_equal(a, b)) return true;
    if (a == NULL || b == NULL) return false;
    if ((a->kind == TYPE_INT || a->kind == TYPE_CHAR || a->kind == TYPE_SHORT || a->kind == TYPE_LONG || a->kind == TYPE_LLONG || a->kind == TYPE_BOOL) &&
        (b->kind == TYPE_INT || b->kind == TYPE_CHAR || b->kind == TYPE_SHORT || b->kind == TYPE_LONG || b->kind == TYPE_LLONG || b->kind == TYPE_BOOL)) return true;
    if (a->kind == TYPE_POINTER && b->kind == TYPE_POINTER) return cinder_type_compatible(a->base, b->base) || a->base->kind == TYPE_VOID || b->base->kind == TYPE_VOID;
    return false;
}

const char *cinder_type_name(const CinderType *type) {
    if (type == NULL) return "<null type>";
    switch (type->kind) {
        case TYPE_ERROR: return "<error>";
        case TYPE_VOID: return "void";
        case TYPE_BOOL: return "_Bool";
        case TYPE_CHAR: return type->is_unsigned ? "unsigned char" : "char";
        case TYPE_SHORT: return type->is_unsigned ? "unsigned short" : "short";
        case TYPE_INT: return type->is_unsigned ? "unsigned int" : "int";
        case TYPE_LONG: return type->is_unsigned ? "unsigned long" : "long";
        case TYPE_LLONG: return type->is_unsigned ? "unsigned long long" : "long long";
        case TYPE_FLOAT: return "float";
        case TYPE_DOUBLE: return "double";
        case TYPE_POINTER: return "pointer";
        case TYPE_ARRAY: return "array";
        case TYPE_FUNCTION: return "function";
        case TYPE_STRUCT: return "struct";
        case TYPE_UNION: return "union";
        case TYPE_ENUM: return "enum";
    }
    return "<unknown type>";
}
