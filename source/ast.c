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
    ast->bindings.data = NULL; ast->bindings.len = 0U; ast->bindings.cap = 0U;
    ast->constant_exprs.data = NULL; ast->constant_exprs.len = 0U; ast->constant_exprs.cap = 0U;
    ast->static_literals.data = NULL; ast->static_literals.len = 0U; ast->static_literals.cap = 0U;
    ast->scope_depth = 0U; ast->declarator_depth = 0U;
    ast->literal_scope = NULL; ast->literal_count = 0U;
    ast->current_function = NULL;
}

static void free_expr(CinderExpr *expr) {
    if (expr == NULL) return;
    if (expr->kind == EX_BINARY) {
        free_expr(expr->as.binary.left);
        free_expr(expr->as.binary.right);
    } else if (expr->kind == EX_UNARY || expr->kind == EX_SIZEOF || expr->kind == EX_ALIGNOF || expr->kind == EX_DECAY) {
        free_expr(expr->as.unary.value);
    } else if (expr->kind == EX_ASSIGN) {
        free_expr(expr->as.assign.target);
        free_expr(expr->as.assign.value);
    } else if (expr->kind == EX_CALL) {
        free_expr(expr->as.call.callee);
        for (size_t i = 0U; i < expr->as.call.args.len; ++i) free_expr(expr->as.call.args.data[i]);
        free(expr->as.call.args.data);
    } else if (expr->kind == EX_VA_ARG) {
        free_expr(expr->as.va_arg.list);
    } else if (expr->kind == EX_CONDITIONAL) {
        free_expr(expr->as.conditional.condition);
        free_expr(expr->as.conditional.yes);
        free_expr(expr->as.conditional.no);
    } else if (expr->kind == EX_CAST) {
        free_expr(expr->as.cast.value);
    } else if (expr->kind == EX_COMPOUND_LITERAL) {
        free_expr(expr->as.compound_literal->initializer);
        free(expr->as.compound_literal->init_actions.data);
    } else if (expr->kind == EX_INDEX) {
        free_expr(expr->as.index.base); free_expr(expr->as.index.index);
    } else if (expr->kind == EX_MEMBER) {
        free_expr(expr->as.member.base);
    } else if (expr->kind == EX_INIT_LIST) {
        for (size_t i = 0U; i < expr->as.initializer.entries.len; ++i) {
            CinderInitEntry *entry = &expr->as.initializer.entries.data[i];
            free_expr(entry->value);
            for (size_t d = 0U; d < entry->designators.len; ++d) free_expr(entry->designators.data[d].index);
            free(entry->designators.data);
        }
        free(expr->as.initializer.entries.data);
    }
}

static void free_stmt(CinderStmt *stmt) {
    if (stmt == NULL) return;
    free(stmt->literal_objects.data);
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
            for (CinderDecl *decl = stmt->as.decl; decl != NULL; decl = decl->next) { free_expr(decl->initializer); free(decl->params.data); free(decl->init_actions.data); }
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
        free(decl->init_actions.data);
    }
    free(ast->declarations.data);
    free(ast->bindings.data);
    for (size_t i = 0U; i < ast->constant_exprs.len; ++i) free_expr(ast->constant_exprs.data[i].expression);
    free(ast->constant_exprs.data);
    free(ast->static_literals.data);
    cinder_arena_destroy(&ast->arena);
}

static void indent(FILE *out, unsigned depth) { for (unsigned i = 0U; i < depth; ++i) fputs("  ", out); }

