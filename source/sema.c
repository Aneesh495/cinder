#include "cinder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CinderSymbol *scope_add(CinderScope *scope, const char *name, CinderType *type, CinderDecl *decl, bool function) {
    CinderSymbol symbol;
    symbol.name = cinder_strndup(name, strlen(name)); symbol.type = type; symbol.decl = decl; symbol.is_function = function;
    cinder_vec_push((CinderVec *)&scope->symbols, &symbol);
    return &scope->symbols.data[scope->symbols.len - 1U];
}

CinderSymbol *cinder_scope_lookup(CinderScope *scope, const char *name) {
    for (CinderScope *current = scope; current != NULL; current = current->parent) {
        for (size_t i = current->symbols.len; i > 0U; --i) if (strcmp(current->symbols.data[i - 1U].name, name) == 0) return &current->symbols.data[i - 1U];
    }
    return NULL;
}

void cinder_sema_init(CinderSema *sema, CinderAst *ast, CinderTypeContext *types, CinderDiagnostics *diags) {
    sema->ast = ast; sema->types = types; sema->diags = diags;
    sema->globals.symbols.data = NULL; sema->globals.symbols.len = 0U; sema->globals.symbols.cap = 0U; sema->globals.parent = NULL;
}

void cinder_sema_destroy(CinderSema *sema) {
    for (size_t i = 0U; i < sema->globals.symbols.len; ++i) free(sema->globals.symbols.data[i].name);
    free(sema->globals.symbols.data);
}

static bool integer_type(const CinderType *type) {
    return type != NULL && (type->kind == TYPE_BOOL || type->kind == TYPE_CHAR || type->kind == TYPE_SHORT || type->kind == TYPE_INT || type->kind == TYPE_LONG || type->kind == TYPE_LLONG || type->kind == TYPE_ENUM);
}

static bool floating_type(const CinderType *type) { return type != NULL && (type->kind == TYPE_FLOAT || type->kind == TYPE_DOUBLE); }
static bool numeric_type(const CinderType *type) { return integer_type(type) || floating_type(type); }
static bool value_compatible(const CinderType *target, const CinderType *value) { return cinder_type_compatible(target, value) || (numeric_type(target) && numeric_type(value)); }

static CinderType *sema_expr(CinderSema *sema, CinderExpr *expr, CinderScope *scope);

static void sema_stmt(CinderSema *sema, CinderStmt *stmt, CinderScope *scope, CinderType *return_type, unsigned loop_depth) {
    if (stmt == NULL) return;
    switch (stmt->kind) {
        case ST_EXPR: if (stmt->as.expr != NULL) (void)sema_expr(sema, stmt->as.expr, scope); break;
        case ST_RETURN:
            if (stmt->as.ret.value == NULL) { if (return_type->kind != TYPE_VOID) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "non-void function must return a value"); }
            else if (!value_compatible(return_type, sema_expr(sema, stmt->as.ret.value, scope))) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "return expression is incompatible with %s", cinder_type_name(return_type));
            break;
        case ST_BLOCK: {
            CinderScope child = { {NULL, 0U, 0U}, scope };
            for (size_t i = 0U; i < stmt->as.block.items.len; ++i) {
                CinderStmt *item = stmt->as.block.items.data[i];
                if (item->kind == ST_DECL) {
                    CinderDecl *decl = item->as.decl;
                    if (cinder_scope_lookup(&child, decl->name) != NULL) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "redeclaration of '%s'", decl->name);
                    else scope_add(&child, decl->name, decl->type, decl, false);
                    if (decl->initializer != NULL && !(decl->initializer->kind == EX_STRING && decl->type->kind == TYPE_ARRAY && decl->type->base->kind == TYPE_CHAR) && !value_compatible(decl->type, sema_expr(sema, decl->initializer, &child))) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "initializer for '%s' has incompatible type", decl->name);
                } else sema_stmt(sema, item, &child, return_type, loop_depth);
            }
            for (size_t i = 0U; i < child.symbols.len; ++i) free(child.symbols.data[i].name);
            free(child.symbols.data);
            break;
        }
        case ST_IF:
            if (!numeric_type(sema_expr(sema, stmt->as.if_stmt.condition, scope))) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "if condition must be scalar");
            sema_stmt(sema, stmt->as.if_stmt.then_branch, scope, return_type, loop_depth); sema_stmt(sema, stmt->as.if_stmt.else_branch, scope, return_type, loop_depth); break;
        case ST_WHILE:
        case ST_DO:
            if (!numeric_type(sema_expr(sema, stmt->as.loop.condition, scope))) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "loop condition must be scalar");
            sema_stmt(sema, stmt->as.loop.body, scope, return_type, loop_depth + 1U); break;
        case ST_FOR: {
            CinderScope child = {{NULL, 0U, 0U}, scope};
            if (stmt->as.for_stmt.init != NULL) {
                if (stmt->as.for_stmt.init->kind == ST_DECL) {
                    CinderDecl *decl = stmt->as.for_stmt.init->as.decl;
                    scope_add(&child, decl->name, decl->type, decl, false);
                    if (decl->initializer != NULL) (void)sema_expr(sema, decl->initializer, &child);
                } else sema_stmt(sema, stmt->as.for_stmt.init, &child, return_type, loop_depth);
            }
            if (stmt->as.for_stmt.condition != NULL && !numeric_type(sema_expr(sema, stmt->as.for_stmt.condition, &child))) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "for condition must be scalar");
            if (stmt->as.for_stmt.step != NULL) (void)sema_expr(sema, stmt->as.for_stmt.step, &child);
            sema_stmt(sema, stmt->as.for_stmt.body, &child, return_type, loop_depth + 1U);
            for (size_t i = 0U; i < child.symbols.len; ++i) free(child.symbols.data[i].name);
            free(child.symbols.data);
            break;
        }
        case ST_BREAK:
        case ST_CONTINUE:
            if (loop_depth == 0U) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "break/continue is only valid inside a loop");
            break;
        case ST_EMPTY:
        case ST_DECL: break;
    }
}

