#include "cinder.h"

#include <limits.h>
#include <math.h>
#include <string.h>

typedef struct { uint64_t bits; CinderType *type; } IntegerConstant;

static bool integer_type(const CinderType *type) {
    return type != NULL && ((type->kind >= TYPE_BOOL && type->kind <= TYPE_LLONG) || type->kind == TYPE_ENUM);
}

static uint64_t mask_for(const CinderType *type) {
    unsigned width = (unsigned)(type->size * 8U);
    return width == 64U ? UINT64_MAX : (UINT64_C(1) << width) - 1U;
}

static int64_t signed_bits(uint64_t bits) {
    return bits <= (uint64_t)INT64_MAX ? (int64_t)bits : -1 - (int64_t)~bits;
}

static uint64_t normalize(uint64_t bits, const CinderType *type) {
    if (type->kind == TYPE_BOOL) return bits != 0U;
    uint64_t mask = mask_for(type); bits &= mask;
    if (!type->is_unsigned && (bits & (mask ^ (mask >> 1U))) != 0U) bits |= ~mask;
    return bits;
}

static int64_t minimum(const CinderType *type) {
    unsigned width = (unsigned)(type->size * 8U);
    return width == 64U ? INT64_MIN : -(INT64_C(1) << (width - 1U));
}

static int64_t maximum(const CinderType *type) {
    return type->size == 8U ? INT64_MAX : (INT64_C(1) << (type->size * 8U - 1U)) - 1;
}

static bool evaluate(CinderAst *ast, const CinderExpr *expr, IntegerConstant *result, unsigned depth);

static bool evaluate_offset(CinderAst *ast, const CinderExpr *expr, size_t *result, unsigned depth) {
    if (expr == NULL || expr->kind != EX_OFFSETOF || depth >= 256U || expr->as.offset.path.len == 0U || expr->as.offset.path.len > 256U) return false;
    const CinderType *type = expr->as.offset.object_type;
    if (type == NULL || (type->kind != TYPE_STRUCT && type->kind != TYPE_UNION) || !type->complete || type->completion_index > expr->parse_index) return false;
    size_t offset = 0U;
    for (size_t i = 0U; i < expr->as.offset.path.len; ++i) {
        const CinderInitDesignator *part = &expr->as.offset.path.data[i];
        if (part->member != NULL) {
            if ((type->kind != TYPE_STRUCT && type->kind != TYPE_UNION) || !type->complete || type->completion_index > expr->parse_index) return false;
            size_t path[64]; size_t length = cinder_type_member_path(type, part->member, path, CINDER_ARRAY_LEN(path));
            if (length == 0U || length == SIZE_MAX) return false;
            for (size_t p = 0U; p < length; ++p) {
                const CinderField *field = &type->fields.data[path[p]];
                if (field->bit_width != 0U || field->offset > SIZE_MAX - offset) return false;
                offset += field->offset; type = field->type;
            }
        } else {
            IntegerConstant index;
            if (type->kind != TYPE_ARRAY || !type->complete || type->base == NULL || !type->base->complete || type->base->size == 0U || !evaluate(ast, part->index, &index, depth + 1U)) return false;
            uint64_t length = (uint64_t)type->array_len;
            if (index.bits > length || (index.bits == length && i + 1U != expr->as.offset.path.len) || index.bits > (SIZE_MAX - offset) / type->base->size) return false;
            offset += (size_t)index.bits * type->base->size; type = type->base;
        }
    }
    *result = offset; return true;
}

bool cinder_offsetof_value(CinderAst *ast, const CinderExpr *expr, size_t *value) {
    return evaluate_offset(ast, expr, value, 0U);
}

static CinderType *value_type(CinderAst *ast, CinderType *type) {
    if (type != NULL && (type->kind == TYPE_ARRAY || type->kind == TYPE_FUNCTION)) return cinder_type_pointer(ast->types, type->kind == TYPE_ARRAY ? type->base : type);
    return type;
}

