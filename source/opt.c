#include "cinder.h"

#include <limits.h>
#include <stdlib.h>

static bool fold(CinderIROp op, int64_t left, int64_t right, int64_t *out) {
    switch (op) {
        case IR_ADD: *out = left + right; return true;
        case IR_SUB: *out = left - right; return true;
        case IR_MUL: *out = left * right; return true;
        case IR_BIT_AND: *out = left & right; return true;
        case IR_BIT_OR: *out = left | right; return true;
        case IR_BIT_XOR: *out = left ^ right; return true;
        case IR_CMP_EQ: *out = left == right; return true;
        case IR_CMP_NE: *out = left != right; return true;
        case IR_CMP_LT_S: *out = left < right; return true;
        case IR_CMP_LE_S: *out = left <= right; return true;
        case IR_CMP_GT_S: *out = left > right; return true;
        case IR_CMP_GE_S: *out = left >= right; return true;
        case IR_DIV_S: if (right == 0 || (left == INT64_MIN && right == -1)) return false; *out = left / right; return true;
        case IR_MOD_S: if (right == 0 || (left == INT64_MIN && right == -1)) return false; *out = left % right; return true;
        default: return false;
    }
}

int cinder_optimize(CinderIRModule *module, int level, CinderOptStats *stats, CinderDiagnostics *diags) {
    (void)diags;
    stats->functions_changed = 0U; stats->instructions_changed = 0U; stats->constants_folded = 0U; stats->blocks_removed = 0U; stats->memory_forwarded = 0U; stats->dead_instructions_removed = 0U;
    if (level <= 0) return 0;
    for (size_t f = 0U; f < module->functions.len; ++f) {
        CinderIRFunction *function = &module->functions.data[f];
        int64_t *constant = cinder_alloc((function->value_count == 0U ? 1U : function->value_count) * sizeof(*constant));
        bool *known = cinder_alloc((function->value_count == 0U ? 1U : function->value_count) * sizeof(*known));
        for (size_t i = 0U; i < function->value_count; ++i) known[i] = false;
        bool changed_function = false;
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            CinderIRBlock *block = &function->blocks.data[b];
            for (size_t i = 0U; i < block->instructions.len; ++i) {
                CinderIRInst *inst = &block->instructions.data[i];
                if (inst->op == IR_CONST) { known[inst->dst] = true; constant[inst->dst] = inst->integer; continue; }
                if (inst->op == IR_COPY && known[inst->left]) { inst->op = IR_CONST; inst->integer = constant[inst->left]; inst->left = CINDER_INVALID_VALUE; known[inst->dst] = true; constant[inst->dst] = inst->integer; changed_function = true; stats->instructions_changed++; stats->constants_folded++; continue; }
                if (inst->right != CINDER_INVALID_VALUE && known[inst->left] && known[inst->right]) {
                    int64_t result = 0;
                    if (fold(inst->op, constant[inst->left], constant[inst->right], &result)) { inst->op = IR_CONST; inst->integer = result; inst->left = CINDER_INVALID_VALUE; inst->right = CINDER_INVALID_VALUE; known[inst->dst] = true; constant[inst->dst] = result; changed_function = true; stats->instructions_changed++; stats->constants_folded++; continue; }
                }
                if (inst->dst != CINDER_INVALID_VALUE) known[inst->dst] = false;
            }
        }
        if (changed_function) stats->functions_changed++;
        stats->memory_forwarded += cinder_forward_local_memory(function);
        stats->dead_instructions_removed += cinder_remove_dead_ir(function);
        stats->instructions_changed += stats->dead_instructions_removed;
        if (stats->memory_forwarded != 0U || stats->dead_instructions_removed != 0U) stats->functions_changed++;
        free(known); free(constant);
    }
    return 0;
}
