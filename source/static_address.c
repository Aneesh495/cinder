#include "cinder.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    CinderIRModule *module;
    CinderAst *ast;
    CinderDiagnostics *diags;
} AddressContext;

static bool value_address(AddressContext *context, const CinderExpr *expr, CinderIRAddress *address, unsigned depth);

static bool shift_address(CinderIRAddress *address, int64_t index, const CinderType *index_type, size_t stride, bool subtract) {
    if (address->symbol == NULL) return false;
    if (index_type->is_unsigned && index < 0) return false;
    bool negative = (index < 0) != subtract;
    uint64_t magnitude = index < 0 ? UINT64_C(0) - (uint64_t)index : (uint64_t)index;
    if (stride == 0U || magnitude > (uint64_t)INT64_MAX / stride) return false;
    int64_t distance = (int64_t)(magnitude * stride);
    if ((negative && address->addend < INT64_MIN + distance) || (!negative && address->addend > INT64_MAX - distance)) return false;
    address->addend = negative ? address->addend - distance : address->addend + distance;
    return true;
}

static bool named_address(AddressContext *context, const CinderExpr *expr, CinderIRAddress *address) {
    for (size_t d = 0U; d < context->ast->declarations.len; ++d) {
        const CinderDecl *decl = context->ast->declarations.data[d];
        if (decl->kind == DECL_TYPEDEF || decl->name == NULL || strcmp(decl->name, expr->as.name) != 0) continue;
        if (decl->canonical != NULL) decl = decl->canonical;
        address->symbol = cinder_strndup(expr->as.name, strlen(expr->as.name));
        address->target_type = decl->type; address->function = decl->kind == DECL_FUNCTION;
        address->domain_end = address->function ? 1U : decl->type->size;
        return true;
    }
    return false;
}

static bool string_address(AddressContext *context, const CinderExpr *expr, CinderIRAddress *address) {
    CinderIRGlobal literal; memset(&literal, 0, sizeof(literal));
    char name[64]; int count = snprintf(name, sizeof(name), ".LCS.%zu", context->module->globals.len);
    if (count < 0 || (size_t)count >= sizeof(name) || expr->type->size > 64U * 1024U * 1024U) return false;
    literal.name = cinder_strndup(name, (size_t)count); literal.type = expr->type;
    literal.byte_count = expr->literal_length + 1U;
    literal.bytes = cinder_arena_strndup(&context->module->arena, expr->as.string, literal.byte_count);
    literal.read_only = true; literal.has_initializer = true; literal.loc = expr->loc;
    cinder_vec_push((CinderVec *)&context->module->globals, &literal);
    address->symbol = cinder_strndup(name, (size_t)count); address->target_type = expr->type;
    address->domain_end = expr->type->size; return true;
}

static bool lvalue_address(AddressContext *context, const CinderExpr *expr, CinderIRAddress *address, unsigned depth) {
    if (depth >= 256U) return false;
    if (expr->kind == EX_GENERIC) return expr->as.generic.selected < expr->as.generic.associations.len && lvalue_address(context, expr->as.generic.associations.data[expr->as.generic.selected].value, address, depth + 1U);
    if (expr->kind == EX_NAME) return named_address(context, expr, address);
    if (expr->kind == EX_STRING) return string_address(context, expr, address);
    if (expr->kind == EX_COMPOUND_LITERAL) {
        CinderDecl *decl = expr->as.compound_literal;
        if (!decl->is_static || !cinder_lower_static_object(context->module, context->ast, decl, context->diags)) return false;
        address->symbol = cinder_strndup(decl->name, strlen(decl->name));
        address->target_type = decl->type; address->domain_end = decl->type->size; return true;
    }
    if (expr->kind == EX_UNARY && expr->as.unary.op == '*') return value_address(context, expr->as.unary.value, address, depth + 1U);
    if (expr->kind == EX_INDEX) {
        int64_t index; CinderType *type;
        return value_address(context, expr->as.index.base, address, depth + 1U) &&
            cinder_constant_integer(context->ast, expr->as.index.index, &index, &type) &&
            shift_address(address, index, type, expr->type->size, false);
    }
    if (expr->kind == EX_MEMBER) {
        const CinderExpr *base = expr->as.member.base;
        CinderType *aggregate = expr->as.member.arrow ? base->type->base : base->type;
        if (expr->as.member.field >= aggregate->fields.len) return false;
        bool valid = expr->as.member.arrow ? value_address(context, base, address, depth + 1U) : lvalue_address(context, base, address, depth + 1U);
        const CinderField *field = &aggregate->fields.data[expr->as.member.field];
        if (!valid || address->symbol == NULL || field->bit_width != 0U || address->addend < 0 || field->offset > (uint64_t)INT64_MAX - (uint64_t)address->addend) return false;
        address->addend += (int64_t)field->offset;
        address->domain_begin = (size_t)address->addend;
        if (field->type->size > SIZE_MAX - address->domain_begin) return false;
        address->domain_end = address->domain_begin + field->type->size; return true;
    }
    return false;
}

static bool value_address(AddressContext *context, const CinderExpr *expr, CinderIRAddress *address, unsigned depth) {
    if (expr == NULL || depth >= 256U) return false;
    if (expr->kind == EX_GENERIC) return expr->as.generic.selected < expr->as.generic.associations.len && value_address(context, expr->as.generic.associations.data[expr->as.generic.selected].value, address, depth + 1U);
    if (expr->kind == EX_CAST) {
        int64_t zero; CinderType *type;
        if (cinder_constant_integer(context->ast, expr->as.cast.value, &zero, &type) && zero == 0) return true;
        return value_address(context, expr->as.cast.value, address, depth + 1U);
    }
    if (expr->kind == EX_DECAY) return lvalue_address(context, expr->as.unary.value, address, depth + 1U);
    if (expr->kind == EX_NAME && expr->type->kind == TYPE_FUNCTION) return named_address(context, expr, address);
    if (expr->kind == EX_UNARY && expr->as.unary.op == '&') return lvalue_address(context, expr->as.unary.value, address, depth + 1U);
    if (expr->kind == EX_BINARY && (expr->as.binary.op == '+' || expr->as.binary.op == '-')) {
        const CinderExpr *base = expr->as.binary.left, *index = expr->as.binary.right;
        int64_t value; CinderType *type;
        return base->type->kind == TYPE_POINTER && value_address(context, base, address, depth + 1U) &&
            cinder_constant_integer(context->ast, index, &value, &type) &&
            shift_address(address, value, type, base->type->base->size, expr->as.binary.op == '-');
    }
    if (expr->kind == EX_CONDITIONAL) {
        int64_t condition; CinderType *type;
        if (!cinder_constant_integer(context->ast, expr->as.conditional.condition, &condition, &type)) return false;
        return value_address(context, condition == 0 ? expr->as.conditional.no : expr->as.conditional.yes, address, depth + 1U);
    }
    int64_t zero; CinderType *type;
    return cinder_constant_integer(context->ast, expr, &zero, &type) && zero == 0;
}

bool cinder_static_address(CinderIRModule *module, CinderAst *ast, const CinderExpr *expr, CinderIRAddress *address, CinderDiagnostics *diags) {
    memset(address, 0, sizeof(*address)); address->pointer_type = expr->type;
    AddressContext context = {module, ast, diags};
    if (value_address(&context, expr, address, 0U)) return true;
    free(address->symbol); address->symbol = NULL; return false;
}