static CinderType *sema_expr(CinderSema *sema, CinderExpr *expr, CinderScope *scope) {
    if (expr == NULL) return sema->types->void_type;
    switch (expr->kind) {
        case EX_INT: case EX_CHAR: if (expr->type == NULL) expr->type = sema->types->int_type; expr->is_lvalue = false; return expr->type;
        case EX_FLOAT: if (expr->type == NULL) expr->type = sema->types->double_type; expr->is_lvalue = false; return expr->type;
        case EX_STRING: expr->type = cinder_type_pointer(sema->types, sema->types->char_type); return expr->type;
        case EX_NAME: {
            CinderSymbol *symbol = cinder_scope_lookup(scope, expr->as.name);
            if (symbol == NULL) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "use of undeclared identifier '%s'", expr->as.name); expr->type = sema->types->error_type; return expr->type; }
            expr->type = symbol->type; expr->is_lvalue = !symbol->is_function; return expr->type;
        }
        case EX_BINARY: {
            CinderType *left = sema_expr(sema, expr->as.binary.left, scope); CinderType *right = sema_expr(sema, expr->as.binary.right, scope);
            if (expr->as.binary.op == ',') { expr->type = right; return right; }
            if (!numeric_type(left) || !numeric_type(right)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "operator requires arithmetic operands");
            int op = expr->as.binary.op;
            bool comparison = op == TOK_EQEQ || op == TOK_NEQ || op == '<' || op == '>' || op == TOK_LE || op == TOK_GE || op == TOK_ANDAND || op == TOK_OROR;
            expr->type = comparison ? sema->types->int_type : floating_type(left) || floating_type(right) ? sema->types->double_type : (cinder_type_compatible(left, right) ? left : sema->types->int_type); return expr->type;
        }
        case EX_UNARY: {
            CinderType *value = sema_expr(sema, expr->as.unary.value, scope);
            if ((expr->as.unary.op == '&' || expr->as.unary.op == TOK_PLUSPLUS || expr->as.unary.op == TOK_MINUSMINUS) && !expr->as.unary.value->is_lvalue) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "unary operator requires an assignable lvalue");
            if (expr->as.unary.op == '&') expr->type = cinder_type_pointer(sema->types, value); else if (expr->as.unary.op == '*') { if (value->kind != TYPE_POINTER) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "cannot dereference a non-pointer"); expr->type = value->kind == TYPE_POINTER ? value->base : sema->types->error_type; expr->is_lvalue = true; } else expr->type = expr->as.unary.op == '!' ? sema->types->int_type : value;
            return expr->type;
        }
        case EX_ASSIGN: {
            CinderType *target = sema_expr(sema, expr->as.assign.target, scope); CinderType *value = sema_expr(sema, expr->as.assign.value, scope);
            if (!expr->as.assign.target->is_lvalue) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "assignment target is not an lvalue");
            if (!value_compatible(target, value)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "assignment types are incompatible");
            expr->type = target; return target;
        }
        case EX_VA_ARG: (void)sema_expr(sema, expr->as.va_arg.list, scope); expr->type = expr->as.va_arg.type; return expr->type;
        case EX_CALL: {
            if (expr->as.call.callee->kind == EX_NAME && (strcmp(expr->as.call.callee->as.name, "va_start") == 0 || strcmp(expr->as.call.callee->as.name, "va_end") == 0)) { for (size_t i = 0U; i < expr->as.call.args.len; ++i) (void)sema_expr(sema, expr->as.call.args.data[i], scope); expr->type = sema->types->void_type; return expr->type; }
            CinderType *callee = sema_expr(sema, expr->as.call.callee, scope);
            if (callee->kind != TYPE_FUNCTION) { if (callee->kind == TYPE_POINTER && callee->base->kind == TYPE_FUNCTION) callee = callee->base; else cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "called object is not a function"); }
            if (callee->kind == TYPE_FUNCTION) {
                for (size_t i = 0U; i < expr->as.call.args.len; ++i) {
                    CinderType *arg = sema_expr(sema, expr->as.call.args.data[i], scope);
                    if (i < callee->params.len && !value_compatible(callee->params.data[i].type, arg)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "argument %zu has incompatible type", i + 1U);
                    else if (i >= callee->params.len && !callee->variadic) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "too many arguments for non-variadic function");
                }
                expr->type = callee->return_type;
            } else expr->type = sema->types->error_type;
            return expr->type;
        }
        case EX_CONDITIONAL: {
            if (!numeric_type(sema_expr(sema, expr->as.conditional.condition, scope))) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "conditional condition must be scalar");
            CinderType *yes = sema_expr(sema, expr->as.conditional.yes, scope); CinderType *no = sema_expr(sema, expr->as.conditional.no, scope); expr->type = floating_type(yes) || floating_type(no) ? sema->types->double_type : (cinder_type_compatible(yes, no) ? yes : sema->types->int_type); return expr->type;
        }
        case EX_CAST: expr->as.cast.value->type = sema_expr(sema, expr->as.cast.value, scope); expr->type = expr->as.cast.cast_type; return expr->type;
        case EX_SIZEOF: (void)sema_expr(sema, expr->as.unary.value, scope); expr->type = sema->types->ulong_type; return expr->type;
    }
    return sema->types->error_type;
}

