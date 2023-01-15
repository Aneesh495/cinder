#include "cinder.h"
#include <stdio.h>

static bool rejected(const CinderMIRFunction *machine) {
    CinderDiagnostics diags; cinder_diags_init(&diags);
    bool failed = cinder_verify_selected_mir(machine, &diags) != 0 && diags.errors != 0U;
    cinder_diags_destroy(&diags); return failed;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    CinderSourceManager sources; cinder_sources_init(&sources); CinderDiagnostics diags; cinder_diags_init(&diags);
    CinderTokenStream tokens; cinder_tokens_init(&tokens); CinderTypeContext types; cinder_types_init(&types);
    int failed = cinder_preprocess(&sources, argv[1], NULL, 0U, NULL, 0U, &diags) || cinder_lex(&sources, &tokens, &diags);
    CinderAst ast; cinder_ast_init(&ast, &types, &tokens, &diags); CinderSema sema; cinder_sema_init(&sema, &ast, &types, &diags);
    if (!failed) failed = cinder_parse(&ast) || cinder_sema_run(&sema);
    CinderIRModule module; cinder_ir_init(&module, &types); CinderOptStats stats;
    if (!failed) failed = cinder_lower_ir(&module, &ast, &diags) || cinder_verify_ir(&module, &diags) || cinder_optimize_source(&module, 0, NULL, &stats, &diags);
    CinderMachineObject object; cinder_machine_init(&object); unsigned addresses = 0U, mutations = 0U, roles = 0U;
    if (!failed) failed = cinder_lower_globals(&module, &object, &diags);
    for (size_t f = 0U; !failed && f < module.functions.len; ++f) {
        CinderIRFunction *function = &module.functions.data[f];
        if (cinder_mir_boundary(function, &diags) != 0) { failed = 1; break; }
        CinderAllocation allocation; cinder_alloc_init(&allocation, function);
        failed = cinder_allocate(&allocation, &diags) || cinder_verify_allocation(&allocation, &diags);
        for (size_t b = 0U; !failed && b < allocation.machine.blocks.len; ++b)
            for (size_t i = 0U; !failed && i < allocation.machine.blocks.data[b].instructions.len; ++i) {
                CinderMIRInst *inst = &allocation.machine.blocks.data[b].instructions.data[i];
                if (inst->kind != MIR_ADDRESS) continue;
                CinderMIRInst original = *inst; ++addresses; roles |= 1U << ((unsigned)inst->address.kind - 1U);
                for (unsigned change = 0U; change < 16U; ++change) {
                    switch (change) {
                        case 0U: inst->address.kind = (CinderMIRAddressKind)((unsigned)inst->address.kind % 5U + 1U); break;
                        case 1U: inst->address.before[0] ^= 1U; break;
                        case 2U: inst->address.before_size = 99U; break;
                        case 3U: inst->address.after[0] ^= 1U; break;
                        case 4U: inst->address.after_size = 99U; break;
                        case 5U: ++inst->address.frame_slot; break;
                        case 6U: ++inst->address.scale; break;
                        case 7U: ++inst->address.displacement; break;
                        case 8U: ++inst->address.relocation; break;
                        case 9U: ++inst->address.addend; break;
                        case 10U: inst->fixed_uses ^= 1U; break;
                        case 11U: inst->clobbers ^= 4U; break;
                        case 12U: inst->kind = MIR_PSEUDO; break;
                        case 13U: inst->operands.integer ^= 1; break;
                        case 14U: inst->operands.operator_code ^= 1; break;
                        case 15U: ++inst->operands.slot; break;
                        default: return 2;
                    }
                    if (rejected(&allocation.machine)) ++mutations; else failed = 1;
                    *inst = original;
                }
            }
        if (!failed) failed = cinder_lower_x86(function, &allocation, &object, false, NULL, &diags);
        cinder_alloc_destroy(&allocation);
    }
    if (!failed) failed = cinder_write_elf64(&object, argv[2], &diags);
    if (!failed) printf("{\"addresses\":%u,\"rejected\":%u,\"roles\":%u}\n", addresses, mutations, roles);
    if (failed) cinder_diag_print(&diags, &sources, stderr);
    cinder_machine_destroy(&object); cinder_ir_destroy(&module); cinder_sema_destroy(&sema); cinder_ast_destroy(&ast);
    cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources); return failed;
}
