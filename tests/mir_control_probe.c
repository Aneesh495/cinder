#include "cinder.h"
#include <stdio.h>

static bool rejected(CinderIRFunction *function, CinderAllocation *allocation, bool frame) {
    CinderDiagnostics diags; cinder_diags_init(&diags);
    bool invalid = (frame ? cinder_verify_frame(allocation, &diags) : cinder_verify_selected_mir(&allocation->machine, &diags)) != 0 && diags.errors != 0U;
    cinder_diags_destroy(&diags); cinder_diags_init(&diags);
    CinderMachineObject object; cinder_machine_init(&object);
    invalid = invalid && cinder_lower_x86(function, allocation, &object, false, NULL, &diags) != 0 && object.text.len == 0U && object.defined_symbols.len == 0U;
    cinder_machine_destroy(&object); cinder_diags_destroy(&diags); return invalid;
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
    CinderMachineObject object; cinder_machine_init(&object); unsigned controls = 0U, frames = 0U, mutations = 0U, roles = 0U;
    if (!failed) failed = cinder_lower_globals(&module, &object, &diags);
    for (size_t f = 0U; !failed && f < module.functions.len; ++f) {
        CinderIRFunction *function = &module.functions.data[f];
        if (cinder_mir_boundary(function, &diags) != 0) { failed = 1; break; }
        CinderAllocation allocation; cinder_alloc_init(&allocation, function);
        failed = cinder_allocate(&allocation, &diags) || cinder_verify_allocation(&allocation, &diags);
        for (size_t b = 0U; !failed && b < allocation.machine.blocks.len; ++b) {
            CinderMIRControl *control = &allocation.machine.blocks.data[b].control; CinderMIRControl original = *control;
            ++controls; roles |= 1U << (unsigned)control->kind;
            for (unsigned change = 0U; change < 12U; ++change) {
                switch (change) {
                    case 0U: control->kind = (CinderMIRControlKind)(((unsigned)control->kind + 1U) % 4U); break;
                    case 1U: control->bank = (CinderMIRBank)(((unsigned)control->bank + 1U) % 3U); break;
                    case 2U: control->value ^= 1U; break;
                    case 3U: control->prefix[0] ^= 1U; break;
                    case 4U: control->prefix_size = 99U; break;
                    case 5U: control->branches[0][0] ^= 1U; break;
                    case 6U: control->branch_sizes[0] = 99U; break;
                    case 7U: control->edge_count = 99U; break;
                    case 8U: control->targets[0] ^= 1U; break;
                    case 9U: control->targets[1] ^= 1U; break;
                    case 10U: control->fixed_uses ^= 1U; break;
                    case 11U: control->clobbers ^= 1U; break;
                    default: return 2;
                }
                if (rejected(function, &allocation, false)) ++mutations; else failed = 1;
                *control = original;
            }
        }
        CinderMIRFrame original = allocation.frame; ++frames;
        for (unsigned change = 0U; !failed && change < 18U; ++change) {
            CinderMIRFrame *frame = &allocation.frame;
            switch (change) {
                case 0U: ++frame->size; break;
                case 1U: ++frame->incoming_base; break;
                case 2U: frame->prologue[0] ^= 1U; break;
                case 3U: frame->prologue[10] ^= 1U; break;
                case 4U: frame->prologue_size = 99U; break;
                case 5U: frame->epilogue[0] ^= 1U; break;
                case 6U: frame->epilogue[1] ^= 1U; break;
                case 7U: frame->saved_count = 99U; break;
                case 8U: ++frame->saved[0].reg; break;
                case 9U: ++frame->saved[0].offset; break;
                case 10U: frame->saved[0].save[0] ^= 1U; break;
                case 11U: frame->saved[0].restore[0] ^= 1U; break;
                case 12U: ++frame->incoming_gpr[0]; break;
                case 13U: ++frame->incoming_gpr[5]; break;
                case 14U: ++frame->incoming_sse[0]; break;
                case 15U: ++frame->incoming_sse[7]; break;
                case 16U: ++frame->variadic_area; break;
                case 17U: frame->saved[3].restore[9] ^= 1U; break;
                default: return 2;
            }
            if (rejected(function, &allocation, true)) ++mutations; else failed = 1;
            allocation.frame = original;
        }
        if (!failed) failed = cinder_lower_x86(function, &allocation, &object, false, NULL, &diags);
        cinder_alloc_destroy(&allocation);
    }
    if (!failed) failed = cinder_write_elf64(&object, argv[2], &diags);
    if (!failed) printf("{\"controls\":%u,\"frames\":%u,\"rejected\":%u,\"roles\":%u}\n", controls, frames, mutations, roles);
    if (failed) cinder_diag_print(&diags, &sources, stderr);
    cinder_machine_destroy(&object); cinder_ir_destroy(&module); cinder_sema_destroy(&sema); cinder_ast_destroy(&ast);
    cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources); return failed;
}
