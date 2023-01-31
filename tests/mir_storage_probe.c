#include "cinder.h"
#include <stdio.h>

static bool rejected(CinderIRFunction *function, CinderAllocation *allocation) {
    CinderDiagnostics diags; cinder_diags_init(&diags);
    bool failed = cinder_verify_selected_mir(&allocation->machine, &diags) != 0 && diags.errors != 0U;
    cinder_diags_destroy(&diags); cinder_diags_init(&diags);
    CinderMachineObject object; cinder_machine_init(&object);
    failed = failed && cinder_lower_x86(function, allocation, &object, false, NULL, &diags) != 0 && object.text.len == 0U && object.defined_symbols.len == 0U;
    cinder_machine_destroy(&object); cinder_diags_destroy(&diags); return failed;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    CinderSourceManager sources; cinder_sources_init(&sources); CinderDiagnostics diags; cinder_diags_init(&diags);
    CinderTokenStream tokens; cinder_tokens_init(&tokens); CinderTypeContext types; cinder_types_init(&types);
    const char *includes[] = {"runtime/include"};
    int failed = cinder_preprocess(&sources, argv[1], includes, 1U, NULL, 0U, &diags) || cinder_lex(&sources, &tokens, &diags);
    CinderAst ast; cinder_ast_init(&ast, &types, &tokens, &diags); CinderSema sema; cinder_sema_init(&sema, &ast, &types, &diags);
    if (!failed) failed = cinder_parse(&ast) || cinder_sema_run(&sema);
    CinderIRModule module; cinder_ir_init(&module, &types); CinderOptStats stats;
    if (!failed) failed = cinder_lower_ir(&module, &ast, &diags) || cinder_verify_ir(&module, &diags) || cinder_optimize_source(&module, 0, NULL, &stats, &diags);
    CinderMachineObject object; cinder_machine_init(&object); unsigned memory = 0U, bitfields = 0U, mutations = 0U, roles = 0U, bitfield_roles = 0U;
    if (!failed) failed = cinder_lower_globals(&module, &object, &diags);
    for (size_t f = 0U; !failed && f < module.functions.len; ++f) {
        CinderIRFunction *function = &module.functions.data[f];
        if (cinder_mir_boundary(function, &diags) != 0) { failed = 1; break; }
        CinderAllocation allocation; cinder_alloc_init(&allocation, function);
        failed = cinder_allocate(&allocation, &diags) || cinder_verify_allocation(&allocation, &diags);
        for (size_t b = 0U; !failed && b < allocation.machine.blocks.len; ++b)
            for (size_t i = 0U; !failed && i < allocation.machine.blocks.data[b].instructions.len; ++i) {
                CinderMIRInst *inst = &allocation.machine.blocks.data[b].instructions.data[i];
                if (inst->kind != MIR_MEMORY && inst->kind != MIR_BITFIELD) continue;
                CinderMIRInst original = *inst;
                if (inst->kind == MIR_MEMORY) { ++memory; roles |= 1U << ((unsigned)inst->storage.kind - 1U); }
                else { ++bitfields; bitfield_roles |= 1U << ((unsigned)inst->bitfield.kind - 1U); }
                for (unsigned change = 0U; change < 16U; ++change) {
                    switch (change) {
                        case 0U: inst->kind = MIR_PSEUDO; break;
                        case 1U: inst->storage.kind = (CinderMIRStorageKind)((unsigned)inst->storage.kind + 1U); break;
                        case 2U: inst->storage.address ^= 1U; break;
                        case 3U: inst->storage.value ^= 1U; break;
                        case 4U: ++inst->storage.frame_slot; break;
                        case 5U: inst->storage.modrm ^= 1U; break;
                        case 6U: inst->storage.extended = !inst->storage.extended; break;
                        case 7U: inst->storage.store = !inst->storage.store; break;
                        case 8U: ++inst->storage.relocation; break;
                        case 9U: ++inst->storage.addend; break;
                        case 10U: inst->bitfield.kind = (CinderMIRBitfieldKind)((unsigned)inst->bitfield.kind + 1U); break;
                        case 11U: inst->fixed_uses ^= 1U; break;
                        case 12U: inst->clobbers ^= 1U; break;
                        case 13U: inst->operands.left ^= 1U; break;
                        case 14U: inst->operands.right ^= 1U; break;
                        case 15U: inst->operands.op = IR_NOP; break;
                        default: return 2;
                    }
                    if (rejected(function, &allocation)) ++mutations; else failed = 1;
                    *inst = original;
                }
            }
        if (!failed) failed = cinder_lower_x86(function, &allocation, &object, false, NULL, &diags);
        cinder_alloc_destroy(&allocation);
    }
    if (!failed) failed = cinder_write_elf64(&object, argv[2], &diags);
    if (!failed) printf("{\"memory\":%u,\"bitfields\":%u,\"rejected\":%u,\"roles\":%u,\"bitfield_roles\":%u}\n", memory, bitfields, mutations, roles, bitfield_roles);
    if (failed) cinder_diag_print(&diags, &sources, stderr);
    cinder_machine_destroy(&object); cinder_ir_destroy(&module); cinder_sema_destroy(&sema); cinder_ast_destroy(&ast);
    cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources); return failed;
}
