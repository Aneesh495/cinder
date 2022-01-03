#include "cinder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cinder_ast_init(CinderAst *ast, CinderTypeContext *types, CinderTokenStream *tokens, CinderDiagnostics *diags) {
    ast->declarations.data = NULL;
    ast->declarations.len = 0U;
    ast->declarations.cap = 0U;
    cinder_arena_init(&ast->arena, 32768U);
    ast->types = types;
    ast->tokens = tokens;
    ast->diags = diags;
    ast->cursor = 0U;
}

static void free_expr(CinderExpr *expr) {
    if (expr == NULL) return;
    if (expr->kind == EX_BINARY) {
        free_expr(expr->as.binary.left);
        free_expr(expr->as.binary.right);
    } else if (expr->kind == EX_UNARY || expr->kind == EX_SIZEOF) {
        free_expr(expr->as.unary.value);
    } else if (expr->kind == EX_ASSIGN) {
        free_expr(expr->as.assign.target);
        free_expr(expr->as.assign.value);
    } else if (expr->kind == EX_CALL) {
        free_expr(expr->as.call.callee);
        for (size_t i = 0U; i < expr->as.call.args.len; ++i) free_expr(expr->as.call.args.data[i]);
        free(expr->as.call.args.data);
    } else if (expr->kind == EX_CONDITIONAL) {
        free_expr(expr->as.conditional.condition);
        free_expr(expr->as.conditional.yes);
        free_expr(expr->as.conditional.no);
    } else if (expr->kind == EX_CAST) {
        free_expr(expr->as.cast.value);
    }
}

static void free_stmt(CinderStmt *stmt) {
    if (stmt == NULL) return;
    switch (stmt->kind) {
        case ST_EXPR: free_expr(stmt->as.expr); break;
        case ST_RETURN: free_expr(stmt->as.ret.value); break;
        case ST_BLOCK:
            for (size_t i = 0U; i < stmt->as.block.items.len; ++i) free_stmt(stmt->as.block.items.data[i]);
            free(stmt->as.block.items.data);
            break;
        case ST_IF:
            free_expr(stmt->as.if_stmt.condition);
            free_stmt(stmt->as.if_stmt.then_branch);
            free_stmt(stmt->as.if_stmt.else_branch);
            break;
        case ST_WHILE:
        case ST_DO:
            free_expr(stmt->as.loop.condition);
            free_stmt(stmt->as.loop.body);
            break;
        case ST_FOR:
            free_stmt(stmt->as.for_stmt.init);
            free_expr(stmt->as.for_stmt.condition);
            free_expr(stmt->as.for_stmt.step);
            free_stmt(stmt->as.for_stmt.body);
            break;
        case ST_DECL:
            if (stmt->as.decl != NULL) free_expr(stmt->as.decl->initializer);
            break;
        case ST_EMPTY:
        case ST_BREAK:
        case ST_CONTINUE:
            break;
    }
}

void cinder_ast_destroy(CinderAst *ast) {
    for (size_t i = 0U; i < ast->declarations.len; ++i) {
        CinderDecl *decl = ast->declarations.data[i];
        free_expr(decl->initializer);
        free_stmt(decl->body);
        free(decl->params.data);
    }
    free(ast->declarations.data);
    cinder_arena_destroy(&ast->arena);
}

static void indent(FILE *out, unsigned depth) { for (unsigned i = 0U; i < depth; ++i) fputs("  ", out); }

