#include "cinder.h"

#include <stdlib.h>
#include <string.h>

/* Phi transfers execute on one CFG edge. Split critical edges before physical
 * allocation so a transfer cannot affect the untaken branch or another input. */
static bool has_phi(const CinderIRBlock *block) {
    for (size_t i = 0U; i < block->instructions.len; ++i)
        if (block->instructions.data[i].op == IR_PHI) return true;
    return false;
}

int cinder_mir_boundary(CinderIRFunction *function, CinderDiagnostics *diags) {
    if (function == NULL || function->blocks.len == 0U) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "cannot lower an empty IR function to MIR");
        return 1;
    }
    size_t original = function->blocks.len;
    for (size_t b = 0U; b < original; ++b) {
        if (function->blocks.data[b].successors.len < 2U) continue;
        for (size_t s = 0U; s < function->blocks.data[b].successors.len; ++s) {
            CinderBlockId target = function->blocks.data[b].successors.data[s];
            if (target >= original) continue;
            CinderIRBlock *successor = &function->blocks.data[target];
            if (successor->predecessors.len < 2U || !has_phi(successor)) continue;
            CinderIRBlock edge;
            memset(&edge, 0, sizeof(edge)); edge.id = (CinderBlockId)function->blocks.len;
            char name[64]; int length = snprintf(name, sizeof(name), "edge.%zu.%u", b, target);
            if (length < 0 || (size_t)length >= sizeof(name)) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "edge name exceeds storage"); return 1; }
            edge.name = cinder_strndup(name, (size_t)length);
            edge.terminator.kind = TERM_JUMP; edge.terminator.target = target;
            edge.terminator.value = CINDER_INVALID_VALUE; edge.terminator.condition = CINDER_INVALID_VALUE;
            CinderBlockId source = (CinderBlockId)b;
            cinder_vec_push((CinderVec *)&edge.predecessors, &source);
            cinder_vec_push((CinderVec *)&edge.successors, &target);
            for (size_t p = 0U; p < successor->predecessors.len; ++p)
                if (successor->predecessors.data[p] == b) successor->predecessors.data[p] = edge.id;
            for (size_t i = 0U; i < successor->instructions.len; ++i) {
                CinderIRInst *phi = &successor->instructions.data[i];
                if (phi->op != IR_PHI) continue;
                for (size_t p = 0U; p < phi->phi_blocks.len; ++p)
                    if (phi->phi_blocks.data[p] == b) phi->phi_blocks.data[p] = edge.id;
            }
            CinderIRBlock *predecessor = &function->blocks.data[b];
            predecessor->successors.data[s] = edge.id;
            if (predecessor->terminator.yes == target) predecessor->terminator.yes = edge.id;
            if (predecessor->terminator.no == target) predecessor->terminator.no = edge.id;
            cinder_vec_push((CinderVec *)&function->blocks, &edge);
        }
    }
    return 0;
}
