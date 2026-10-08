#include "cinder.h"

#include <stdlib.h>
#include <string.h>

static void append_edge(CinderIRFunction *function, CinderBlockId from, CinderBlockId to) {
    cinder_vec_push((CinderVec *)&function->blocks.data[from].successors, &to);
    cinder_vec_push((CinderVec *)&function->blocks.data[to].predecessors, &from);
}

static bool connects(const CinderIRFunction *function, CinderBlockId from, CinderBlockId to) {
    if (from >= function->blocks.len) return false;
    const CinderTerminator *term = &function->blocks.data[from].terminator;
    return (term->kind == TERM_JUMP && term->target == to) || (term->kind == TERM_BRANCH && (term->yes == to || term->no == to));
}

static void rebuild_edges(CinderIRFunction *function) {
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        function->blocks.data[b].predecessors.len = 0U;
        function->blocks.data[b].successors.len = 0U;
    }
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        const CinderTerminator *term = &function->blocks.data[b].terminator;
        if (term->kind == TERM_JUMP) append_edge(function, (CinderBlockId)b, term->target);
        else if (term->kind == TERM_BRANCH) {
            append_edge(function, (CinderBlockId)b, term->yes);
            if (term->no != term->yes) append_edge(function, (CinderBlockId)b, term->no);
        }
    }
}

static void discard_block(CinderIRBlock *block) {
    for (size_t i = 0U; i < block->instructions.len; ++i) {
        CinderIRInst *inst = &block->instructions.data[i];
        free(inst->callee); free(inst->args.data); free(inst->arg_floats.data); free(inst->phi_blocks.data);
    }
    free(block->name); free(block->instructions.data); free(block->predecessors.data); free(block->successors.data);
}

/* Conditions are replaced only when an existing SSA constant proves the edge.
 * Uninitialized definitions, memory reads, and unknown calls are never used as
 * constants. A removed edge also removes its phi input before block remapping. */
unsigned cinder_simplify_cfg(CinderIRFunction *function, CinderDiagnostics *diags) {
    size_t count = function->value_count == 0U ? 1U : function->value_count;
    bool *known = cinder_alloc(count * sizeof(*known));
    int64_t *constants = cinder_alloc(count * sizeof(*constants));
    memset(known, 0, count * sizeof(*known));
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
            if (inst->op == IR_CONST && inst->dst < function->value_count && inst->type->kind != TYPE_POINTER) {
                unsigned width = (unsigned)(inst->type->size * 8U);
                uint64_t bits = (uint64_t)inst->integer;
                if (inst->type->kind != TYPE_BOOL && width < 64U) bits &= (UINT64_C(1) << width) - 1U;
                known[inst->dst] = true; constants[inst->dst] = bits != 0U;
            }
        }
    unsigned changes = 0U;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderTerminator *term = &function->blocks.data[b].terminator;
        if (term->kind != TERM_BRANCH) continue;
        bool constant = term->condition < function->value_count && known[term->condition];
        if (constant) {
            CinderBlockId target = constant && constants[term->condition] == 0 ? term->no : term->yes;
            term->kind = TERM_JUMP; term->target = target; term->condition = CINDER_INVALID_VALUE;
            term->yes = CINDER_INVALID_BLOCK; term->no = CINDER_INVALID_BLOCK; ++changes;
        }
    }
    free(known); free(constants);
    rebuild_edges(function);
    CinderCFGAnalysis cfg; cinder_cfg_init(&cfg);
    if (cinder_analyze_cfg(function, &cfg, diags) != 0) { cinder_cfg_destroy(&cfg); return changes; }
    CinderBlockId *mapping = cinder_alloc(function->blocks.len * sizeof(*mapping));
    size_t retained = 0U;
    for (size_t b = 0U; b < function->blocks.len; ++b) mapping[b] = cfg.reachable[b] ? (CinderBlockId)retained++ : CINDER_INVALID_BLOCK;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        if (!cfg.reachable[b]) continue;
        CinderIRBlock *block = &function->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst *phi = &block->instructions.data[i];
            if (phi->op != IR_PHI) continue;
            size_t used = 0U;
            for (size_t p = 0U; p < phi->args.len; ++p) {
                CinderBlockId predecessor = phi->phi_blocks.data[p];
                if (cfg.reachable[predecessor] && connects(function, predecessor, (CinderBlockId)b)) {
                    phi->args.data[used] = phi->args.data[p]; phi->phi_blocks.data[used++] = mapping[predecessor];
                } else ++changes;
            }
            phi->args.len = used; phi->phi_blocks.len = used;
        }
    }
    CinderIRBlock *blocks = cinder_alloc(retained * sizeof(*blocks));
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderIRBlock *block = &function->blocks.data[b];
        if (!cfg.reachable[b]) { discard_block(block); ++changes; continue; }
        CinderBlockId index = mapping[b]; block->id = index;
        if (block->terminator.kind == TERM_JUMP) block->terminator.target = mapping[block->terminator.target];
        else if (block->terminator.kind == TERM_BRANCH) { block->terminator.yes = mapping[block->terminator.yes]; block->terminator.no = mapping[block->terminator.no]; }
        blocks[index] = *block;
    }
    free(function->blocks.data); function->blocks.data = blocks; function->blocks.len = retained; function->blocks.cap = retained;
    free(mapping); cinder_cfg_destroy(&cfg); rebuild_edges(function);
    return changes;
}
