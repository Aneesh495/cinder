#include "opt_private.h"

#include <stdlib.h>
#include <string.h>

static CinderValueId origin(const CinderValueId *aliases, size_t count, CinderValueId value) {
    for (size_t depth = 0U; value < count && aliases[value] != value; ++depth) {
        if (depth >= count) return CINDER_INVALID_VALUE;
        value = aliases[value];
    }
    return value;
}

static bool dominates(const CinderCFGAnalysis *cfg, CinderBlockId definition, CinderBlockId block) {
    if (definition >= cfg->block_count || !cfg->reachable[definition] || !cfg->reachable[block]) return false;
    for (size_t depth = 0U; depth < cfg->block_count; ++depth) {
        if (definition == block) return true;
        if (block == cfg->idom[block]) break;
        block = cfg->idom[block];
    }
    return false;
}

/* A phi transfers a value without reading it. Eliminate it by redirecting uses,
 * never by inserting an eager copy of a possibly indeterminate input. Copies
 * that can perform an indeterminate read retain their original instruction. */
unsigned cinder_cleanup_copies(CinderIRFunction *function, CinderDiagnostics *diags) {
    CinderCFGAnalysis cfg; cinder_cfg_init(&cfg);
    if (cinder_analyze_cfg(function, &cfg, diags) != 0) { cinder_cfg_destroy(&cfg); return 0U; }
    size_t count = function->value_count == 0U ? 1U : function->value_count;
    CinderValueId *aliases = cinder_alloc(count * sizeof(*aliases));
    CinderBlockId *blocks = cinder_alloc(count * sizeof(*blocks));
    size_t *positions = cinder_alloc(count * sizeof(*positions));
    CinderType **types = cinder_alloc(count * sizeof(*types));
    bool *defined = cinder_opt_defined_values(function);
    for (size_t v = 0U; v < count; ++v) { aliases[v] = (CinderValueId)v; blocks[v] = CINDER_INVALID_BLOCK; positions[v] = SIZE_MAX; types[v] = NULL; }
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
            if (inst->dst != CINDER_INVALID_VALUE) { blocks[inst->dst] = (CinderBlockId)b; positions[inst->dst] = i; types[inst->dst] = inst->type; }
        }
    bool changed = true;
    size_t rounds = 0U;
    while (changed && rounds++ < count) {
        changed = false;
        for (size_t r = 0U; r < cfg.rpo_count; ++r) {
            CinderBlockId b = cfg.rpo[r];
            for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
                const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
                if (inst->op != IR_COPY && inst->op != IR_PHI) continue;
                CinderValueId candidate = CINDER_INVALID_VALUE;
                if (inst->op == IR_COPY) candidate = origin(aliases, count, inst->left);
                else if (b != 0U && inst->args.len != 0U) {
                    bool equal = true;
                    for (size_t p = 0U; p < inst->args.len; ++p) {
                        CinderValueId value = origin(aliases, count, inst->args.data[p]);
                        if (value == inst->dst) continue;
                        if (candidate == CINDER_INVALID_VALUE) candidate = value;
                        else if (candidate != value) { equal = false; break; }
                    }
                    if (!equal) candidate = CINDER_INVALID_VALUE;
                }
                if (candidate >= function->value_count || candidate == inst->dst || !cinder_type_equal(inst->type, types[candidate])) continue;
                if (!dominates(&cfg, blocks[candidate], b)) continue;
                if (blocks[candidate] == b && (inst->op == IR_PHI || positions[candidate] >= i)) continue;
                if (aliases[inst->dst] != candidate) { aliases[inst->dst] = candidate; changed = true; }
            }
        }
    }
    unsigned removed = 0U;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderIRBlock *block = &function->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst *inst = &block->instructions.data[i];
            CinderValueId replacement = origin(aliases, count, inst->dst);
            if ((inst->op == IR_PHI || inst->op == IR_COPY) && replacement < function->value_count && replacement != inst->dst &&
                (inst->op == IR_PHI || defined[replacement])) {
                CinderLoc loc = inst->loc; CinderType *type = inst->type;
                free(inst->callee); free(inst->args.data); free(inst->arg_floats.data); free(inst->phi_blocks.data);
                memset(inst, 0, sizeof(*inst)); inst->op = IR_NOP; inst->type = type; inst->loc = loc; inst->slot = -1;
                inst->dst = CINDER_INVALID_VALUE; inst->left = CINDER_INVALID_VALUE; inst->right = CINDER_INVALID_VALUE; ++removed;
                continue;
            }
            inst->left = origin(aliases, count, inst->left); inst->right = origin(aliases, count, inst->right);
            for (size_t a = 0U; a < inst->args.len; ++a) inst->args.data[a] = origin(aliases, count, inst->args.data[a]);
        }
        block->terminator.value = origin(aliases, count, block->terminator.value);
        block->terminator.condition = origin(aliases, count, block->terminator.condition);
    }
    free(aliases); free(blocks); free(positions); free(types); free(defined); cinder_cfg_destroy(&cfg); return removed;
}
