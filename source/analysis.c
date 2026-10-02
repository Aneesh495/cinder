#include "cinder.h"

#include <stdlib.h>
#include <string.h>

static void visit_rpo(const CinderIRFunction *function, CinderBlockId block, bool *seen, CinderBlockId *postorder, size_t *length) {
    if (block >= function->blocks.len || seen[block]) return;
    seen[block] = true;
    const CinderIRBlock *current = &function->blocks.data[block];
    for (size_t i = 0U; i < current->successors.len; ++i) visit_rpo(function, current->successors.data[i], seen, postorder, length);
    postorder[(*length)++] = block;
}

void cinder_cfg_init(CinderCFGAnalysis *analysis) { memset(analysis, 0, sizeof(*analysis)); }

void cinder_cfg_destroy(CinderCFGAnalysis *analysis) {
    free(analysis->rpo); free(analysis->idom); free(analysis->reachable); free(analysis->loop_header); free(analysis->rpo_index);
    memset(analysis, 0, sizeof(*analysis));
}

static CinderBlockId intersect(const CinderCFGAnalysis *analysis, CinderBlockId a, CinderBlockId b) {
    while (a != b) {
        while (analysis->rpo_index[a] > analysis->rpo_index[b]) a = analysis->idom[a];
        while (analysis->rpo_index[b] > analysis->rpo_index[a]) b = analysis->idom[b];
    }
    return a;
}

static bool dominates(const CinderCFGAnalysis *analysis, CinderBlockId dominator, CinderBlockId block) {
    if (!analysis->reachable[dominator] || !analysis->reachable[block]) return false;
    CinderBlockId current = block;
    while (current != analysis->idom[current]) {
        if (current == dominator) return true;
        current = analysis->idom[current];
    }
    return current == dominator;
}

int cinder_analyze_cfg(const CinderIRFunction *function, CinderCFGAnalysis *analysis, CinderDiagnostics *diags) {
    cinder_cfg_destroy(analysis);
    analysis->block_count = function->blocks.len;
    if (analysis->block_count == 0U) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "cannot analyze a function without blocks");
        return 1;
    }
    size_t count = analysis->block_count;
    analysis->rpo = cinder_alloc(count * sizeof(*analysis->rpo));
    analysis->idom = cinder_alloc(count * sizeof(*analysis->idom));
    analysis->reachable = cinder_alloc(count * sizeof(*analysis->reachable));
    analysis->loop_header = cinder_alloc(count * sizeof(*analysis->loop_header));
    analysis->rpo_index = cinder_alloc(count * sizeof(*analysis->rpo_index));
    bool *seen = cinder_alloc(count * sizeof(*seen));
    CinderBlockId *postorder = cinder_alloc(count * sizeof(*postorder));
    memset(seen, 0, count * sizeof(*seen)); memset(analysis->reachable, 0, count * sizeof(*analysis->reachable)); memset(analysis->loop_header, 0, count * sizeof(*analysis->loop_header));
    size_t post_count = 0U;
    visit_rpo(function, 0U, seen, postorder, &post_count);
    analysis->rpo_count = post_count;
    for (size_t i = 0U; i < post_count; ++i) {
        analysis->rpo[i] = postorder[post_count - i - 1U];
        analysis->rpo_index[analysis->rpo[i]] = i;
        analysis->reachable[analysis->rpo[i]] = true;
        analysis->idom[analysis->rpo[i]] = CINDER_INVALID_BLOCK;
    }
    analysis->idom[0] = 0U;
    bool changed = true;
    unsigned rounds = 0U;
    while (changed && rounds++ < (unsigned)(count + 1U)) {
        changed = false;
        for (size_t ri = 1U; ri < post_count; ++ri) {
            CinderBlockId block = analysis->rpo[ri];
            CinderBlockId candidate = CINDER_INVALID_BLOCK;
            const CinderIRBlock *current = &function->blocks.data[block];
            for (size_t p = 0U; p < current->predecessors.len; ++p) {
                CinderBlockId predecessor = current->predecessors.data[p];
                if (predecessor < count && analysis->reachable[predecessor] && analysis->idom[predecessor] != CINDER_INVALID_BLOCK) candidate = candidate == CINDER_INVALID_BLOCK ? predecessor : intersect(analysis, candidate, predecessor);
            }
            if (candidate != CINDER_INVALID_BLOCK && analysis->idom[block] != candidate) { analysis->idom[block] = candidate; changed = true; }
        }
    }
    if (changed) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "dominator analysis did not converge for '%s'", function->name);
    for (size_t b = 0U; b < count; ++b) {
        if (!analysis->reachable[b]) analysis->idom[b] = CINDER_INVALID_BLOCK;
        const CinderIRBlock *block = &function->blocks.data[b];
        for (size_t s = 0U; s < block->successors.len; ++s) if (dominates(analysis, block->successors.data[s], (CinderBlockId)b)) analysis->loop_header[block->successors.data[s]] = true;
    }
    free(postorder); free(seen);
    return diags->errors == 0U ? 0 : 1;
}

