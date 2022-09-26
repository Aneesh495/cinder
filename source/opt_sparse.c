#include "opt_private.h"

#include <stdlib.h>
#include <string.h>

typedef enum { FACT_UNKNOWN, FACT_CONSTANT, FACT_VARYING } FactKind;
typedef struct { FactKind kind; int64_t value; } Fact;

static Fact join(Fact left, Fact right) {
    if (left.kind == FACT_UNKNOWN) return right;
    if (right.kind == FACT_UNKNOWN) return left;
    if (left.kind == FACT_CONSTANT && right.kind == FACT_CONSTANT && left.value == right.value) return left;
    return (Fact){FACT_VARYING, 0};
}

static bool integer_value(const CinderType *type) {
    return type != NULL && ((type->kind >= TYPE_BOOL && type->kind <= TYPE_LLONG) || type->kind == TYPE_ENUM);
}

static bool edge_live(const CinderIRFunction *function, const unsigned char *edges, CinderBlockId from, CinderBlockId to) {
    const CinderTerminator *term = &function->blocks.data[from].terminator;
    if (term->kind == TERM_JUMP) return (edges[from] & 1U) != 0U && term->target == to;
    return term->kind == TERM_BRANCH && (((edges[from] & 1U) != 0U && term->yes == to) || ((edges[from] & 2U) != 0U && term->no == to));
}

static Fact instruction_fact(const CinderIRFunction *function, const CinderIRInst *inst, const Fact *facts, const unsigned char *edges, CinderBlockId block) {
    if (inst->op == IR_CONST && integer_value(inst->type)) return (Fact){FACT_CONSTANT, cinder_opt_normalize(inst->integer, inst->type)};
    if (inst->op == IR_UNDEF) return (Fact){FACT_VARYING, 0};
    if (inst->op == IR_PHI) {
        Fact result = {FACT_UNKNOWN, 0};
        for (size_t p = 0U; p < inst->args.len; ++p)
            if (edge_live(function, edges, inst->phi_blocks.data[p], block)) result = join(result, facts[inst->args.data[p]]);
        return result;
    }
    if (inst->op == IR_COPY) return facts[inst->left];
    if (inst->op == IR_CONVERT && integer_value(inst->type) && integer_value(inst->source_type)) {
        Fact result = facts[inst->left];
        if (result.kind == FACT_CONSTANT) result.value = cinder_opt_normalize(result.value, inst->type);
        return result;
    }
    if (inst->left != CINDER_INVALID_VALUE && inst->right != CINDER_INVALID_VALUE && integer_value(inst->type) && integer_value(inst->source_type == NULL ? inst->type : inst->source_type)) {
        Fact left = facts[inst->left], right = facts[inst->right];
        if (left.kind == FACT_UNKNOWN || right.kind == FACT_UNKNOWN) return (Fact){FACT_UNKNOWN, 0};
        int64_t value;
        if (left.kind == FACT_CONSTANT && right.kind == FACT_CONSTANT && cinder_opt_integer_value(inst, left.value, right.value, &value)) return (Fact){FACT_CONSTANT, value};
    }
    return (Fact){FACT_VARYING, 0};
}

/* Executable edges and SSA facts grow monotonically. Indeterminate SSA values
 * are varying, never the lattice's not-yet-observed state. The bounded analysis
 * leaves the function unchanged if its fixed point is not reached. */
unsigned cinder_sparse_constants(CinderIRFunction *function, CinderDiagnostics *diags) {
    size_t count = function->value_count == 0U ? 1U : function->value_count;
    Fact *facts = cinder_alloc(count * sizeof(*facts)); memset(facts, 0, count * sizeof(*facts));
    bool *live = cinder_alloc(function->blocks.len * sizeof(*live)); memset(live, 0, function->blocks.len * sizeof(*live));
    unsigned char *edges = cinder_alloc(function->blocks.len); memset(edges, 0, function->blocks.len);
    live[0] = true;
    bool changed = true;
    size_t rounds = 0U, budget = 4096U;
    while (changed && rounds++ < budget) {
        changed = false;
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            if (!live[b]) continue;
            CinderIRBlock *block = &function->blocks.data[b];
            for (size_t i = 0U; i < block->instructions.len; ++i) {
                const CinderIRInst *inst = &block->instructions.data[i];
                if (inst->dst == CINDER_INVALID_VALUE) continue;
                Fact result = join(facts[inst->dst], instruction_fact(function, inst, facts, edges, (CinderBlockId)b));
                if (result.kind != facts[inst->dst].kind || (result.kind == FACT_CONSTANT && result.value != facts[inst->dst].value)) { facts[inst->dst] = result; changed = true; }
            }
            const CinderTerminator *term = &block->terminator;
            unsigned char next = 0U;
            if (term->kind == TERM_JUMP) next = 1U;
            else if (term->kind == TERM_BRANCH) {
                Fact condition = facts[term->condition];
                if (condition.kind == FACT_CONSTANT) next = condition.value == 0 ? 2U : 1U;
                else if (condition.kind == FACT_VARYING) next = 3U;
            }
            unsigned char added = (unsigned char)(next & (unsigned char)~edges[b]);
            if (added == 0U) continue;
            edges[b] |= added; changed = true;
            if ((added & 1U) != 0U) live[term->kind == TERM_JUMP ? term->target : term->yes] = true;
            if ((added & 2U) != 0U) live[term->no] = true;
        }
    }
    unsigned changes = 0U;
    if (!changed) {
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            if (!live[b]) continue;
            CinderIRBlock *block = &function->blocks.data[b];
            for (size_t i = 0U; i < block->instructions.len; ++i) {
                CinderIRInst *inst = &block->instructions.data[i];
                if (inst->dst == CINDER_INVALID_VALUE || inst->op == IR_CONST || facts[inst->dst].kind != FACT_CONSTANT || !integer_value(inst->type)) continue;
                /* Calls, memory reads and float operations stay varying. */
                free(inst->callee); free(inst->args.data); free(inst->arg_floats.data); free(inst->phi_blocks.data);
                CinderType *type = inst->type; CinderValueId dst = inst->dst; CinderLoc loc = inst->loc;
                memset(inst, 0, sizeof(*inst)); inst->op = IR_CONST; inst->type = type; inst->dst = dst; inst->loc = loc;
                inst->integer = facts[dst].value; inst->left = CINDER_INVALID_VALUE; inst->right = CINDER_INVALID_VALUE; inst->slot = -1; ++changes;
            }
        }
    }
    free(facts); free(live); free(edges);
    if (changes != 0U) changes += cinder_simplify_cfg(function, diags);
    return changes;
}
