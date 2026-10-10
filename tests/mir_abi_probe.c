#include "cinder.h"
#include <stdlib.h>
#include <string.h>

static unsigned reject_plan(CinderAllocation *allocation, CinderMIRCallPlan *plan, bool outgoing) {
    unsigned rejected = 0U;
    for (unsigned change = 0U; change < 6U; ++change) {
        CinderMIRCallPlan original = *plan;
        CinderABIArgument argument = {0}; size_t staging = 0U;
        if (plan->argument_count != 0U) { argument = plan->arguments[0]; staging = plan->staging[0]; }
        if (change == 0U) ++plan->state.gpr;
        else if (change == 1U) ++plan->state.sse;
        else if (change == 2U) plan->result.memory = !plan->result.memory;
        else if (change == 3U) ++plan->argument_count;
        else if (change == 4U) { if (outgoing) plan->frame_size += 8U; else ++plan->state.stack; }
        else if (plan->argument_count != 0U) { if (outgoing) ++plan->staging[0]; else ++plan->arguments[0].registers[0]; }
        else ++plan->result.align;
        CinderDiagnostics invalid; cinder_diags_init(&invalid);
        if (cinder_verify_selected_mir(&allocation->machine, &invalid) != 0 && invalid.errors != 0U) ++rejected;
        cinder_diags_destroy(&invalid); *plan = original;
        if (plan->argument_count != 0U) { plan->arguments[0] = argument; plan->staging[0] = staging; }
    }
    return rejected;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    CinderSourceManager sources; cinder_sources_init(&sources); CinderDiagnostics diags; cinder_diags_init(&diags);
    CinderTokenStream tokens; cinder_tokens_init(&tokens); CinderTypeContext types; cinder_types_init(&types);
    const char *includes[] = {"runtime/include"};
    int failed = cinder_preprocess(&sources, argv[1], includes, 1U, NULL, 0U, &diags);
    if (!failed) failed = cinder_lex(&sources, &tokens, &diags);
    CinderAst ast; cinder_ast_init(&ast, &types, &tokens, &diags); CinderSema sema; cinder_sema_init(&sema, &ast, &types, &diags);
    if (!failed) failed = cinder_parse(&ast) || cinder_sema_run(&sema);
    CinderIRModule module; cinder_ir_init(&module, &types);
    CinderOptStats stats;
    if (!failed) failed = cinder_lower_ir(&module, &ast, &diags) || cinder_verify_ir(&module, &diags) || cinder_optimize_source(&module, 0, NULL, &stats, &diags);
    CinderMachineObject object; cinder_machine_init(&object); unsigned calls = 0U, variadic = 0U, rejected = 0U;
    if (!failed) failed = cinder_lower_globals(&module, &object, &diags);
    for (size_t f = 0U; !failed && f < module.functions.len; ++f) {
        CinderIRFunction *function = &module.functions.data[f];
        if (cinder_mir_boundary(function, &diags) != 0) { failed = 1; break; }
        CinderAllocation allocation; cinder_alloc_init(&allocation, function);
        failed = cinder_allocate(&allocation, &diags) || cinder_verify_allocation(&allocation, &diags);
        if (!failed) { unsigned checked = reject_plan(&allocation, allocation.machine.signature, false); rejected += checked; if (checked != 6U) failed = 1; }
        for (size_t b = 0U; !failed && b < allocation.machine.blocks.len; ++b)
            for (size_t i = 0U; !failed && i < allocation.machine.blocks.data[b].instructions.len; ++i) {
                CinderMIRInst *inst = &allocation.machine.blocks.data[b].instructions.data[i];
                if (inst->call != NULL) { ++calls; unsigned checked = reject_plan(&allocation, inst->call, true); rejected += checked; if (checked != 6U) failed = 1; }
                if (inst->variadic_layout != NULL) {
                    ++variadic; CinderABIValue original = *inst->variadic_layout;
                    for (unsigned change = 0U; change < 3U; ++change) {
                        if (change == 0U) ++inst->variadic_layout->size;
                        else if (change == 1U) ++inst->variadic_layout->align;
                        else inst->variadic_layout->memory = !inst->variadic_layout->memory;
                        CinderDiagnostics invalid; cinder_diags_init(&invalid);
                        if (cinder_verify_selected_mir(&allocation.machine, &invalid) != 0 && invalid.errors != 0U) ++rejected; else failed = 1;
                        cinder_diags_destroy(&invalid); *inst->variadic_layout = original;
                    }
                }
                if (inst->operands.op == IR_CALL) {
                    CinderIRInst *source = &function->blocks.data[b].instructions.data[i]; CinderType *signature = source->callee_type;
                    source->callee_type = NULL; CinderMIRFunction invalid_machine; cinder_selected_mir_init(&invalid_machine);
                    CinderDiagnostics invalid; cinder_diags_init(&invalid);
                    if (cinder_select_mir(function, &invalid_machine, &invalid) != 0 && invalid.errors != 0U) ++rejected; else failed = 1;
                    cinder_selected_mir_destroy(&invalid_machine); cinder_diags_destroy(&invalid); source->callee_type = signature;
                }
            }
        if (!failed) failed = cinder_lower_x86(function, &allocation, &object, false, NULL, &diags);
        cinder_alloc_destroy(&allocation);
    }
    if (!failed) failed = cinder_write_elf64(&object, argv[2], &diags);
    if (!failed) printf("{\"functions\":%zu,\"calls\":%u,\"variadic\":%u,\"rejected\":%u}\n", module.functions.len, calls, variadic, rejected);
    if (failed) cinder_diag_print(&diags, &sources, stderr);
    cinder_machine_destroy(&object); cinder_ir_destroy(&module); cinder_sema_destroy(&sema); cinder_ast_destroy(&ast);
    cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources); return failed;
}