size_t cinder_generic_selection(const CinderType *control, const CinderExpr *expr) {
    if (control == NULL || expr == NULL || expr->kind != EX_GENERIC) return SIZE_MAX;
    CinderType converted = *control; converted.qualifiers = 0U;
    size_t selected = SIZE_MAX, fallback = SIZE_MAX;
    for (size_t i = 0U; i < expr->as.generic.associations.len; ++i) {
        const CinderType *type = expr->as.generic.associations.data[i].type;
        if (type == NULL) { if (fallback != SIZE_MAX) return SIZE_MAX; fallback = i; }
        else if (cinder_type_compatible(&converted, type)) { if (selected != SIZE_MAX) return SIZE_MAX; selected = i; }
    }
    return selected == SIZE_MAX ? fallback : selected;
}

CinderType *cinder_expression_type(CinderAst *ast, const CinderExpr *expr, unsigned depth) {
    if (expr == NULL || depth >= 256U) return NULL;
    if (expr->kind == EX_COMPOUND_LITERAL && !cinder_infer_initializer_shape(ast, expr->as.compound_literal, depth + 1U)) return NULL;
    if (expr->type != NULL) return expr->type;
    if (expr->kind == EX_GENERIC) {
        CinderType *control = value_type(ast, cinder_expression_type(ast, expr->as.generic.control, depth + 1U));
        size_t selected = cinder_generic_selection(control, expr);
        return selected < expr->as.generic.associations.len ? cinder_expression_type(ast, expr->as.generic.associations.data[selected].value, depth + 1U) : NULL;
    }
    if (expr->kind == EX_CAST) return expr->as.cast.cast_type;
    if (expr->kind == EX_CHAR) return ast->types->int_type;
    if (expr->kind == EX_SIZEOF || expr->kind == EX_ALIGNOF || expr->kind == EX_OFFSETOF) return ast->types->ulong_type;
    if (expr->kind == EX_UNARY) {
        CinderType *type = cinder_expression_type(ast, expr->as.unary.value, depth + 1U);
        if (expr->as.unary.op == '!') return ast->types->int_type;
        if (expr->as.unary.op == '&' && type != NULL) return cinder_type_pointer(ast->types, type);
        type = value_type(ast, type);
        if (expr->as.unary.op == '*' && type != NULL && type->kind == TYPE_POINTER) return type->base;
        if (expr->as.unary.op == TOK_PLUSPLUS || expr->as.unary.op == TOK_MINUSMINUS) return type;
        if ((expr->as.unary.op == '+' || expr->as.unary.op == '-') && type != NULL && (type->kind == TYPE_FLOAT || type->kind == TYPE_DOUBLE)) return type;
        return integer_type(type) ? cinder_integer_promote(ast->types, type) : NULL;
    }
    if (expr->kind == EX_BINARY || expr->kind == EX_CONDITIONAL) {
        CinderType *left = value_type(ast, cinder_expression_type(ast, expr->kind == EX_BINARY ? expr->as.binary.left : expr->as.conditional.yes, depth + 1U));
        CinderType *right = value_type(ast, cinder_expression_type(ast, expr->kind == EX_BINARY ? expr->as.binary.right : expr->as.conditional.no, depth + 1U));
        if (expr->kind == EX_BINARY && expr->as.binary.op == ',') return right;
        if (expr->kind == EX_CONDITIONAL && left != NULL && right != NULL && cinder_type_compatible(left, right)) return left;
        if (expr->kind == EX_CONDITIONAL && left != NULL && right != NULL && (left->kind == TYPE_STRUCT || left->kind == TYPE_UNION)) {
            CinderType a = *left, b = *right; a.qualifiers = 0U; b.qualifiers = 0U;
            if (cinder_type_compatible(&a, &b)) return left;
        }
        if (expr->kind == EX_CONDITIONAL && left != NULL && right != NULL && (left->kind == TYPE_POINTER || right->kind == TYPE_POINTER)) return left->kind == TYPE_POINTER ? left : right;
        if (expr->kind == EX_BINARY) {
            int op = expr->as.binary.op;
            if ((op == TOK_SHL || op == TOK_SHR) && integer_type(left) && integer_type(right)) return cinder_integer_promote(ast->types, left);
            if (op == TOK_EQEQ || op == TOK_NEQ || op == TOK_LE || op == TOK_GE || op == '<' || op == '>' || op == TOK_ANDAND || op == TOK_OROR) return ast->types->int_type;
            if ((op == '+' || op == '-') && left != NULL && left->kind == TYPE_POINTER && integer_type(right)) return left;
            if (op == '+' && right != NULL && right->kind == TYPE_POINTER && integer_type(left)) return right;
            if (op == '-' && left != NULL && right != NULL && left->kind == TYPE_POINTER && right->kind == TYPE_POINTER) return ast->types->long_type;
        }
        bool a = integer_type(left) || (left != NULL && (left->kind == TYPE_FLOAT || left->kind == TYPE_DOUBLE));
        bool b = integer_type(right) || (right != NULL && (right->kind == TYPE_FLOAT || right->kind == TYPE_DOUBLE));
        if (!a || !b) return NULL;
        return cinder_arithmetic_type(ast->types, left, right);
    }
    if (expr->kind == EX_CALL) {
        CinderType *type = cinder_expression_type(ast, expr->as.call.callee, depth + 1U);
        if (type != NULL && type->kind == TYPE_POINTER) type = type->base;
        return type != NULL && type->kind == TYPE_FUNCTION ? type->return_type : NULL;
    }
    if (expr->kind == EX_VA_ARG) return expr->as.va_arg.type;
    if (expr->kind == EX_ASSIGN) return cinder_expression_type(ast, expr->as.assign.target, depth + 1U);
    if (expr->kind == EX_INDEX) {
        CinderType *base = cinder_expression_type(ast, expr->as.index.base, depth + 1U);
        if (integer_type(base)) base = cinder_expression_type(ast, expr->as.index.index, depth + 1U);
        return base != NULL && (base->kind == TYPE_ARRAY || base->kind == TYPE_POINTER) ? base->base : NULL;
    }
    if (expr->kind == EX_MEMBER) {
        CinderType *base = cinder_expression_type(ast, expr->as.member.base, depth + 1U);
        if (expr->as.member.arrow) base = value_type(ast, base);
        if (base != NULL && expr->as.member.arrow && base->kind == TYPE_POINTER) base = base->base;
        if (base == NULL || (base->kind != TYPE_STRUCT && base->kind != TYPE_UNION)) return NULL;
        size_t path[64]; size_t length = cinder_type_member_path(base, expr->as.member.name, path, CINDER_ARRAY_LEN(path));
        if (length == 0U || length == SIZE_MAX) return NULL;
        for (size_t p = 0U; p < length; ++p) {
            CinderType *child = base->fields.data[path[p]].type;
            base = cinder_type_qualified(ast->types, child, child->qualifiers | base->qualifiers);
        }
        return base;
    }
    return NULL;
}