void cinder_dump_cfg(const CinderIRFunction *function, const CinderCFGAnalysis *analysis, FILE *out) {
    fprintf(out, "cfg %s: rpo", function->name);
    for (size_t i = 0U; i < analysis->rpo_count; ++i) fprintf(out, " %u", analysis->rpo[i]);
    fputc('\n', out);
    for (size_t b = 0U; b < analysis->block_count; ++b) {
        const CinderIRBlock *block = &function->blocks.data[b];
        fprintf(out, "  block %u reachable=%s idom=%s", block->id, analysis->reachable[b] ? "yes" : "no", analysis->idom[b] == CINDER_INVALID_BLOCK ? "none" : function->blocks.data[analysis->idom[b]].name);
        if (analysis->loop_header[b]) fputs(" loop-header", out);
        fputs(" preds=", out); for (size_t i = 0U; i < block->predecessors.len; ++i) fprintf(out, "%u%s", block->predecessors.data[i], i + 1U == block->predecessors.len ? "" : ",");
        fputs(" succs=", out); for (size_t i = 0U; i < block->successors.len; ++i) fprintf(out, "%u%s", block->successors.data[i], i + 1U == block->successors.len ? "" : ",");
        fputc('\n', out);
    }
}

unsigned cinder_forward_local_memory(CinderIRFunction *function) {
    unsigned forwarded = 0U;
    if (function->local_count == 0U) return 0U;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderValueId *current = cinder_alloc(function->local_count * sizeof(*current));
        for (size_t i = 0U; i < function->local_count; ++i) current[i] = CINDER_INVALID_VALUE;
        CinderIRBlock *block = &function->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst *inst = &block->instructions.data[i];
            if (inst->op == IR_LOCAL_STORE && inst->slot >= 0 && (size_t)inst->slot < function->local_count) { current[inst->slot] = inst->left; continue; }
            if (inst->op == IR_LOCAL_LOAD && inst->slot >= 0 && (size_t)inst->slot < function->local_count && current[inst->slot] != CINDER_INVALID_VALUE) { inst->op = IR_COPY; inst->left = current[inst->slot]; inst->right = CINDER_INVALID_VALUE; inst->slot = -1; forwarded++; }
        }
        free(current);
    }
    return forwarded;
}

unsigned cinder_remove_dead_ir(CinderIRFunction *function) {
    unsigned removed = 0U;
    bool changed = true;
    while (changed) {
        changed = false;
        size_t count = function->value_count == 0U ? 1U : function->value_count;
        unsigned *uses = cinder_alloc(count * sizeof(*uses));
        memset(uses, 0, count * sizeof(*uses));
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            CinderIRBlock *block = &function->blocks.data[b];
            for (size_t i = 0U; i < block->instructions.len; ++i) {
                CinderIRInst *inst = &block->instructions.data[i];
                if (inst->left != CINDER_INVALID_VALUE && inst->left < function->value_count) uses[inst->left]++;
                if (inst->right != CINDER_INVALID_VALUE && inst->right < function->value_count) uses[inst->right]++;
                for (size_t a = 0U; a < inst->args.len; ++a) if (inst->args.data[a] < function->value_count) uses[inst->args.data[a]]++;
            }
            if (block->terminator.value != CINDER_INVALID_VALUE) uses[block->terminator.value]++;
            if (block->terminator.condition != CINDER_INVALID_VALUE) uses[block->terminator.condition]++;
        }
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            CinderIRBlock *block = &function->blocks.data[b];
            for (size_t i = 0U; i < block->instructions.len; ++i) {
                CinderIRInst *inst = &block->instructions.data[i];
                bool pure = inst->op == IR_CONST || inst->op == IR_COPY || inst->op == IR_ADD || inst->op == IR_SUB || inst->op == IR_MUL || inst->op == IR_NEG || inst->op == IR_BIT_NOT || inst->op == IR_BIT_AND || inst->op == IR_BIT_OR || inst->op == IR_BIT_XOR || (inst->op >= IR_CMP_EQ && inst->op <= IR_CMP_GE_U);
                if (pure && inst->dst != CINDER_INVALID_VALUE && inst->dst < function->value_count && uses[inst->dst] == 0U) { inst->op = IR_NOP; inst->dst = CINDER_INVALID_VALUE; inst->left = CINDER_INVALID_VALUE; inst->right = CINDER_INVALID_VALUE; removed++; changed = true; }
            }
        }
        free(uses);
    }
    return removed;
}