int cinder_sema_run(CinderSema *sema) {
    for (size_t i = 0U; i < sema->ast->declarations.len; ++i) {
        CinderDecl *decl = sema->ast->declarations.data[i];
        CinderSymbol *old = cinder_scope_lookup(&sema->globals, decl->name);
        if (old != NULL && !cinder_type_compatible(old->type, decl->type)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "conflicting declaration of '%s'", decl->name);
        else if (old == NULL) scope_add(&sema->globals, decl->name, decl->type, decl, decl->kind == DECL_FUNCTION);
    }
    for (size_t i = 0U; i < sema->ast->declarations.len; ++i) {
        CinderDecl *decl = sema->ast->declarations.data[i];
        if (decl->initializer != NULL) {
            CinderType *initializer_type = sema_expr(sema, decl->initializer, &sema->globals);
            bool string_array = decl->initializer->kind == EX_STRING && decl->type->kind == TYPE_ARRAY && decl->type->base->kind == TYPE_CHAR;
            if (!string_array && !cinder_type_compatible(decl->type, initializer_type)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "global initializer for '%s' has incompatible type", decl->name);
        }
        if (decl->body != NULL) {
            CinderScope scope = { {NULL, 0U, 0U}, &sema->globals };
            for (size_t p = 0U; p < decl->params.len; ++p) scope_add(&scope, decl->params.data[p]->name, decl->params.data[p]->type, decl->params.data[p], false);
            sema_stmt(sema, decl->body, &scope, decl->type->return_type, 0U);
            for (size_t p = 0U; p < scope.symbols.len; ++p) free(scope.symbols.data[p].name);
            free(scope.symbols.data);
        }
    }
    return sema->diags->errors == 0U ? 0 : 1;
}