static bool convert_constant(CinderAst *ast, const CinderExpr *expr, IntegerConstant *result, unsigned depth) {
    CinderType *type = expr->as.cast.cast_type;
    if (!integer_type(type)) return false;
    if (expr->as.cast.value->kind == EX_FLOAT) {
        double value = expr->as.cast.value->as.floating;
        if (type->kind == TYPE_BOOL) { *result = (IntegerConstant){value != 0.0, type}; return true; }
        double bound = type->size == 8U ? 0x1p64 : (double)(UINT64_C(1) << (type->size * 8U));
        if (!isfinite(value)) return false;
        if (type->is_unsigned) {
            if (value <= -1.0 || value >= bound) return false;
            result->bits = normalize((uint64_t)value, type);
        } else {
            bound /= 2.0;
            if ((type->size == 8U ? value < -bound : value <= -bound - 1.0) || value >= bound) return false;
            result->bits = normalize((uint64_t)(int64_t)value, type);
        }
        result->type = type; return true;
    }
    IntegerConstant input;
    if (!evaluate(ast, expr->as.cast.value, &input, depth + 1U)) return false;
    *result = (IntegerConstant){normalize(input.bits, type), type}; return true;
}

static bool binary(CinderAst *ast, const CinderExpr *expr, IntegerConstant *result, unsigned depth) {
    IntegerConstant left, right;
    int op = expr->as.binary.op;
    if (!evaluate(ast, expr->as.binary.left, &left, depth + 1U)) return false;
    if ((op == TOK_ANDAND && left.bits == 0U) || (op == TOK_OROR && left.bits != 0U)) {
        *result = (IntegerConstant){op == TOK_OROR, ast->types->int_type}; return true;
    }
    if (!evaluate(ast, expr->as.binary.right, &right, depth + 1U)) return false;
    if (op == TOK_ANDAND || op == TOK_OROR) { *result = (IntegerConstant){op == TOK_ANDAND ? left.bits != 0U && right.bits != 0U : left.bits != 0U || right.bits != 0U, ast->types->int_type}; return true; }
    bool shift = op == TOK_SHL || op == TOK_SHR;
    CinderType *type = shift ? cinder_integer_promote(ast->types, left.type) : cinder_arithmetic_type(ast->types, left.type, right.type);
    uint64_t a = normalize(left.bits, type), b = shift ? normalize(right.bits, cinder_integer_promote(ast->types, right.type)) : normalize(right.bits, type);
    int64_t x = signed_bits(a), y = signed_bits(b), lo = minimum(type), hi = maximum(type);
    uint64_t value = 0U; bool comparison = false;
    switch (op) {
        case '+':
            if (!type->is_unsigned && ((y > 0 && x > hi - y) || (y < 0 && x < lo - y))) return false;
            value = a + b; break;
        case '-':
            if (!type->is_unsigned && ((y < 0 && x > hi + y) || (y > 0 && x < lo + y))) return false;
            value = a - b; break;
        case '*': {
            if (!type->is_unsigned) {
                bool negative = (x < 0) != (y < 0);
                uint64_t magnitude_a = x < 0 ? UINT64_C(0) - a : a, magnitude_b = y < 0 ? UINT64_C(0) - b : b;
                uint64_t bound = negative ? UINT64_C(0) - (uint64_t)lo : (uint64_t)hi;
                if (magnitude_b != 0U && magnitude_a > bound / magnitude_b) return false;
            }
            value = a * b; break;
        }
        case '/': case '%':
            if (b == 0U || (!type->is_unsigned && x == lo && y == -1)) return false;
            value = type->is_unsigned ? (op == '/' ? a / b : a % b) : (uint64_t)(op == '/' ? x / y : x % y); break;
        case '&': value = a & b; break;
        case '|': value = a | b; break;
        case '^': value = a ^ b; break;
        case TOK_SHL:
            if (b >= type->size * 8U || (!type->is_unsigned && (x < 0 || a > (uint64_t)hi >> (unsigned)b))) return false;
            value = a << (unsigned)b; break;
        case TOK_SHR:
            if (b >= type->size * 8U) return false;
            value = type->is_unsigned ? a >> (unsigned)b : (uint64_t)(x >> (unsigned)b); break;
        case TOK_EQEQ: value = a == b; comparison = true; break;
        case TOK_NEQ: value = a != b; comparison = true; break;
        case '<': value = type->is_unsigned ? a < b : x < y; comparison = true; break;
        case '>': value = type->is_unsigned ? a > b : x > y; comparison = true; break;
        case TOK_LE: value = type->is_unsigned ? a <= b : x <= y; comparison = true; break;
        case TOK_GE: value = type->is_unsigned ? a >= b : x >= y; comparison = true; break;
        default: return false;
    }
    result->type = comparison ? ast->types->int_type : type;
    result->bits = normalize(value, result->type); return true;
}

