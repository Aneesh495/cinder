#include "cinder.h"

#include <limits.h>
#include <math.h>

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

static CinderType *expression_type(CinderAst *ast, const CinderExpr *expr, unsigned depth) {
    if (expr == NULL || depth >= 256U) return NULL;
    if (expr->type != NULL) return expr->type;
    if (expr->kind == EX_CAST) return expr->as.cast.cast_type;
    if (expr->kind == EX_CHAR) return ast->types->int_type;
    if (expr->kind == EX_SIZEOF || expr->kind == EX_ALIGNOF) return ast->types->ulong_type;
    if (expr->kind == EX_UNARY) {
        CinderType *type = expression_type(ast, expr->as.unary.value, depth + 1U);
        if (expr->as.unary.op == '!') return ast->types->int_type;
        if (expr->as.unary.op == '*' && type != NULL && type->kind == TYPE_POINTER) return type->base;
        return integer_type(type) ? cinder_integer_promote(ast->types, type) : NULL;
    }
    if (expr->kind == EX_BINARY || expr->kind == EX_CONDITIONAL) {
        CinderType *left = expression_type(ast, expr->kind == EX_BINARY ? expr->as.binary.left : expr->as.conditional.yes, depth + 1U);
        CinderType *right = expression_type(ast, expr->kind == EX_BINARY ? expr->as.binary.right : expr->as.conditional.no, depth + 1U);
        if (!integer_type(left) || !integer_type(right)) return NULL;
        if (expr->kind == EX_BINARY) {
            int op = expr->as.binary.op;
            if (op == TOK_SHL || op == TOK_SHR) return cinder_integer_promote(ast->types, left);
            if (op == TOK_EQEQ || op == TOK_NEQ || op == TOK_LE || op == TOK_GE || op == '<' || op == '>' || op == TOK_ANDAND || op == TOK_OROR) return ast->types->int_type;
        }
        return cinder_arithmetic_type(ast->types, left, right);
    }
    if (expr->kind == EX_CALL) {
        CinderType *type = expression_type(ast, expr->as.call.callee, depth + 1U);
        if (type != NULL && type->kind == TYPE_POINTER) type = type->base;
        return type != NULL && type->kind == TYPE_FUNCTION ? type->return_type : NULL;
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
    if (expr->kind == EX_INT || expr->kind == EX_CHAR) {
        result->type = expr->type == NULL ? ast->types->int_type : expr->type;
        if (!integer_type(result->type)) return false;
        result->bits = normalize((uint64_t)expr->as.integer, result->type); return true;
    }
    if (expr->kind == EX_CAST) return convert_constant(ast, expr, result, depth);
    if (expr->kind == EX_BINARY) return binary(ast, expr, result, depth);
    if (expr->kind == EX_CONDITIONAL) {
        IntegerConstant condition;
        CinderType *type = expression_type(ast, expr, depth + 1U);
        if (!integer_type(type) || !evaluate(ast, expr->as.conditional.condition, &condition, depth + 1U) || !evaluate(ast, condition.bits != 0U ? expr->as.conditional.yes : expr->as.conditional.no, result, depth + 1U)) return false;
        result->bits = normalize(result->bits, type); result->type = type; return true;
    }
    if (expr->kind == EX_SIZEOF || expr->kind == EX_ALIGNOF) {
        CinderType *type = expr->queried_type != NULL ? expr->queried_type : expression_type(ast, expr->as.unary.value, depth + 1U);
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
