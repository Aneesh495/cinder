#include "cinder.h"

#include <stdlib.h>
#include <string.h>

/* Pruned iterated-dominance-frontier promotion of non-addressed scalar slots.
 * Renaming walks the dominator tree with an explicit undo stack. */
typedef struct { size_t slot; CinderValueId previous; } RenameUndo;
typedef struct { CinderBlockId block; size_t child; size_t undo_mark; bool entered; } RenameFrame;

static CinderIRInst instruction(CinderIROp op, CinderType *type, CinderValueId result, int slot) {
    CinderIRInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.op = op; inst.type = type; inst.dst = result;
    inst.left = CINDER_INVALID_VALUE; inst.right = CINDER_INVALID_VALUE; inst.slot = slot;
    return inst;
}

static void prepend(CinderIRBlock *block, const CinderIRInst *inst) {
    size_t previous = block->instructions.len;
    cinder_vec_push((CinderVec *)&block->instructions, inst);
    memmove(block->instructions.data + 1U, block->instructions.data, previous * sizeof(*inst));
    block->instructions.data[0] = *inst;
}

static bool promotable(const CinderType *type) {
    if (type == NULL || (type->qualifiers & 2U) != 0U) return false;
    return type->kind == TYPE_BOOL || type->kind == TYPE_CHAR || type->kind == TYPE_SHORT || type->kind == TYPE_INT || type->kind == TYPE_LONG || type->kind == TYPE_LLONG || type->kind == TYPE_ENUM || type->kind == TYPE_POINTER || cinder_ir_floating(type);
}

static void local_liveness(const CinderIRFunction *function, const CinderCFGAnalysis *cfg, size_t slot, bool *definitions, bool *input) {
    size_t count = function->blocks.len;
    bool *uses = cinder_alloc(count * sizeof(*uses));
    memset(definitions, 0, count * sizeof(*definitions));
    memset(input, 0, count * sizeof(*input));
    memset(uses, 0, count * sizeof(*uses));
    for (size_t b = 0U; b < count; ++b) {
        if (!cfg->reachable[b]) continue;
        const CinderIRBlock *block = &function->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderIRInst *inst = &block->instructions.data[i];
            if (inst->slot < 0 || (size_t)inst->slot != slot) continue;
            if (inst->op == IR_LOCAL_LOAD && !definitions[b]) uses[b] = true;
            if ((inst->op == IR_LOCAL_STORE || inst->op == IR_LOCAL_INIT) || inst->op == IR_LOCAL_BEGIN) definitions[b] = true;
        }
    }
    bool changed;
    do {
        changed = false;
        for (size_t ri = cfg->rpo_count; ri > 0U; --ri) {
            CinderBlockId b = cfg->rpo[ri - 1U];
            const CinderIRBlock *block = &function->blocks.data[b];
            bool next = uses[b];
            if (!definitions[b])
                for (size_t s = 0U; s < block->successors.len; ++s) next = next || input[block->successors.data[s]];
            if (next != input[b]) { input[b] = next; changed = true; }
        }
    } while (changed);
    free(uses);
}

static int insert_phis(CinderIRFunction *function, const CinderCFGAnalysis *cfg, const bool *eligible) {
    size_t count = function->blocks.len;
    bool *definitions = cinder_alloc(count * sizeof(*definitions));
    bool *live = cinder_alloc(count * sizeof(*live));
    bool *queued = cinder_alloc(count * sizeof(*queued));
    bool *placed = cinder_alloc(count * sizeof(*placed));
    CinderBlockId *work = cinder_alloc(count * sizeof(*work));
    int inserted = 0;
    for (size_t slot = 0U; slot < function->local_count; ++slot) {
        if (!eligible[slot]) continue;
        local_liveness(function, cfg, slot, definitions, live);
        memset(queued, 0, count * sizeof(*queued)); memset(placed, 0, count * sizeof(*placed));
        size_t begin = 0U, end = 0U;
        for (size_t b = 0U; b < count; ++b)
            if (definitions[b]) { work[end++] = (CinderBlockId)b; queued[b] = true; }
        while (begin < end) {
            CinderBlockId definition = work[begin++];
            const CinderBlockVec *frontier = &cfg->frontier[definition];
            for (size_t d = 0U; d < frontier->len; ++d) {
                CinderBlockId join = frontier->data[d];
                if (placed[join] || !live[join]) continue;
                CinderIRBlock *block = &function->blocks.data[join];
                CinderIRInst phi = instruction(IR_PHI, function->local_types.data[slot], (CinderValueId)function->value_count++, (int)slot);
                for (size_t p = 0U; p < block->predecessors.len; ++p) {
                    CinderValueId missing = CINDER_INVALID_VALUE;
                    cinder_vec_push((CinderVec *)&phi.args, &missing);
                    cinder_vec_push((CinderVec *)&phi.phi_blocks, &block->predecessors.data[p]);
                }
                prepend(block, &phi); placed[join] = true; ++inserted;
                if (!queued[join]) { work[end++] = join; queued[join] = true; }
            }
        }
    }
    free(work); free(placed); free(queued); free(live); free(definitions);
    return inserted;
}

