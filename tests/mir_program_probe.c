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
    CinderMachineObject object; cinder_machine_init(&object); unsigned programs = 0U, steps = 0U, byte_steps = 0U, patches = 0U, fixups = 0U, patch_lists = 0U, fixup_lists = 0U, mutations = 0U, roles = 0U, step_roles = 0U, patch_roles = 0U;
    if (!failed) failed = cinder_lower_globals(&module, &object, &diags);
    for (size_t f = 0U; !failed && f < module.functions.len; ++f) {
        CinderIRFunction *function = &module.functions.data[f];
        if (cinder_mir_boundary(function, &diags) != 0) { failed = 1; break; }
        CinderAllocation allocation; cinder_alloc_init(&allocation, function);
        failed = cinder_allocate(&allocation, &diags) || cinder_verify_allocation(&allocation, &diags);
        for (size_t b = 0U; !failed && b < allocation.machine.blocks.len; ++b)
            for (size_t i = 0U; !failed && i < allocation.machine.blocks.data[b].instructions.len; ++i) {
                CinderMIRInst *inst = &allocation.machine.blocks.data[b].instructions.data[i];
                if (inst->program == NULL) continue;
                ++programs; CinderMIRProgram *program = inst->program; roles |= 1U << ((unsigned)program->role - 1U);
                CinderMIRInst saved_inst = *inst; CinderMIRProgram saved_program = *program;
                for (unsigned change = 0U; change < 4U; ++change) {
                    if (change == 0U) inst->kind = MIR_PSEUDO;
                    else if (change == 1U) program->role = MIR_PROGRAM_NONE;
                    else if (change == 2U) program->steps.len ^= 1U;
                    else program->steps.data = NULL;
                    if (rejected(function, &allocation)) ++mutations; else failed = 1;
                    *inst = saved_inst; *program = saved_program;
                }
                for (size_t s = 0U; !failed && s < program->steps.len; ++s) {
                    CinderMIRProgramStep *step = &program->steps.data[s], original = *step; ++steps; step_roles |= 1U << (unsigned)step->kind;
                    for (unsigned change = 0U; change < 6U; ++change) {
                        if (change == 0U) step->kind = (CinderMIRProgramStepKind)99;
                        else if (change == 1U) step->value ^= 1U;
                        else if (change == 2U) step->reg ^= 1U;
                        else if (change == 3U) step->bytes.len ^= 1U;
                        else if (change == 4U) step->patches.len ^= 1U;
                        else step->fixups.len ^= 1U;
                        if (rejected(function, &allocation)) ++mutations; else failed = 1;
                        *step = original;
                    }
                    if (step->bytes.len != 0U) {
                        ++byte_steps; step->bytes.data[0] ^= 1U;
                        if (rejected(function, &allocation)) ++mutations; else failed = 1;
                        step->bytes.data[0] ^= 1U; step->bytes.data = NULL;
                        if (rejected(function, &allocation)) ++mutations; else failed = 1;
                        *step = original;
                    }
                    if (step->patches.len != 0U) {
                        ++patch_lists; step->patches.data = NULL;
                        if (rejected(function, &allocation)) ++mutations; else failed = 1;
                        *step = original;
                    }
                    for (size_t p = 0U; !failed && p < step->patches.len; ++p) {
                        CinderMIRFramePatch *patch = &step->patches.data[p], saved = *patch; ++patches; patch_roles |= 1U << (unsigned)patch->kind;
                        for (unsigned change = 0U; change < 4U; ++change) {
                            if (change == 0U) patch->kind = (CinderMIRFramePatchKind)99;
                            else if (change == 1U) ++patch->offset;
                            else if (change == 2U) patch->slot ^= 1;
                            else ++patch->addend;
                            if (rejected(function, &allocation)) ++mutations; else failed = 1;
                            *patch = saved;
                        }
                    }
                    if (step->fixups.len != 0U) {
                        ++fixup_lists; step->fixups.data = NULL;
                        if (rejected(function, &allocation)) ++mutations; else failed = 1;
                        *step = original;
                    }
                    for (size_t fix_index = 0U; !failed && fix_index < step->fixups.len; ++fix_index) {
                        CinderFixup *fix = &step->fixups.data[fix_index], saved = *fix; ++fixups;
                        for (unsigned change = 0U; change < 6U; ++change) {
                            if (change == 0U) ++fix->offset;
                            else if (change == 1U) ++fix->type;
                            else if (change == 2U) ++fix->addend;
                            else if (change == 3U) ++fix->section_kind;
                            else if (change == 4U) fix->symbol[0] ^= 1;
                            else fix->symbol = NULL;
                            if (rejected(function, &allocation)) ++mutations; else failed = 1;
                            if (change == 4U) fix->symbol[0] ^= 1;
                            *fix = saved;
                        }
                    }
                }
            }
        if (!failed) failed = cinder_lower_x86(function, &allocation, &object, false, NULL, &diags);
        cinder_alloc_destroy(&allocation);
    }
    if (!failed) failed = cinder_write_elf64(&object, argv[2], &diags);
    if (!failed) printf("{\"programs\":%u,\"steps\":%u,\"byte_steps\":%u,\"patches\":%u,\"fixups\":%u,\"patch_lists\":%u,\"fixup_lists\":%u,\"rejected\":%u,\"roles\":%u,\"step_roles\":%u,\"patch_roles\":%u}\n", programs, steps, byte_steps, patches, fixups, patch_lists, fixup_lists, mutations, roles, step_roles, patch_roles);
    if (failed) cinder_diag_print(&diags, &sources, stderr);
    cinder_machine_destroy(&object); cinder_ir_destroy(&module); cinder_sema_destroy(&sema); cinder_ast_destroy(&ast);
    cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources); return failed;
}
