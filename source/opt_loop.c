#include "opt_private.h"

#include <stdlib.h>
#include <string.h>

static bool dominates(const CinderCFGAnalysis *cfg, CinderBlockId definition, CinderBlockId block) {
    if (definition >= cfg->block_count || block >= cfg->block_count || !cfg->reachable[definition] || !cfg->reachable[block]) return false;
    for (size_t hops = 0U; hops < cfg->block_count; ++hops) {
        if (definition == block) return true;
        if (block == cfg->idom[block]) return false;
        block = cfg->idom[block];
    }
    return false;
}

/* Union the reverse predecessor closure of all backedges into one natural
 * loop. Every member must be dominated by the header. Reject side entries
 * rather than treating an irreducible region as a single-entry loop. */
static CinderBlockId natural_loop(const CinderIRFunction *function, const CinderCFGAnalysis *cfg, CinderBlockId header, bool *members, CinderBlockId *work) {
    memset(members, 0, function->blocks.len * sizeof(*members));
    members[header] = true;
    size_t length = 0U;
    const CinderIRBlock *entry = &function->blocks.data[header];
    for (size_t p = 0U; p < entry->predecessors.len; ++p) {
        CinderBlockId latch = entry->predecessors.data[p];
        if (!dominates(cfg, header, latch) || members[latch]) continue;
        members[latch] = true; work[length++] = latch;
    }
    while (length != 0U) {
        CinderBlockId block = work[--length];
        const CinderIRBlock *current = &function->blocks.data[block];
        for (size_t p = 0U; p < current->predecessors.len; ++p) {
            CinderBlockId predecessor = current->predecessors.data[p];
            if (!cfg->reachable[predecessor]) continue;
            if (!dominates(cfg, header, predecessor)) return CINDER_INVALID_BLOCK;
            if (!members[predecessor]) { members[predecessor] = true; work[length++] = predecessor; }
        }
    }
    CinderBlockId preheader = CINDER_INVALID_BLOCK;
    for (size_t p = 0U; p < entry->predecessors.len; ++p) {
        CinderBlockId predecessor = entry->predecessors.data[p];
        if (!cfg->reachable[predecessor] || members[predecessor]) continue;
        if (preheader != CINDER_INVALID_BLOCK) return CINDER_INVALID_BLOCK;
        preheader = predecessor;
    }
    if (preheader == CINDER_INVALID_BLOCK) return preheader;
    const CinderIRBlock *before = &function->blocks.data[preheader];
    if (before->terminator.kind != TERM_JUMP || before->terminator.target != header || before->successors.len != 1U) return CINDER_INVALID_BLOCK;
    return preheader;
}

static bool integer(const CinderType *type) {
    return type != NULL && ((type->kind >= TYPE_BOOL && type->kind <= TYPE_LLONG) || type->kind == TYPE_ENUM);
}

/* Only total scalar integer operations can execute on a zero-trip path.
 * No memory, floating, pointer, signed arithmetic, division or variable shift
 * may be speculated. Even a copy requires a defined input. */
static bool total(const CinderIRInst *inst, const bool *constant, const int64_t *values) {
    if (!integer(inst->type)) return false;
    if (inst->op == IR_CONST) return true;
    if (inst->op == IR_COPY) return true;
    if (inst->op == IR_CONVERT) return integer(inst->source_type);
    if (inst->op == IR_BIT_NOT || inst->op == IR_BIT_AND || inst->op == IR_BIT_OR || inst->op == IR_BIT_XOR) return true;
    if (inst->op >= IR_CMP_EQ && inst->op <= IR_CMP_GE_U) return integer(inst->source_type);
    if (inst->op == IR_ADD || inst->op == IR_SUB || inst->op == IR_MUL || inst->op == IR_NEG) return inst->type->is_unsigned;
    if (inst->op == IR_SHL || inst->op == IR_SHR_U) {
        if (!inst->type->is_unsigned || !constant[inst->right]) return false;
        int64_t amount = values[inst->right];
        return amount >= 0 && (uint64_t)amount < inst->type->size * 8U;
    }
    return false;
}