static void rename_slots(CinderIRFunction *function, const CinderCFGAnalysis *cfg, const bool *eligible, CinderValueId *current) {
    CINDER_VEC_TYPE(RenameUndo) undo = {NULL, 0U, 0U};
    RenameFrame *stack = cinder_alloc(function->blocks.len * sizeof(*stack));
    size_t depth = 1U;
    stack[0] = (RenameFrame){0U, 0U, 0U, false};
    while (depth != 0U) {
        RenameFrame *frame = &stack[depth - 1U];
        CinderIRBlock *block = &function->blocks.data[frame->block];
        if (!frame->entered) {
            frame->entered = true; frame->undo_mark = undo.len;
            for (size_t i = 0U; i < block->instructions.len; ++i) {
                CinderIRInst *inst = &block->instructions.data[i];
                if (inst->slot < 0 || (size_t)inst->slot >= function->local_count || !eligible[inst->slot]) continue;
                size_t slot = (size_t)inst->slot;
                if (inst->op == IR_LOCAL_BEGIN) {
                    RenameUndo change = {slot, current[slot]}; cinder_vec_push((CinderVec *)&undo, &change);
                    inst->op = IR_UNDEF; inst->dst = (CinderValueId)function->value_count++; inst->slot = -1;
                    current[slot] = inst->dst; continue;
                }
                if (inst->op == IR_PHI || (inst->op == IR_LOCAL_STORE || inst->op == IR_LOCAL_INIT)) {
                    RenameUndo change = {slot, current[slot]};
                    cinder_vec_push((CinderVec *)&undo, &change);
                    current[slot] = inst->op == IR_PHI ? inst->dst : inst->left;
                    if ((inst->op == IR_LOCAL_STORE || inst->op == IR_LOCAL_INIT)) {
                        inst->op = IR_NOP; inst->left = CINDER_INVALID_VALUE; inst->slot = -1;
                    }
                } else if (inst->op == IR_LOCAL_LOAD) {
                    inst->op = IR_COPY; inst->left = current[slot]; inst->slot = -1;
                }
            }
            for (size_t s = 0U; s < block->successors.len; ++s) {
                CinderIRBlock *successor = &function->blocks.data[block->successors.data[s]];
                for (size_t i = 0U; i < successor->instructions.len; ++i) {
                    CinderIRInst *phi = &successor->instructions.data[i];
                    if (phi->op != IR_PHI) continue;
                    for (size_t p = 0U; p < phi->phi_blocks.len; ++p)
                        if (phi->phi_blocks.data[p] == frame->block) phi->args.data[p] = current[phi->slot];
                }
            }
        }
        const CinderBlockVec *children = &cfg->children[frame->block];
        if (frame->child < children->len) {
            CinderBlockId child = children->data[frame->child++];
            stack[depth++] = (RenameFrame){child, 0U, 0U, false};
        } else {
            while (undo.len > frame->undo_mark) {
                RenameUndo change = undo.data[--undo.len]; current[change.slot] = change.previous;
            }
            --depth;
        }
    }
    free(stack); free(undo.data);
}

int cinder_insert_join_phis(CinderIRFunction *function, CinderDiagnostics *diags) {
    if (function->local_count == 0U) return 0;
    CinderCFGAnalysis cfg; cinder_cfg_init(&cfg);
    if (cinder_analyze_cfg(function, &cfg, diags) != 0) { cinder_cfg_destroy(&cfg); return -1; }
    bool *eligible = cinder_alloc(function->local_count * sizeof(*eligible));
    CinderValueId *current = cinder_alloc(function->local_count * sizeof(*current));
    for (size_t slot = 0U; slot < function->local_count; ++slot) {
        eligible[slot] = promotable(function->local_types.data[slot]); current[slot] = CINDER_INVALID_VALUE;
    }
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
            if (inst->op == IR_LOCAL_ADDRESS && inst->slot >= 0 && (size_t)inst->slot < function->local_count) eligible[inst->slot] = false;
        }
    int inserted = insert_phis(function, &cfg, eligible);
    CinderIRBlock *entry = &function->blocks.data[0];
    for (size_t slot = 0U; slot < function->local_count; ++slot) {
        if (!eligible[slot]) continue;
        CinderIRInst undef = instruction(IR_UNDEF, function->local_types.data[slot], (CinderValueId)function->value_count++, -1);
        current[slot] = undef.dst; prepend(entry, &undef);
    }
    rename_slots(function, &cfg, eligible, current);
    /* Unreachable predecessor inputs have no dynamic observation. Bind them to
     * an explicit undef definition so the serialized SSA remains well formed. */
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            CinderIRInst *phi = &function->blocks.data[b].instructions.data[i];
            if (phi->op != IR_PHI) continue;
            for (size_t p = 0U; p < phi->args.len; ++p)
                if (phi->args.data[p] == CINDER_INVALID_VALUE) phi->args.data[p] = current[phi->slot];
        }
    free(current); free(eligible); cinder_cfg_destroy(&cfg);
    return inserted;
}
