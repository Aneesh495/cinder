#include "cinder.h"

#include <stdlib.h>
#include <string.h>

static void set_scalar(CinderType *type, size_t size, size_t align, bool is_unsigned) {
    type->size = size;
    type->align = align;
    type->complete = true;
    type->completion_index = 0U;
    type->is_unsigned = is_unsigned;
}

static CinderType *type_scalar(CinderTypeContext *types, CinderTypeKind kind, size_t size, size_t align, bool is_unsigned) {
    CinderType *type = cinder_type_new(types, kind);
    set_scalar(type, size, align, is_unsigned);
    return type;
}

void cinder_types_init(CinderTypeContext *types) {
    cinder_arena_init(&types->arena, 8192U);
    types->all_types.data = NULL; types->all_types.len = 0U; types->all_types.cap = 0U;
    types->error_type = cinder_type_new(types, TYPE_ERROR);
    types->error_type->complete = false;
    types->void_type = cinder_type_new(types, TYPE_VOID);
    types->void_type->complete = true;
    types->void_type->completion_index = 0U;
    types->void_type->size = 0U;
    types->void_type->align = 1U;
    types->bool_type = type_scalar(types, TYPE_BOOL, 1U, 1U, true);
    types->char_type = type_scalar(types, TYPE_CHAR, 1U, 1U, false);
    types->char_type->plain_char = true;
    types->schar_type = type_scalar(types, TYPE_CHAR, 1U, 1U, false);
    types->uchar_type = type_scalar(types, TYPE_CHAR, 1U, 1U, true);
    types->short_type = type_scalar(types, TYPE_SHORT, 2U, 2U, false);
    types->ushort_type = type_scalar(types, TYPE_SHORT, 2U, 2U, true);
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
    for (size_t i = 0U; i < types->all_types.len; ++i) {
        free(types->all_types.data[i]->params.data);
        free(types->all_types.data[i]->fields.data);
    }
    free(types->all_types.data);
    cinder_arena_destroy(&types->arena);
}

CinderType *cinder_type_new(CinderTypeContext *types, CinderTypeKind kind) {
    CinderType *type = cinder_arena_alloc(&types->arena, sizeof(*type), _Alignof(CinderType));
    memset(type, 0, sizeof(*type));
    type->kind = kind;
    type->identity = (uint32_t)types->all_types.len + 1U;
    type->align = 1U;
    type->complete = false;
    type->completion_index = SIZE_MAX;
    type->params.data = NULL;
    type->params.len = 0U;
    type->params.cap = 0U;
    type->fields.data = NULL;
    type->fields.len = 0U;
    type->fields.cap = 0U;
    cinder_vec_push((CinderVec *)&types->all_types, &type);
    return type;
}

CinderType *cinder_type_qualified(CinderTypeContext *types, CinderType *base, unsigned qualifiers) {
    if (base->kind == TYPE_ARRAY && qualifiers != 0U) {
        CinderType *element = cinder_type_qualified(types, base->base, qualifiers);
        CinderType *array = cinder_type_array(types, element, base->array_len);
        array->complete = base->complete; array->completion_index = base->completion_index; return array;
    }
    if (qualifiers == 0U || (base->qualifiers | qualifiers) == base->qualifiers) return base;
    CinderType *result = cinder_type_new(types, base->kind);
    *result = *base; result->qualifiers |= qualifiers;
    result->params.data = NULL; result->params.len = 0U; result->params.cap = 0U;
    result->fields.data = NULL; result->fields.len = 0U; result->fields.cap = 0U;
    for (size_t i = 0U; i < base->params.len; ++i) cinder_vec_push((CinderVec *)&result->params, &base->params.data[i]);
    for (size_t i = 0U; i < base->fields.len; ++i) cinder_vec_push((CinderVec *)&result->fields, &base->fields.data[i]);
    return result;
}

CinderType *cinder_integer_promote(CinderTypeContext *types, CinderType *type) {
    if (type->kind == TYPE_BOOL || type->kind == TYPE_CHAR || type->kind == TYPE_SHORT || type->kind == TYPE_ENUM) return types->int_type;
    if (type->kind == TYPE_INT) return type->is_unsigned ? types->uint_type : types->int_type;
    if (type->kind == TYPE_LONG) return type->is_unsigned ? types->ulong_type : types->long_type;
    if (type->kind == TYPE_LLONG) return type->is_unsigned ? types->ullong_type : types->llong_type;
    return type;
}

static unsigned integer_rank(const CinderType *type) {
    if (type->kind == TYPE_LLONG) return 3U;
    if (type->kind == TYPE_LONG) return 2U;
    return 1U;
}