static void transfer(CinderIRInst *inst, CinderIRBlock *preheader) {
    cinder_vec_push((CinderVec *)&preheader->instructions, inst);
    CinderLoc loc = inst->loc; CinderType *type = inst->type;
    memset(inst, 0, sizeof(*inst)); inst->op = IR_NOP; inst->type = type; inst->loc = loc;
    inst->dst = CINDER_INVALID_VALUE; inst->left = CINDER_INVALID_VALUE; inst->right = CINDER_INVALID_VALUE; inst->slot = -1;
}

unsigned cinder_move_loop_invariants(CinderIRFunction *function, CinderDiagnostics *diags) {
    CinderCFGAnalysis cfg; cinder_cfg_init(&cfg);
    if (cinder_analyze_cfg(function, &cfg, diags) != 0) { cinder_cfg_destroy(&cfg); return 0U; }
    size_t count = function->value_count == 0U ? 1U : function->value_count;
    CinderBlockId *definitions = cinder_alloc(count * sizeof(*definitions));
    bool *constant = cinder_alloc(count * sizeof(*constant));
    int64_t *values = cinder_alloc(count * sizeof(*values));
    bool *defined = cinder_opt_defined_values(function);
    bool *members = cinder_alloc(function->blocks.len * sizeof(*members));
    CinderBlockId *work = cinder_alloc(function->blocks.len * sizeof(*work));
    memset(constant, 0, count * sizeof(*constant)); memset(values, 0, count * sizeof(*values));
    for (size_t v = 0U; v < count; ++v) definitions[v] = CINDER_INVALID_BLOCK;
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
            if (inst->dst == CINDER_INVALID_VALUE) continue;
            definitions[inst->dst] = (CinderBlockId)b;
            if (inst->op == IR_CONST && integer(inst->type)) { constant[inst->dst] = true; values[inst->dst] = cinder_opt_normalize(inst->integer, inst->type); }
        }
    unsigned changes = 0U;
    /* Inner headers usually occur later in RPO. Moving an inner invariant into
     * its preheader can expose an invariant of the enclosing natural loop. */
    for (size_t h = cfg.rpo_count; h > 0U; --h) {
        CinderBlockId header = cfg.rpo[h - 1U];
        if (!cfg.loop_header[header]) continue;
        CinderBlockId preheader = natural_loop(function, &cfg, header, members, work);
        if (preheader == CINDER_INVALID_BLOCK) continue;
        bool changed = true; unsigned rounds = 0U;
        while (changed && rounds++ < 4096U) {
            changed = false;
            for (size_t r = 0U; r < cfg.rpo_count; ++r) {
                CinderBlockId b = cfg.rpo[r];
                if (!members[b]) continue;
                CinderIRBlock *block = &function->blocks.data[b];
                for (size_t i = 0U; i < block->instructions.len; ++i) {
                    CinderIRInst *inst = &block->instructions.data[i];
                    if (inst->dst == CINDER_INVALID_VALUE || !total(inst, constant, values)) continue;
                    CinderValueId operands[2] = {inst->left, inst->right}; bool invariant = true;
                    for (size_t a = 0U; a < 2U; ++a) {
                        CinderValueId value = operands[a];
                        if (value == CINDER_INVALID_VALUE) continue;
                        CinderBlockId definition = definitions[value];
                        if (!defined[value] || definition == CINDER_INVALID_BLOCK || members[definition] || !dominates(&cfg, definition, preheader)) invariant = false;
                    }
                    if (!invariant) continue;
                    definitions[inst->dst] = preheader;
                    transfer(inst, &function->blocks.data[preheader]); ++changes; changed = true;
                }
            }
        }
    }
    free(work); free(members); free(defined); free(values); free(constant); free(definitions); cinder_cfg_destroy(&cfg);
    return changes;
}