static bool evaluate(CinderAst *ast, const CinderExpr *expr, IntegerConstant *result, unsigned depth) {
    if (expr == NULL || depth >= 256U) return false;
    if (expr->kind == EX_GENERIC) {
        CinderType *control = value_type(ast, cinder_expression_type(ast, expr->as.generic.control, depth + 1U));
        size_t selected = cinder_generic_selection(control, expr);
        return selected < expr->as.generic.associations.len && evaluate(ast, expr->as.generic.associations.data[selected].value, result, depth + 1U);
    }
    if (expr->kind == EX_INT || expr->kind == EX_CHAR) {
        result->type = expr->type == NULL ? ast->types->int_type : expr->type;
        if (!integer_type(result->type)) return false;
        result->bits = normalize((uint64_t)expr->as.integer, result->type); return true;
    }
    if (expr->kind == EX_CAST) return convert_constant(ast, expr, result, depth);
    if (expr->kind == EX_BINARY) return binary(ast, expr, result, depth);
    if (expr->kind == EX_CONDITIONAL) {
        IntegerConstant condition;
        CinderType *type = cinder_expression_type(ast, expr, depth + 1U);
        if (!integer_type(type) || !evaluate(ast, expr->as.conditional.condition, &condition, depth + 1U) || !evaluate(ast, condition.bits != 0U ? expr->as.conditional.yes : expr->as.conditional.no, result, depth + 1U)) return false;
        result->bits = normalize(result->bits, type); result->type = type; return true;
    }
    if (expr->kind == EX_OFFSETOF) {
        size_t value;
        if (!evaluate_offset(ast, expr, &value, depth + 1U)) return false;
        *result = (IntegerConstant){(uint64_t)value, ast->types->ulong_type}; return true;
    }
    if (expr->kind == EX_SIZEOF || expr->kind == EX_ALIGNOF) {
        CinderType *type = expr->queried_type != NULL ? expr->queried_type : cinder_expression_type(ast, expr->as.unary.value, depth + 1U);
        if (type == NULL || !type->complete || type->completion_index > expr->parse_index || type->kind == TYPE_VOID || type->kind == TYPE_FUNCTION) return false;
        *result = (IntegerConstant){expr->kind == EX_SIZEOF ? type->size : type->align, ast->types->ulong_type}; return true;
    }
    if (expr->kind == EX_UNARY) {
        IntegerConstant operand;
        if (!evaluate(ast, expr->as.unary.value, &operand, depth + 1U)) return false;
        CinderType *type = cinder_integer_promote(ast->types, operand.type); uint64_t value = normalize(operand.bits, type);
        if (expr->as.unary.op == '!') { *result = (IntegerConstant){value == 0U, ast->types->int_type}; return true; }
        if (expr->as.unary.op == '-') {
            if (!type->is_unsigned && signed_bits(value) == minimum(type)) return false;
            value = UINT64_C(0) - value;
        } else if (expr->as.unary.op == '~') value = ~value;
        else if (expr->as.unary.op != '+') return false;
        *result = (IntegerConstant){normalize(value, type), type}; return true;
    }
    return false;
}