static bool block_has_phi_for_slot(const CinderIRBlock *block, int slot) {
    for (size_t i = 0U; i < block->instructions.len; ++i) if (block->instructions.data[i].op == IR_PHI && block->instructions.data[i].slot == slot) return true;
    return false;
}

int cinder_insert_join_phis(CinderIRFunction *function, CinderDiagnostics *diags) {
    (void)diags;
    int inserted = 0;
    if (function->local_count == 0U) return 0;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderIRBlock *block = &function->blocks.data[b];
        if (block->predecessors.len < 2U) continue;
        for (size_t slot = 0U; slot < function->local_count; ++slot) {
            if (block_has_phi_for_slot(block, (int)slot)) continue;
            CinderValueId *incoming = cinder_alloc(block->predecessors.len * sizeof(*incoming));
            bool complete = true;
            for (size_t p = 0U; p < block->predecessors.len; ++p) {
                CinderBlockId predecessor = block->predecessors.data[p];
                incoming[p] = CINDER_INVALID_VALUE;
                if (predecessor >= function->blocks.len) { complete = false; break; }
                CinderIRBlock *pred_block = &function->blocks.data[predecessor];
                for (size_t i = 0U; i < pred_block->instructions.len; ++i) {
                    CinderIRInst *inst = &pred_block->instructions.data[i];
                    if (inst->op == IR_LOCAL_STORE && inst->slot == (int)slot) incoming[p] = inst->left;
                }
                if (incoming[p] == CINDER_INVALID_VALUE) complete = false;
            }
            if (!complete) { free(incoming); continue; }
            CinderIRInst phi;
            memset(&phi, 0, sizeof(phi));
            phi.op = IR_PHI; phi.dst = (CinderValueId)function->value_count++; phi.left = CINDER_INVALID_VALUE; phi.right = CINDER_INVALID_VALUE; phi.slot = (int)slot;
            phi.args.data = NULL; phi.args.len = 0U; phi.args.cap = 0U; phi.phi_blocks.data = NULL; phi.phi_blocks.len = 0U; phi.phi_blocks.cap = 0U;
            for (size_t p = 0U; p < block->predecessors.len; ++p) { cinder_vec_push((CinderVec *)&phi.args, &incoming[p]); cinder_vec_push((CinderVec *)&phi.phi_blocks, &block->predecessors.data[p]); }
            CinderIRInst *old = block->instructions.data;
            CinderIRInst *new_data = cinder_alloc((block->instructions.len + 1U) * sizeof(*new_data));
            new_data[0] = phi;
            memcpy(new_data + 1U, old, block->instructions.len * sizeof(*old));
            free(old); block->instructions.data = new_data; block->instructions.len++;
            CinderValueId value = phi.dst;
            bool current = true;
            for (size_t i = 1U; i < block->instructions.len; ++i) {
                CinderIRInst *inst = &block->instructions.data[i];
                if (inst->op == IR_LOCAL_STORE && inst->slot == (int)slot) current = false;
                else if (current && inst->op == IR_LOCAL_LOAD && inst->slot == (int)slot) { inst->op = IR_COPY; inst->left = value; inst->right = CINDER_INVALID_VALUE; inst->slot = -1; }
            }
            free(incoming); inserted++;
        }
    }
    return inserted;
}
