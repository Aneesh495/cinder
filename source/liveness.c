#include "regalloc_private.h"

#include <stdlib.h>
#include <string.h>

bool cinder_live_has(const uint64_t *bits, CinderValueId value) {
    return (bits[value / 64U] & (UINT64_C(1) << (value % 64U))) != 0U;
}

static void include_value(uint64_t *bits, CinderValueId value, size_t count) {
    if ((size_t)value < count) bits[value / 64U] |= UINT64_C(1) << (value % 64U);
}

static void include_use(uint64_t *use, const uint64_t *def, CinderValueId value, size_t count) {
    if ((size_t)value < count && !cinder_live_has(def, value)) include_value(use, value, count);
}

void cinder_liveness_destroy(CinderLiveness *live) {
    free(live->use); free(live->def); free(live->in); free(live->out);
    free(live->begin); free(live->end);
    memset(live, 0, sizeof(*live));
}

int cinder_liveness_build(const CinderIRFunction *function, CinderLiveness *live, CinderDiagnostics *diags) {
    memset(live, 0, sizeof(*live));
    live->words = (function->value_count + 63U) / 64U;
    if (live->words == 0U) live->words = 1U;
    live->blocks = function->blocks.len;
    if (live->blocks > SIZE_MAX / live->words / sizeof(uint64_t)) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "liveness storage size overflow");
        return 1;
    }
    size_t bytes = live->words * live->blocks * sizeof(uint64_t);
    live->use = cinder_alloc(bytes); live->def = cinder_alloc(bytes);
    live->in = cinder_alloc(bytes); live->out = cinder_alloc(bytes);
    memset(live->use, 0, bytes); memset(live->def, 0, bytes);
    memset(live->in, 0, bytes); memset(live->out, 0, bytes);
    live->begin = cinder_alloc(live->blocks * sizeof(*live->begin));
    live->end = cinder_alloc(live->blocks * sizeof(*live->end));
    size_t position = 0U;
    for (size_t b = 0U; b < live->blocks; ++b) {
        const CinderIRBlock *block = &function->blocks.data[b];
        uint64_t *use = live->use + b * live->words;
        uint64_t *def = live->def + b * live->words;
        live->begin[b] = position;
        for (size_t i = 0U; i < block->instructions.len; ++i, ++position) {
            const CinderIRInst *inst = &block->instructions.data[i];
            if (inst->op != IR_PHI) {
                include_use(use, def, inst->left, function->value_count);
                include_use(use, def, inst->right, function->value_count);
                for (size_t a = 0U; a < inst->args.len; ++a)
                    include_use(use, def, inst->args.data[a], function->value_count);
            }
            include_value(def, inst->dst, function->value_count);
        }
        include_use(use, def, block->terminator.condition, function->value_count);
        include_use(use, def, block->terminator.value, function->value_count);
        live->end[b] = position++;
    }
    uint64_t *next = cinder_alloc(live->words * sizeof(*next));
    bool changed;
    do {
        changed = false;
        for (size_t bi = live->blocks; bi > 0U; --bi) {
            size_t b = bi - 1U;
            const CinderIRBlock *block = &function->blocks.data[b];
            memset(next, 0, live->words * sizeof(*next));
            for (size_t s = 0U; s < block->successors.len; ++s) {
                CinderBlockId target = block->successors.data[s];
                if (target >= live->blocks) {
                    cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "liveness found an invalid successor");
                    free(next); cinder_liveness_destroy(live); return 1;
                }
                const uint64_t *incoming = live->in + (size_t)target * live->words;
                for (size_t w = 0U; w < live->words; ++w) next[w] |= incoming[w];
                const CinderIRBlock *successor = &function->blocks.data[target];
                for (size_t i = 0U; i < successor->instructions.len; ++i) {
                    const CinderIRInst *phi = &successor->instructions.data[i];
                    if (phi->op != IR_PHI) continue;
                    for (size_t p = 0U; p < phi->phi_blocks.len && p < phi->args.len; ++p)
                        if (phi->phi_blocks.data[p] == b) include_value(next, phi->args.data[p], function->value_count);
                }
            }
            for (size_t w = 0U; w < live->words; ++w) {
                size_t index = b * live->words + w;
                uint64_t input = live->use[index] | (next[w] & ~live->def[index]);
                if (live->out[index] != next[w] || live->in[index] != input) changed = true;
                live->out[index] = next[w]; live->in[index] = input;
            }
        }
    } while (changed);
    free(next);
    return 0;
}
