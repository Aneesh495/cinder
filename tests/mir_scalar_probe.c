#include "cinder.h"
#include <stdlib.h>
#include <string.h>

static bool rejected(const CinderMIRFunction *machine) {
    CinderDiagnostics diags; cinder_diags_init(&diags);
    bool invalid = cinder_verify_selected_mir(machine, &diags) != 0 && diags.errors != 0U;
    cinder_diags_destroy(&diags); return invalid;
}

static unsigned access_profile(const CinderMIRInst *inst) {
    const CinderType *type = inst->operands.type;
    if (inst->kind == MIR_BITFIELD) return 0U;
    unsigned index = type->kind == TYPE_POINTER ? 11U : type->kind == TYPE_BOOL ? 8U : type->kind == TYPE_FLOAT ? 9U : type->kind == TYPE_DOUBLE ? 10U : type->size == 1U ? 0U : type->size == 2U ? 2U : type->size == 4U ? 4U : 6U;
    if (index < 8U && type->is_unsigned) ++index;
    return 1U << index;
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
    CinderIRModule module; cinder_ir_init(&module, &types); CinderOptStats stats;
    if (!failed) failed = cinder_lower_ir(&module, &ast, &diags) || cinder_verify_ir(&module, &diags) || cinder_optimize_source(&module, 0, NULL, &stats, &diags);
    CinderMachineObject object; cinder_machine_init(&object);
    unsigned memory = 0U, conversion = 0U, bitfield = 0U, unary = 0U, values = 0U, mutations = 0U, profile = 0U, conversions = 0U, addresses = 0U;
    if (!failed) failed = cinder_lower_globals(&module, &object, &diags);
    for (size_t f = 0U; !failed && f < module.functions.len; ++f) {
        CinderIRFunction *function = &module.functions.data[f];
        if (cinder_mir_boundary(function, &diags) != 0) { failed = 1; break; }
        CinderAllocation allocation; cinder_alloc_init(&allocation, function);
        failed = cinder_allocate(&allocation, &diags) || cinder_verify_allocation(&allocation, &diags);
        for (size_t b = 0U; !failed && b < allocation.machine.blocks.len; ++b)
            for (size_t i = 0U; !failed && i < allocation.machine.blocks.data[b].instructions.len; ++i) {
                CinderMIRInst *inst = &allocation.machine.blocks.data[b].instructions.data[i];
                CinderMIRInst original = *inst;
                if (inst->kind == MIR_MEMORY || inst->kind == MIR_BITFIELD) {
                    if (inst->kind == MIR_MEMORY) ++memory;
                    profile |= access_profile(inst);
                    CinderIROp op = inst->operands.op;
                    addresses |= op == IR_GLOBAL_LOAD || op == IR_GLOBAL_STORE ? 1U : op == IR_LOCAL_LOAD || op == IR_LOCAL_STORE || op == IR_LOCAL_INIT ? 2U : 4U;
                    for (unsigned change = 0U; change < 6U; ++change) {
                        if (change == 0U) inst->access.load[0] ^= 1U;
                        else if (change == 1U) inst->access.load_extended[0] ^= 1U;
                        else if (change == 2U) inst->access.store[0] ^= 1U;
                        else if (change == 3U) inst->access.store_extended[0] ^= 1U;
                        else if (change == 4U) inst->access.load_size = 99U;
                        else inst->access.floating = !inst->access.floating;
                        if (rejected(&allocation.machine)) ++mutations; else failed = 1;
                        *inst = original;
                    }
                }
                if (inst->kind == MIR_CONVERSION) {
                    ++conversion; conversions |= 1U << (unsigned)inst->conversion;
                    inst->conversion = (CinderMIRConversion)(((unsigned)inst->conversion + 1U) % 7U);
                    if (rejected(&allocation.machine)) ++mutations; else failed = 1; *inst = original;
                    inst->single_precision = !inst->single_precision;
                    if (rejected(&allocation.machine)) ++mutations; else failed = 1; *inst = original;
                }
                if (inst->kind == MIR_BITFIELD) {
                    ++bitfield;
                    for (unsigned change = 0U; change < 5U; ++change) {
                        if (change == 0U) inst->bitfield.mask ^= 1U;
                        else if (change == 1U) inst->bitfield.positioned_mask ^= 1U;
                        else if (change == 2U) ++inst->bitfield.begin;
                        else if (change == 3U) ++inst->bitfield.end;
                        else inst->bitfield.signed_shift ^= 1U;
                        if (rejected(&allocation.machine)) ++mutations; else failed = 1; *inst = original;
                    }
                }
                if (inst->kind == MIR_INTEGER_UNARY) {
                    ++unary; inst->encoding[0] ^= 1U;
                    if (rejected(&allocation.machine)) ++mutations; else failed = 1; *inst = original;
                    inst->encoding_size = 99U;
                    if (rejected(&allocation.machine)) ++mutations; else failed = 1; *inst = original;
                }
                if (inst->operands.dst != CINDER_INVALID_VALUE) {
                    CinderMIRValue *value = &allocation.machine.values[inst->operands.dst], original_value = *value; ++values;
                    for (unsigned change = 0U; change < 3U; ++change) {
                        if (change == 0U) value->normalization[0] ^= 1U;
                        else if (change == 1U) value->normalization_size = 99U;
                        else value->incoming_bool = !value->incoming_bool;
                        if (rejected(&allocation.machine)) ++mutations; else failed = 1; *value = original_value;
                    }
                }
            }
        if (!failed) failed = cinder_lower_x86(function, &allocation, &object, false, NULL, &diags);
        cinder_alloc_destroy(&allocation);
    }
    if (!failed) failed = cinder_write_elf64(&object, argv[2], &diags);
    if (!failed) printf("{\"memory\":%u,\"conversion\":%u,\"bitfield\":%u,\"unary\":%u,\"values\":%u,\"rejected\":%u,\"memory_profile\":%u,\"conversion_profile\":%u,\"addresses\":%u}\n", memory, conversion, bitfield, unary, values, mutations, profile, conversions, addresses);
    if (failed) cinder_diag_print(&diags, &sources, stderr);
    cinder_machine_destroy(&object); cinder_ir_destroy(&module); cinder_sema_destroy(&sema); cinder_ast_destroy(&ast);
    cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources); return failed;
}