static void dump_expr(const CinderExpr *expr, FILE *out, unsigned depth) {
    if (expr == NULL) { indent(out, depth); fputs("<null>\n", out); return; }
    indent(out, depth);
    switch (expr->kind) {
        case EX_COMPOUND_LITERAL:
            fprintf(out, "compound-literal %s : %s %s-storage lvalue\n", expr->as.compound_literal->name, cinder_type_name(expr->type), expr->as.compound_literal->is_static ? "static" : "automatic");
            dump_expr(expr->as.compound_literal->initializer, out, depth + 1U); break;
        case EX_INIT_LIST:
            fprintf(out, "initializer-list entries=%zu\n", expr->as.initializer.entries.len);
            for (size_t i = 0U; i < expr->as.initializer.entries.len; ++i) {
                const CinderInitEntry *entry = &expr->as.initializer.entries.data[i];
                for (size_t d = 0U; d < entry->designators.len; ++d) {
                    const CinderInitDesignator *designator = &entry->designators.data[d]; indent(out, depth + 1U);
                    if (designator->member != NULL) fprintf(out, "designator .%s\n", designator->member);
                    else { fputs("designator index\n", out); dump_expr(designator->index, out, depth + 2U); }
                }
                dump_expr(entry->value, out, depth + 1U);
            }
            break;
        case EX_INT: fprintf(out, "int %lld\n", (long long)expr->as.integer); break;
        case EX_FLOAT: fprintf(out, "double %.17g\n", expr->as.floating); break;
        case EX_CHAR: fprintf(out, "char %lld\n", (long long)expr->as.integer); break;
        case EX_STRING:
            fputs("string bytes=", out);
            for (size_t i = 0U; i < expr->literal_length; ++i) fprintf(out, "%02x", (unsigned char)expr->as.string[i]);
            fputc('\n', out); break;
        case EX_NAME: fprintf(out, "name %s : %s%s\n", expr->as.name, cinder_type_name(expr->type), expr->is_lvalue ? " lvalue" : ""); break;
        case EX_BINARY:
            fprintf(out, "binary '%c' : %s\n", expr->as.binary.op, cinder_type_name(expr->type));
            dump_expr(expr->as.binary.left, out, depth + 1U); dump_expr(expr->as.binary.right, out, depth + 1U); break;
        case EX_UNARY:
            fprintf(out, "unary '%c' : %s\n", expr->as.unary.op, cinder_type_name(expr->type)); dump_expr(expr->as.unary.value, out, depth + 1U); break;
        case EX_ASSIGN: fputs("assign\n", out); dump_expr(expr->as.assign.target, out, depth + 1U); dump_expr(expr->as.assign.value, out, depth + 1U); break;
        case EX_CALL:
            fprintf(out, "call : %s\n", cinder_type_name(expr->type)); dump_expr(expr->as.call.callee, out, depth + 1U);
            for (size_t i = 0U; i < expr->as.call.args.len; ++i) {
                dump_expr(expr->as.call.args.data[i], out, depth + 2U);
            }
            break;
        case EX_VA_ARG: fprintf(out, "va_arg : %s\n", cinder_type_name(expr->as.va_arg.type)); dump_expr(expr->as.va_arg.list, out, depth + 1U); break;
        case EX_CONDITIONAL: fputs("conditional\n", out); dump_expr(expr->as.conditional.condition, out, depth + 1U); dump_expr(expr->as.conditional.yes, out, depth + 1U); dump_expr(expr->as.conditional.no, out, depth + 1U); break;
        case EX_CAST: fprintf(out, "cast %s\n", cinder_type_name(expr->as.cast.cast_type)); dump_expr(expr->as.cast.value, out, depth + 1U); break;
        case EX_ALIGNOF: case EX_SIZEOF: fputs("sizeof\n", out); dump_expr(expr->as.unary.value, out, depth + 1U); break;
        case EX_DECAY: fputs("decay\n", out); dump_expr(expr->as.unary.value, out, depth + 1U); break;
        case EX_INDEX: fputs("index\n", out); dump_expr(expr->as.index.base, out, depth + 1U); dump_expr(expr->as.index.index, out, depth + 1U); break;
        case EX_MEMBER: fprintf(out, "member %s\n", expr->as.member.name); dump_expr(expr->as.member.base, out, depth + 1U); break;
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
        case ST_DECL:
            for (const CinderDecl *decl = stmt->as.decl; decl != NULL; decl = decl->next) { fprintf(out, "decl %s : %s\n", decl->name, cinder_type_name(decl->type)); dump_expr(decl->initializer, out, depth + 1U); }
            break;
    }
}

void cinder_dump_ast(const CinderAst *ast, CinderSourceManager *sources, FILE *out) {
    (void)sources;
    for (size_t i = 0U; i < ast->declarations.len; ++i) {
        const CinderDecl *decl = ast->declarations.data[i];
        fprintf(out, "%s %s : %s%s\n", decl->kind == DECL_FUNCTION ? "function" : decl->kind == DECL_TYPEDEF ? "typedef" : "object", decl->name, cinder_type_name(decl->type), decl->is_definition ? " definition" : " declaration");
        for (size_t p = 0U; p < decl->params.len; ++p) fprintf(out, "  param %s : %s\n", decl->params.data[p]->name == NULL ? "<unnamed>" : decl->params.data[p]->name, cinder_type_name(decl->params.data[p]->type));
        dump_expr(decl->initializer, out, 1U);
        dump_stmt(decl->body, out, 1U);
    }
}
