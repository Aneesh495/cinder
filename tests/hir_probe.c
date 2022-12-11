#include "cinder.h"

#include <stdio.h>
#include <string.h>

static bool rejects(CinderHIR *hir) {
    CinderDiagnostics diags; cinder_diags_init(&diags);
    bool rejected = cinder_verify_hir(hir, &diags) != 0 && diags.errors != 0U;
    cinder_diags_destroy(&diags); return rejected;
}

static bool observes(CinderHIR *hir, int64_t expected, CinderDiagnostics *diags) {
    CinderIRModule module; cinder_ir_init(&module, hir->types);
    bool valid = cinder_lower_hir_ir(&module, hir, diags) == 0 && cinder_verify_ir(&module, diags) == 0;
    if (valid) {
        CinderInterpResult result = cinder_interpret(&module, "main", NULL, 0U, 100000U, diags);
        valid = result.valid && result.value == expected;
    }
    cinder_ir_destroy(&module); return valid;
}

static bool object_control_unit(const char *path) {
    CinderSourceManager sources; CinderDiagnostics diags; CinderTokenStream tokens; CinderTypeContext types; CinderAst ast; CinderSema sema; CinderHIR hir; CinderIRModule module;
    cinder_sources_init(&sources); cinder_diags_init(&diags); cinder_tokens_init(&tokens); cinder_types_init(&types); cinder_hir_init(&hir, &types); cinder_ir_init(&module, &types);
    bool valid = cinder_preprocess(&sources, path, NULL, 0U, NULL, 0U, &diags) == 0 && cinder_lex(&sources, &tokens, &diags) == 0;
    cinder_ast_init(&ast, &types, &tokens, &diags); cinder_sema_init(&sema, &ast, &types, &diags);
    if (valid) valid = cinder_parse(&ast) == 0 && cinder_sema_run(&sema) == 0 && cinder_build_hir(&hir, &ast, &diags) == 0;
    cinder_sema_destroy(&sema); cinder_ast_destroy(&ast);
    if (valid) valid = cinder_lower_hir_ir(&module, &hir, &diags) == 0 && cinder_verify_ir(&module, &diags) == 0;
    CinderIRGlobal *values = NULL;
    if (valid) for (size_t i = 0U; i < hir.static_objects->globals.len; ++i) if (strcmp(hir.static_objects->globals.data[i].name, "values") == 0) values = &hir.static_objects->globals.data[i];
    if (valid && (values == NULL || values->byte_count != 12U || values->bytes == NULL || (unsigned char)values->bytes[0] != 2U)) valid = false;
    if (valid) { values->bytes[0] = 9; valid = observes(&hir, 33, &diags); }
    cinder_hir_destroy(&hir);
    if (valid) { CinderInterpResult result = cinder_interpret(&module, "main", NULL, 0U, 100000U, &diags); valid = result.valid && result.value == 26; }
    if (diags.errors != 0U) cinder_diag_print(&diags, &sources, stderr);
    cinder_ir_destroy(&module); cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources); return valid;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    CinderSourceManager sources; CinderDiagnostics diags; CinderTokenStream tokens; CinderTypeContext types; CinderAst ast; CinderSema sema; CinderHIR hir;
    cinder_sources_init(&sources); cinder_diags_init(&diags); cinder_tokens_init(&tokens); cinder_types_init(&types); cinder_hir_init(&hir, &types);
    bool ast_live = true, sema_live = true;
    int failed = cinder_preprocess(&sources, argv[1], NULL, 0U, NULL, 0U, &diags) || cinder_lex(&sources, &tokens, &diags);
    cinder_ast_init(&ast, &types, &tokens, &diags); cinder_sema_init(&sema, &ast, &types, &diags);
    if (!failed) failed = cinder_parse(&ast) || cinder_sema_run(&sema) || cinder_build_hir(&hir, &ast, &diags);
    CinderHIRExpr *constant = NULL, *load = NULL, *address = NULL, *conversion = NULL, *call = NULL;
    if (!failed) for (size_t i = 0U; i < hir.expressions.len; ++i) {
        CinderHIRExpr *expr = hir.expressions.data[i];
        if (expr->kind == HIR_INT && expr->as.integer == 17) constant = expr;
        if (expr->kind == HIR_LOAD) load = expr;
        if (expr->kind == HIR_ADDRESS) address = expr;
        if (expr->kind == HIR_CAST) conversion = expr;
        if (expr->kind == HIR_CALL) call = expr;
    }
    if (!failed && (constant == NULL || load == NULL || address == NULL || conversion == NULL || call == NULL)) failed = 1;
    if (!failed) {
        CinderDecl *main_decl = ast.declarations.data[1];
        CinderExpr *original = main_decl->body->as.block.items.data[0]->as.decl->initializer;
        if (original->kind != EX_INT || original->as.integer != 17) failed = 1;
        else {
            original->as.integer = 99;
            cinder_sema_destroy(&sema); sema_live = false;
            cinder_ast_destroy(&ast); ast_live = false;
            if (!observes(&hir, 17, &diags)) failed = 1;
            constant->as.integer = 23;
            if (!observes(&hir, 23, &diags)) failed = 1;
        }
    }
    unsigned rejected = 0U;
    if (!failed) {
        uint32_t id = constant->id; constant->id = UINT32_MAX; rejected += rejects(&hir); constant->id = id;
        CinderType *type = constant->type; constant->type = NULL; rejected += rejects(&hir); constant->type = type;
        CinderHIRKind kind = constant->kind; constant->kind = (CinderHIRKind)99; rejected += rejects(&hir); constant->kind = kind;
        CinderHIRExpr *place = load->as.unary.value; load->as.unary.value = NULL; rejected += rejects(&hir); load->as.unary.value = place;
        load->is_place = true; rejected += rejects(&hir); load->is_place = false;
        type = load->type; load->type = types.double_type; rejected += rejects(&hir); load->type = type;
        CinderHIRExpr foreign; memset(&foreign, 0, sizeof(foreign)); foreign.id = constant->id;
        CinderHIRExpr *argument = call->as.call.args.data[0]; call->as.call.args.data[0] = &foreign; rejected += rejects(&hir); call->as.call.args.data[0] = argument;
        type = address->type; address->type = types.int_type; rejected += rejects(&hir); address->type = type;
        type = conversion->as.cast.cast_type; conversion->as.cast.cast_type = types.double_type; rejected += rejects(&hir); conversion->as.cast.cast_type = type;
        CinderHIRExpr saved = *place;
        place->kind = HIR_UNARY; place->as.unary.op = '+'; place->as.unary.value = load;
        rejected += rejects(&hir); *place = saved;
        CinderHIRDecl *definition = hir.roots.data[1]; CinderHIRStmt *body = definition->body;
        CinderHIRDecl foreign_decl; memset(&foreign_decl, 0, sizeof(foreign_decl)); foreign_decl.id = definition->id;
        hir.roots.data[1] = &foreign_decl; rejected += rejects(&hir); hir.roots.data[1] = definition;
        int parent = definition->control.scopes.data[0].parent; definition->control.scopes.data[0].parent = 0;
        rejected += rejects(&hir); definition->control.scopes.data[0].parent = parent;
        CinderHIRDecl *local = body->as.block.items.data[0]->as.decl, *next = local->next;
        local->next = local; rejected += rejects(&hir); local->next = next;
        CinderHIRStmt *child = body->as.block.items.data[0]; body->as.block.items.data[0] = body;
        rejected += rejects(&hir); body->as.block.items.data[0] = child;
        body->as.block.items.data[0] = NULL; rejected += rejects(&hir); body->as.block.items.data[0] = child;
        CinderHIRStmt *ret = body->as.block.items.data[2]; CinderHIRExpr *value = ret->as.ret.value;
        ret->as.ret.value = &foreign; rejected += rejects(&hir); ret->as.ret.value = value;
        if (rejected != 16U || cinder_verify_hir(&hir, &diags) != 0 || !observes(&hir, 23, &diags)) failed = 1;
    }
    if (diags.errors != 0U) cinder_diag_print(&diags, &sources, stderr);
    cinder_hir_destroy(&hir); if (sema_live) cinder_sema_destroy(&sema); if (ast_live) cinder_ast_destroy(&ast); cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources);
    if (!failed && !object_control_unit(argv[2])) failed = 1;
    if (!failed) puts("HIR ownership: retired AST and HIR isolated, control/object plans consumed, sixteen malformed graphs rejected");
    return failed ? 1 : 0;
}
