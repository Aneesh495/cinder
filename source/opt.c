#include "opt_private.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

int64_t cinder_opt_normalize(int64_t value, const CinderType *type) {
    uint64_t bits = (uint64_t)value;
    if (type->kind == TYPE_BOOL) return bits != 0U;
    unsigned width = (unsigned)(type->size * 8U);
    if (width < 64U) {
        uint64_t mask = (UINT64_C(1) << width) - 1U; bits &= mask;
        if (!type->is_unsigned && (bits & (UINT64_C(1) << (width - 1U))) != 0U) bits |= ~mask;
    }
    return bits <= (uint64_t)INT64_MAX ? (int64_t)bits : -1 - (int64_t)~bits;
}

static bool fold(const CinderIRInst *inst, int64_t left, int64_t right, int64_t *out) {
    CinderIROp op = inst->op;
    if (op != IR_ADD && op != IR_SUB && op != IR_MUL && op != IR_BIT_AND && op != IR_BIT_OR && op != IR_BIT_XOR && op != IR_CMP_EQ && op != IR_CMP_NE && op != IR_CMP_LT_S && op != IR_CMP_LE_S && op != IR_CMP_GT_S && op != IR_CMP_GE_S && op != IR_DIV_S && op != IR_MOD_S) return false;
    bool unsig = inst->type != NULL && inst->type->is_unsigned;
    const CinderType *operand = inst->source_type != NULL ? inst->source_type : inst->type;
    if (operand->kind == TYPE_POINTER) return false;
    unsigned width = (unsigned)(operand->size * 8U);
    int64_t minimum = width == 64U ? INT64_MIN : -(INT64_C(1) << (width - 1U));
    switch (op) {
        case IR_ADD:
            if (!unsig && ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right))) return false;
            *out = (int64_t)((uint64_t)left + (uint64_t)right); return true;
        case IR_SUB:
            if (!unsig && ((right < 0 && left > INT64_MAX + right) || (right > 0 && left < INT64_MIN + right))) return false;
            *out = (int64_t)((uint64_t)left - (uint64_t)right); return true;
        case IR_MUL:
            if (!unsig && left != 0 && right != 0) {
                if (left > 0) {
                    if ((right > 0 && left > INT64_MAX / right) || (right < 0 && right < INT64_MIN / left)) return false;
                } else {
                    if ((right > 0 && left < INT64_MIN / right) || (right < 0 && left < INT64_MAX / right)) return false;
                }
            }
            *out = (int64_t)((uint64_t)left * (uint64_t)right); return true;
        case IR_BIT_AND: *out = left & right; return true;
        case IR_BIT_OR: *out = left | right; return true;
        case IR_BIT_XOR: *out = left ^ right; return true;
        case IR_CMP_EQ: *out = left == right; return true;
        case IR_CMP_NE: *out = left != right; return true;
        case IR_CMP_LT_S: *out = left < right; return true;
        case IR_CMP_LE_S: *out = left <= right; return true;
        case IR_CMP_GT_S: *out = left > right; return true;
        case IR_CMP_GE_S: *out = left >= right; return true;
        case IR_DIV_S: if (right == 0 || (left == minimum && right == -1)) return false; *out = left / right; return true;
        case IR_MOD_S: if (right == 0 || (left == minimum && right == -1)) return false; *out = left % right; return true;
        default: return false;
    }
}

bool cinder_opt_integer_value(const CinderIRInst *inst, int64_t left, int64_t right, int64_t *out) {
    int64_t result;
    if (!fold(inst, left, right, &result)) return false;
    if (inst->type->size < 8U && !inst->type->is_unsigned) {
        unsigned width = (unsigned)(inst->type->size * 8U);
        if (result < -(INT64_C(1) << (width - 1U)) || result >= (INT64_C(1) << (width - 1U))) return false;
    }
    *out = cinder_opt_normalize(result, inst->type); return true;
}

static CinderValueId alias_of(CinderValueId value, const CinderValueId *aliases, size_t count) {
    if ((size_t)value >= count) return value;
    for (size_t hops = 0U; aliases[value] != value && hops < count; ++hops) value = aliases[value];
    return value;
}

static unsigned propagate_copies(CinderIRFunction *function) {
    size_t count = function->value_count;
    CinderValueId *aliases = cinder_alloc((count == 0U ? 1U : count) * sizeof(*aliases));
    for (size_t v = 0U; v < count; ++v) aliases[v] = (CinderValueId)v;
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
            if (inst->op == IR_COPY) aliases[inst->dst] = inst->left;
        }
    unsigned changes = 0U;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderIRBlock *block = &function->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst *inst = &block->instructions.data[i];
            CinderValueId left = alias_of(inst->left, aliases, count), right = alias_of(inst->right, aliases, count);
            if (left != inst->left) { inst->left = left; ++changes; }
            if (right != inst->right) { inst->right = right; ++changes; }
            for (size_t a = 0U; a < inst->args.len; ++a) {
                CinderValueId value = alias_of(inst->args.data[a], aliases, count);
                if (value != inst->args.data[a]) { inst->args.data[a] = value; ++changes; }
            }
        }
        CinderValueId value = alias_of(block->terminator.value, aliases, count), condition = alias_of(block->terminator.condition, aliases, count);
        if (value != block->terminator.value) { block->terminator.value = value; ++changes; }
        if (condition != block->terminator.condition) { block->terminator.condition = condition; ++changes; }
    }
    free(aliases); return changes;
}

unsigned cinder_fold_constants(CinderIRFunction *function, unsigned *folded) {
    *folded = 0U;
    int64_t *constant = cinder_alloc((function->value_count == 0U ? 1U : function->value_count) * sizeof(*constant));
    bool *known = cinder_alloc((function->value_count == 0U ? 1U : function->value_count) * sizeof(*known));
    for (size_t i = 0U; i < function->value_count; ++i) known[i] = false;
    unsigned copies = propagate_copies(function);
    unsigned changes = copies;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderIRBlock *block = &function->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst *inst = &block->instructions.data[i];
            if (inst->op == IR_CONST) { known[inst->dst] = inst->type->kind != TYPE_POINTER; constant[inst->dst] = cinder_opt_normalize(inst->integer, inst->type); continue; }
            if (inst->op == IR_COPY && known[inst->left]) { inst->op = IR_CONST; inst->integer = constant[inst->left]; inst->left = CINDER_INVALID_VALUE; known[inst->dst] = true; constant[inst->dst] = inst->integer; ++changes; ++*folded; continue; }
            if (inst->right != CINDER_INVALID_VALUE && known[inst->left] && known[inst->right]) {
                int64_t result = 0;
                if (fold(inst, constant[inst->left], constant[inst->right], &result)) {
                    if (inst->type->size < 8U) {
                        unsigned width = (unsigned)(inst->type->size * 8U);
                        if (inst->type->is_unsigned) result = (int64_t)((uint64_t)result & ((UINT64_C(1) << width) - 1U));
                        else if (result < -(INT64_C(1) << (width - 1U)) || result >= (INT64_C(1) << (width - 1U))) continue;
                    }
                    result = cinder_opt_normalize(result, inst->type);
                    inst->op = IR_CONST; inst->integer = result; inst->left = CINDER_INVALID_VALUE; inst->right = CINDER_INVALID_VALUE; known[inst->dst] = true; constant[inst->dst] = result; ++changes; ++*folded; continue; }
            }
            if (inst->dst != CINDER_INVALID_VALUE) known[inst->dst] = false;
        }
    }
    free(known); free(constant); return changes;
}