bool cinder_constant_integer(CinderAst *ast, const CinderExpr *expr, int64_t *value, CinderType **type) {
    IntegerConstant result;
    if (!evaluate(ast, expr, &result, 0U)) return false;
    *value = signed_bits(result.bits); *type = result.type; return true;
}

typedef struct { CinderType *type; uint64_t bits; double floating; bool fp; } ScalarConstant;

static bool scalar_truth(const ScalarConstant *value) { return value->fp ? value->floating != 0.0 : value->bits != 0U; }

static bool scalar_conversion(ScalarConstant *value, CinderType *target) {
    bool fp = target->kind == TYPE_FLOAT || target->kind == TYPE_DOUBLE;
    if (!fp && !integer_type(target)) return false;
    if (fp) {
        if (!value->fp) {
            if (target->kind == TYPE_FLOAT) value->floating = value->type->is_unsigned ? (double)(float)value->bits : (double)(float)signed_bits(value->bits);
            else value->floating = value->type->is_unsigned ? (double)value->bits : (double)signed_bits(value->bits);
        } else if (target->kind == TYPE_FLOAT) value->floating = (double)(float)value->floating;
    } else if (target->kind == TYPE_BOOL) value->bits = scalar_truth(value);
    else if (value->fp) {
        double number = value->floating;
        if (!isfinite(number)) return false;
        double bound = target->size == 8U ? 0x1p64 : (double)(UINT64_C(1) << (target->size * 8U));
        if (target->is_unsigned) {
            if (number <= -1.0 || number >= bound) return false;
            value->bits = normalize((uint64_t)number, target);
        } else {
            bound /= 2.0;
            if ((target->size == 8U ? number < -bound : number <= -bound - 1.0) || number >= bound) return false;
            value->bits = normalize((uint64_t)(int64_t)number, target);
        }
    } else value->bits = normalize(value->bits, target);
    value->type = target; value->fp = fp; return true;
}