static CinderType *unsigned_variant(CinderTypeContext *types, CinderType *type) {
    if (type->kind == TYPE_LLONG) return types->ullong_type;
    if (type->kind == TYPE_LONG) return types->ulong_type;
    return types->uint_type;
}

CinderType *cinder_arithmetic_type(CinderTypeContext *types, CinderType *left, CinderType *right) {
    if (left->kind == TYPE_DOUBLE || right->kind == TYPE_DOUBLE) return types->double_type;
    if (left->kind == TYPE_FLOAT || right->kind == TYPE_FLOAT) return types->float_type;
    left = cinder_integer_promote(types, left); right = cinder_integer_promote(types, right);
    if (left->is_unsigned == right->is_unsigned) return integer_rank(left) >= integer_rank(right) ? left : right;
    CinderType *unsig = left->is_unsigned ? left : right;
    CinderType *sign = left->is_unsigned ? right : left;
    if (integer_rank(unsig) >= integer_rank(sign)) return unsig;
    if (sign->size > unsig->size) return sign;
    return unsigned_variant(types, sign);
}

CinderType *cinder_type_pointer(CinderTypeContext *types, CinderType *base) {
    CinderType *type = cinder_type_new(types, TYPE_POINTER);
    type->base = base;
    type->size = 8U;
    type->align = 8U;
    type->complete = true;
    type->completion_index = 0U;
    return type;
}

CinderType *cinder_type_array(CinderTypeContext *types, CinderType *base, size_t length) {
    CinderType *type = cinder_type_new(types, TYPE_ARRAY);
    type->base = base;
    type->array_len = length;
    if (base->size != 0U && length > SIZE_MAX / base->size) { type->kind = TYPE_ERROR; return type; }
    type->size = base->size * length;
    type->align = base->align;
    type->complete = base->complete;
    type->completion_index = base->completion_index;
    return type;
}

CinderType *cinder_type_function(CinderTypeContext *types, CinderType *ret, const CinderParamVec *params) {
    CinderType *type = cinder_type_new(types, TYPE_FUNCTION);
    type->return_type = ret;
    type->complete = true;
    type->completion_index = 0U;
    for (size_t i = 0U; i < params->len; ++i) {
        CinderParam param = params->data[i];
        cinder_vec_push((CinderVec *)&type->params, &param);
    }
    return type;
}

bool cinder_type_equal(const CinderType *a, const CinderType *b) {
    if (a == b) return true;
    if (a == NULL || b == NULL || a->kind != b->kind || a->is_unsigned != b->is_unsigned || a->plain_char != b->plain_char || a->qualifiers != b->qualifiers) return false;
    if (a->kind == TYPE_POINTER) return cinder_type_equal(a->base, b->base);
    if (a->kind == TYPE_ARRAY) return a->array_len == b->array_len && cinder_type_equal(a->base, b->base);
    if (a->kind == TYPE_FUNCTION) {
        if (!cinder_type_equal(a->return_type, b->return_type) || a->variadic != b->variadic || a->params.len != b->params.len) return false;
        for (size_t i = 0U; i < a->params.len; ++i) {
            CinderType left = *a->params.data[i].type, right = *b->params.data[i].type;
            left.qualifiers = 0U; right.qualifiers = 0U;
            if (!cinder_type_equal(&left, &right)) return false;
        }
        return true;
    }
    if (a->kind == TYPE_STRUCT || a->kind == TYPE_UNION || a->kind == TYPE_ENUM) return a->identity == b->identity;
    return true;
}

bool cinder_type_compatible(const CinderType *a, const CinderType *b) {
    if (cinder_type_equal(a, b)) return true;
    /* The declared enumeration representation is signed 32-bit int.
     * Distinct enumeration tags remain distinct from one another. */
    if (a != NULL && b != NULL && ((a->kind == TYPE_ENUM && b->kind == TYPE_INT) || (b->kind == TYPE_ENUM && a->kind == TYPE_INT))) return a->qualifiers == b->qualifiers && a->size == 4U && b->size == 4U && !a->is_unsigned && !b->is_unsigned;
    if (a == NULL || b == NULL || a->kind != b->kind || a->qualifiers != b->qualifiers || a->is_unsigned != b->is_unsigned || a->plain_char != b->plain_char) return false;
    if (a->kind == TYPE_POINTER) return cinder_type_compatible(a->base, b->base);
    if (a->kind == TYPE_ARRAY) return (!a->complete || !b->complete || a->array_len == b->array_len) && cinder_type_compatible(a->base, b->base);
    if (a->kind == TYPE_FUNCTION) {
        if (a->variadic != b->variadic || a->params.len != b->params.len || !cinder_type_compatible(a->return_type, b->return_type)) return false;
        for (size_t p = 0U; p < a->params.len; ++p) {
            CinderType left = *a->params.data[p].type, right = *b->params.data[p].type;
            left.qualifiers = 0U; right.qualifiers = 0U;
            if (!cinder_type_compatible(&left, &right)) return false;
        }
        return true;
    }
    return false;
}

