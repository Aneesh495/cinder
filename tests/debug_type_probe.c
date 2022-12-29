#include "cinder.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    CinderSourceManager sources; CinderDiagnostics diags; CinderTokenStream tokens;
    CinderTypeContext types; CinderAst ast; CinderSema sema; CinderHIR hir; CinderIRModule module; CinderMachineObject object;
    cinder_sources_init(&sources); cinder_diags_init(&diags); cinder_tokens_init(&tokens); cinder_types_init(&types);
    cinder_hir_init(&hir, &types); cinder_ir_init(&module, &types); cinder_machine_init(&object); object.debug_enabled = true;
    int failed = cinder_preprocess(&sources, argv[1], NULL, 0U, NULL, 0U, &diags) || cinder_lex(&sources, &tokens, &diags);
    cinder_ast_init(&ast, &types, &tokens, &diags); cinder_sema_init(&sema, &ast, &types, &diags);
    if (!failed) failed = cinder_parse(&ast) || cinder_sema_run(&sema) || cinder_build_hir(&hir, &ast, &diags);
    cinder_sema_destroy(&sema); cinder_ast_destroy(&ast);
    if (!failed) failed = cinder_lower_hir_ir(&module, &hir, &diags) || cinder_verify_ir(&module, &diags);
    cinder_hir_destroy(&hir);
    if (!failed) {
        CinderOptStats stats;
        failed = cinder_optimize_source(&module, 0, NULL, &stats, &diags) || cinder_lower_globals(&module, &object, &diags);
    }
    for (size_t i = 0U; !failed && i < module.functions.len; ++i) {
        CinderIRFunction *function = &module.functions.data[i];
        failed = cinder_mir_boundary(function, &diags);
        if (!failed) {
            CinderAllocation allocation; cinder_alloc_init(&allocation, function);
            failed = cinder_allocate(&allocation, &diags) || cinder_verify_allocation(&allocation, &diags) ||
                cinder_lower_x86(function, &allocation, &object, false, NULL, &diags);
            cinder_alloc_destroy(&allocation);
        }
    }
    if (!failed) failed = cinder_debug_finish(&object, &module, &sources, &diags);
    if (diags.errors != 0U) cinder_diag_print(&diags, &sources, stderr);
    cinder_ir_destroy(&module); cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_sources_destroy(&sources);
    /* ELF publication must consume only the already-owned debug bytes/fixups. */
    if (!failed) failed = cinder_write_elf64(&object, argv[2], &diags);
    cinder_machine_destroy(&object); cinder_diags_destroy(&diags);
    if (failed) return 1;
    puts("Debug types: AST and HIR retired before construction; IR, types and sources retired before ELF publication");
    return 0;
}