static bool scalar_evaluate(CinderAst *ast, const CinderExpr *expr, ScalarConstant *value, unsigned depth) {
    if (expr == NULL || depth >= 256U) return false;
    if (expr->kind == EX_GENERIC) {
        size_t selected = expr->as.generic.selected;
        return selected < expr->as.generic.associations.len && scalar_evaluate(ast, expr->as.generic.associations.data[selected].value, value, depth + 1U);
    }
    int64_t integer; CinderType *type;
    if (cinder_constant_integer(ast, expr, &integer, &type)) { *value = (ScalarConstant){type, (uint64_t)integer, 0.0, false}; return true; }
    if (expr->kind == EX_FLOAT) { *value = (ScalarConstant){expr->type, 0U, expr->as.floating, true}; return scalar_conversion(value, expr->type); }
    if (expr->kind == EX_CAST) return scalar_evaluate(ast, expr->as.cast.value, value, depth + 1U) && scalar_conversion(value, expr->as.cast.cast_type);
    if (expr->kind == EX_CONDITIONAL) {
        ScalarConstant condition;
        return scalar_evaluate(ast, expr->as.conditional.condition, &condition, depth + 1U) &&
            scalar_evaluate(ast, scalar_truth(&condition) ? expr->as.conditional.yes : expr->as.conditional.no, value, depth + 1U) && scalar_conversion(value, expr->type);
    }
    if (expr->kind == EX_UNARY) {
        if (!scalar_evaluate(ast, expr->as.unary.value, value, depth + 1U)) return false;
        int op = expr->as.unary.op;
        if (op == '!') { *value = (ScalarConstant){ast->types->int_type, !scalar_truth(value), 0.0, false}; return true; }
        if (value->fp) {
            if (op == '-') value->floating = -value->floating;
            else if (op != '+') return false;
            return scalar_conversion(value, expr->type);
        }
        CinderExpr operand; memset(&operand, 0, sizeof(operand)); operand.kind = EX_INT; operand.type = value->type; operand.as.integer = signed_bits(value->bits);
        CinderExpr operation = *expr; operation.as.unary.value = &operand;
        if (!cinder_constant_integer(ast, &operation, &integer, &type)) return false;
        *value = (ScalarConstant){type, (uint64_t)integer, 0.0, false}; return true;
    }
    if (expr->kind != EX_BINARY) return false;
    ScalarConstant left, right;
    if (!scalar_evaluate(ast, expr->as.binary.left, &left, depth + 1U)) return false;
    int op = expr->as.binary.op;
    if ((op == TOK_ANDAND && !scalar_truth(&left)) || (op == TOK_OROR && scalar_truth(&left))) { *value = (ScalarConstant){ast->types->int_type, op == TOK_OROR, 0.0, false}; return true; }
    if (!scalar_evaluate(ast, expr->as.binary.right, &right, depth + 1U)) return false;
    if (op == TOK_ANDAND || op == TOK_OROR) { *value = (ScalarConstant){ast->types->int_type, scalar_truth(&right), 0.0, false}; return true; }
    if (!left.fp && !right.fp) {
        CinderExpr a, b; memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
        a.kind = EX_INT; a.type = left.type; a.as.integer = signed_bits(left.bits);
        b.kind = EX_INT; b.type = right.type; b.as.integer = signed_bits(right.bits);
        CinderExpr operation = *expr; operation.as.binary.left = &a; operation.as.binary.right = &b;
        if (!cinder_constant_integer(ast, &operation, &integer, &type)) return false;
        *value = (ScalarConstant){type, (uint64_t)integer, 0.0, false}; return true;
    }
    CinderType *common = cinder_arithmetic_type(ast->types, left.type, right.type);
    if (!scalar_conversion(&left, common) || !scalar_conversion(&right, common)) return false;
    *value = (ScalarConstant){common, 0U, 0.0, true};
    switch (op) {
        case '+': value->floating = left.floating + right.floating; break;
        case '-': value->floating = left.floating - right.floating; break;
        case '*': value->floating = left.floating * right.floating; break;
        case '/': value->floating = left.floating / right.floating; break;
        case TOK_EQEQ: value->bits = left.floating == right.floating; value->fp = false; break;
        case TOK_NEQ: value->bits = left.floating != right.floating; value->fp = false; break;
        case '<': value->bits = left.floating < right.floating; value->fp = false; break;
        case '>': value->bits = left.floating > right.floating; value->fp = false; break;
        case TOK_LE: value->bits = left.floating <= right.floating; value->fp = false; break;
        case TOK_GE: value->bits = left.floating >= right.floating; value->fp = false; break;
        default: return false;
    }
    if (!value->fp) value->type = ast->types->int_type;
    return scalar_conversion(value, expr->type);
}

bool cinder_constant_scalar(CinderAst *ast, const CinderExpr *expr, CinderType *target, int64_t *integer, double *floating) {
    ScalarConstant value;
    if (!scalar_evaluate(ast, expr, &value, 0U) || !scalar_conversion(&value, target)) return false;
    *integer = value.fp ? 0 : signed_bits(value.bits); *floating = value.fp ? value.floating : 0.0; return true;
}
