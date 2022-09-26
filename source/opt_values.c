#include "opt_private.h"

#include <stdlib.h>

typedef struct { CinderIRInst *inst; CinderBlockId block; size_t position; } Candidate;

static CinderValueId origin(const CinderValueId *aliases, size_t count, CinderValueId value) {
    for (size_t depth = 0U; value < count && aliases[value] != value && depth < count; ++depth) value = aliases[value];
    return value;
}

static bool pure_integer(const CinderIRInst *inst) {
    if (inst->type == NULL || inst->type->kind == TYPE_POINTER || cinder_ir_floating(inst->type)) return false;
    if (inst->source_type != NULL && (inst->source_type->kind == TYPE_POINTER || cinder_ir_floating(inst->source_type))) return false;
    return inst->op == IR_CONST || inst->op == IR_ADD || inst->op == IR_SUB || inst->op == IR_MUL || inst->op == IR_NEG || inst->op == IR_BIT_NOT ||
        (inst->op >= IR_BIT_AND && inst->op <= IR_CMP_GE_U) || (inst->op >= IR_DIV_S && inst->op <= IR_MOD_U) || inst->op == IR_CONVERT || inst->op == IR_BIT_CONVERT;
}

static bool equivalent(const CinderIRInst *left, const CinderIRInst *right) {
    if (left->op != right->op || !cinder_type_equal(left->type, right->type) || !cinder_type_equal(left->source_type, right->source_type) || left->left != right->left || left->right != right->right || left->operator_code != right->operator_code) return false;
    if (left->op == IR_CONST) return cinder_opt_normalize(left->integer, left->type) == cinder_opt_normalize(right->integer, right->type);
    return left->integer == right->integer;
}

static bool dominates(const CinderCFGAnalysis *cfg, const Candidate *candidate, CinderBlockId block, size_t position) {
    if (candidate->block == block) return candidate->position < position;
    for (size_t depth = 0U; depth < cfg->block_count && block != cfg->idom[block]; ++depth) {
        block = cfg->idom[block];
        if (block == candidate->block) return true;
    }
    return false;
}

/* The value table contains only pure integer computations. Reuse requires
 * actual CFG dominance, including instruction order within the same block.
 * Memory, pointers, floating operations, and calls never enter the table. */
unsigned cinder_number_values(CinderIRFunction *function, CinderDiagnostics *diags) {
    CinderCFGAnalysis cfg; cinder_cfg_init(&cfg);
    if (cinder_analyze_cfg(function, &cfg, diags) != 0) { cinder_cfg_destroy(&cfg); return 0U; }
    CINDER_VEC_TYPE(Candidate) candidates = {NULL, 0U, 0U};
    size_t count = function->value_count == 0U ? 1U : function->value_count;
    CinderValueId *aliases = cinder_alloc(count * sizeof(*aliases));
    for (size_t v = 0U; v < count; ++v) aliases[v] = (CinderValueId)v;
    unsigned changes = 0U;
    for (size_t r = 0U; r < cfg.rpo_count; ++r) {
        CinderBlockId block_id = cfg.rpo[r]; CinderIRBlock *block = &function->blocks.data[block_id];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst *inst = &block->instructions.data[i];
            inst->left = origin(aliases, count, inst->left); inst->right = origin(aliases, count, inst->right);
            if (inst->op == IR_COPY) { aliases[inst->dst] = inst->left; continue; }
            if (inst->dst == CINDER_INVALID_VALUE || !pure_integer(inst)) continue;
            Candidate *match = NULL;
            /* Bound table work on very large functions; a missed expression
             * opportunity leaves the original computation intact. */
            for (size_t p = candidates.len; p > 0U && candidates.len - p < 4096U; --p) {
                Candidate *candidate = &candidates.data[p - 1U];
                if (equivalent(candidate->inst, inst) && dominates(&cfg, candidate, block_id, i)) { match = candidate; break; }
            }
            if (match == NULL) { Candidate candidate = {inst, block_id, i}; cinder_vec_push((CinderVec *)&candidates, &candidate); continue; }
            CinderValueId value = match->inst->dst;
            free(inst->callee); free(inst->args.data); free(inst->arg_floats.data); free(inst->phi_blocks.data);
            inst->op = IR_COPY; inst->left = value; inst->right = CINDER_INVALID_VALUE; inst->source_type = NULL;
            inst->callee = NULL; inst->args.data = NULL; inst->args.len = 0U; inst->args.cap = 0U;
            inst->arg_floats.data = NULL; inst->arg_floats.len = 0U; inst->arg_floats.cap = 0U;
            inst->phi_blocks.data = NULL; inst->phi_blocks.len = 0U; inst->phi_blocks.cap = 0U;
            inst->integer = 0; inst->floating = 0.0; inst->slot = -1; inst->operator_code = 0; ++changes;
            aliases[inst->dst] = value;
        }
    }
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderIRBlock *block = &function->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst *inst = &block->instructions.data[i];
            inst->left = origin(aliases, count, inst->left); inst->right = origin(aliases, count, inst->right);
            for (size_t a = 0U; a < inst->args.len; ++a) inst->args.data[a] = origin(aliases, count, inst->args.data[a]);
        }
        block->terminator.value = origin(aliases, count, block->terminator.value);
        block->terminator.condition = origin(aliases, count, block->terminator.condition);
    }
    free(aliases); free(candidates.data); cinder_cfg_destroy(&cfg); return changes;
}