static void dump_expr(const CinderExpr *expr, FILE *out, unsigned depth) {
    if (expr == NULL) { indent(out, depth); fputs("<null>\n", out); return; }
    indent(out, depth);
    switch (expr->kind) {
        case EX_INT: fprintf(out, "int %lld\n", (long long)expr->as.integer); break;
        case EX_CHAR: fprintf(out, "char %lld\n", (long long)expr->as.integer); break;
        case EX_STRING: fprintf(out, "string %s\n", expr->as.string); break;
        case EX_NAME: fprintf(out, "name %s : %s%s\n", expr->as.name, cinder_type_name(expr->type), expr->is_lvalue ? " lvalue" : ""); break;
        case EX_BINARY:
            fprintf(out, "binary '%c' : %s\n", expr->as.binary.op, cinder_type_name(expr->type));
            dump_expr(expr->as.binary.left, out, depth + 1U); dump_expr(expr->as.binary.right, out, depth + 1U); break;
        case EX_UNARY:
            fprintf(out, "unary '%c' : %s\n", expr->as.unary.op, cinder_type_name(expr->type)); dump_expr(expr->as.unary.value, out, depth + 1U); break;
        case EX_ASSIGN: fputs("assign\n", out); dump_expr(expr->as.assign.target, out, depth + 1U); dump_expr(expr->as.assign.value, out, depth + 1U); break;
        case EX_CALL:
            fprintf(out, "call : %s\n", cinder_type_name(expr->type)); dump_expr(expr->as.call.callee, out, depth + 1U);
            for (size_t i = 0U; i < expr->as.call.args.len; ++i) dump_expr(expr->as.call.args.data[i], out, depth + 2U); break;
        case EX_CONDITIONAL: fputs("conditional\n", out); dump_expr(expr->as.conditional.condition, out, depth + 1U); dump_expr(expr->as.conditional.yes, out, depth + 1U); dump_expr(expr->as.conditional.no, out, depth + 1U); break;
        case EX_CAST: fprintf(out, "cast %s\n", cinder_type_name(expr->as.cast.cast_type)); dump_expr(expr->as.cast.value, out, depth + 1U); break;
        case EX_SIZEOF: fputs("sizeof\n", out); dump_expr(expr->as.unary.value, out, depth + 1U); break;
    }
}

static void dump_stmt(const CinderStmt *stmt, FILE *out, unsigned depth) {
    if (stmt == NULL) return;
    indent(out, depth);
    switch (stmt->kind) {
        case ST_EMPTY: fputs("empty\n", out); break;
        case ST_EXPR: fputs("expr\n", out); dump_expr(stmt->as.expr, out, depth + 1U); break;
        case ST_RETURN: fputs("return\n", out); dump_expr(stmt->as.ret.value, out, depth + 1U); break;
        case ST_BLOCK:
            fputs("block\n", out); for (size_t i = 0U; i < stmt->as.block.items.len; ++i) dump_stmt(stmt->as.block.items.data[i], out, depth + 1U); break;
        case ST_IF: fputs("if\n", out); dump_expr(stmt->as.if_stmt.condition, out, depth + 1U); dump_stmt(stmt->as.if_stmt.then_branch, out, depth + 1U); dump_stmt(stmt->as.if_stmt.else_branch, out, depth + 1U); break;
        case ST_WHILE: fputs("while\n", out); dump_expr(stmt->as.loop.condition, out, depth + 1U); dump_stmt(stmt->as.loop.body, out, depth + 1U); break;
        case ST_DO: fputs("do\n", out); dump_stmt(stmt->as.loop.body, out, depth + 1U); dump_expr(stmt->as.loop.condition, out, depth + 1U); break;
        case ST_FOR: fputs("for\n", out); dump_stmt(stmt->as.for_stmt.init, out, depth + 1U); dump_expr(stmt->as.for_stmt.condition, out, depth + 1U); dump_expr(stmt->as.for_stmt.step, out, depth + 1U); dump_stmt(stmt->as.for_stmt.body, out, depth + 1U); break;
        case ST_BREAK: fputs("break\n", out); break;
        case ST_CONTINUE: fputs("continue\n", out); break;
        case ST_DECL: fprintf(out, "decl %s : %s\n", stmt->as.decl->name, cinder_type_name(stmt->as.decl->type)); dump_expr(stmt->as.decl->initializer, out, depth + 1U); break;
    }
}

void cinder_dump_ast(const CinderAst *ast, CinderSourceManager *sources, FILE *out) {
    (void)sources;
    for (size_t i = 0U; i < ast->declarations.len; ++i) {
        const CinderDecl *decl = ast->declarations.data[i];
        fprintf(out, "%s %s : %s%s\n", decl->kind == DECL_FUNCTION ? "function" : "object", decl->name, cinder_type_name(decl->type), decl->is_definition ? " definition" : " declaration");
        for (size_t p = 0U; p < decl->params.len; ++p) fprintf(out, "  param %s : %s\n", decl->params.data[p]->name, cinder_type_name(decl->params.data[p]->type));
        dump_expr(decl->initializer, out, 1U);
        dump_stmt(decl->body, out, 1U);
    }
}