CinderType *cinder_type_composite(CinderTypeContext *types, CinderType *a, CinderType *b) {
    if (!cinder_type_compatible(a, b)) return types->error_type;
    if (cinder_type_equal(a, b)) return a;
    CinderType *result = a;
    if (a->kind == TYPE_POINTER) {
        CinderType *base = cinder_type_composite(types, a->base, b->base);
        if (base == a->base) return a;
        if (base == b->base) return b;
        result = cinder_type_pointer(types, base);
    } else if (a->kind == TYPE_ARRAY) {
        CinderType *base = cinder_type_composite(types, a->base, b->base);
        size_t length = a->complete ? a->array_len : b->array_len;
        if (base == a->base && (a->complete || !b->complete)) return a;
        if (base == b->base && b->complete) return b;
        result = cinder_type_array(types, base, length); result->complete = a->complete || b->complete;
    } else if (a->kind == TYPE_FUNCTION) {
        CinderParamVec params = {NULL, 0U, 0U};
        for (size_t p = 0U; p < a->params.len; ++p) {
            CinderParam parameter = a->params.data[p];
            CinderType left = *parameter.type, right = *b->params.data[p].type;
            if (left.qualifiers != right.qualifiers) parameter.type = parameter.type->qualifiers == 0U ? parameter.type : b->params.data[p].type;
            else parameter.type = cinder_type_composite(types, parameter.type, b->params.data[p].type);
            cinder_vec_push((CinderVec *)&params, &parameter);
        }
        result = cinder_type_function(types, cinder_type_composite(types, a->return_type, b->return_type), &params);
        result->variadic = a->variadic; free(params.data);
    }
    return cinder_type_qualified(types, result, a->qualifiers);
}

const char *cinder_type_name(const CinderType *type) {
    if (type == NULL) return "<null type>";
    switch (type->kind) {
        case TYPE_ERROR: return "<error>";
        case TYPE_VOID: return "void";
        case TYPE_BOOL: return "_Bool";
        case TYPE_CHAR: return type->is_unsigned ? "unsigned char" : type->plain_char ? "char" : "signed char";
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


bool cinder_object_alignment_valid(const CinderType *type, size_t alignment) {
    if (type == NULL || type->align == 0U || type->align > 16U || (type->align & (type->align - 1U)) != 0U) return false;
    return alignment == 0U || (alignment >= type->align && alignment <= 16U && (alignment & (alignment - 1U)) == 0U);
}

static size_t type_align_up(size_t value, size_t align) {
    if (align == 0U) return value;
    size_t mask = align - 1U;
    return value > SIZE_MAX - mask ? SIZE_MAX : (value + mask) & ~mask;
}

int cinder_type_layout_aggregate(CinderType *type, CinderDiagnostics *diags, CinderLoc loc) {
    if (type == NULL || (type->kind != TYPE_STRUCT && type->kind != TYPE_UNION)) return 1;
    size_t size = 0U;
    size_t align = 1U;
    for (size_t i = 0U; i < type->fields.len; ++i) {
        CinderField *field = &type->fields.data[i];
        if (field->type == NULL || !field->type->complete || field->type->kind == TYPE_VOID || field->type->kind == TYPE_FUNCTION) { cinder_diag(diags, CINDER_ERROR, loc, "field '%s' requires a complete object type", field->name == NULL ? "<unnamed>" : field->name); continue; }
        if (!cinder_object_alignment_valid(field->type, field->alignment)) { cinder_diag(diags, CINDER_ERROR, loc, "member alignment is invalid or weaker than its type"); continue; }
        size_t field_align = field->alignment == 0U ? field->type->align : field->alignment;
        if (field_align > align) align = field_align;
        if (type->kind == TYPE_UNION) field->offset = 0U;
        else {
            size = type_align_up(size, field_align);
            field->offset = size;
            if (field->type->size > SIZE_MAX - size) { cinder_diag(diags, CINDER_ERROR, loc, "aggregate layout size overflow"); size = SIZE_MAX; }
            else size += field->type->size;
        }
        if (type->kind == TYPE_UNION && field->type->size > size) size = field->type->size;
    }
    type->align = align;
    type->size = type_align_up(size, align);
    type->complete = diags->errors == 0U;
    return diags->errors == 0U ? 0 : 1;
}
