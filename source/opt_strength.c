#include "opt_private.h"

#include <stdlib.h>
#include <string.h>

static bool integer_type(const CinderType *type) {
    return type != NULL && ((type->kind >= TYPE_BOOL && type->kind <= TYPE_LLONG) || type->kind == TYPE_ENUM);
}

/* Unsigned arithmetic is modulo its declared width. Multiplication and
 * division by a power of two use a shift strictly below that width; remainder
 * uses the low-bit mask. Every replacement still reads the original operand. */
unsigned cinder_reduce_strength(CinderIRFunction *function) {
    size_t original_count = function->value_count;
    size_t count = original_count == 0U ? 1U : original_count;
    bool *known = cinder_alloc(count * sizeof(*known)); memset(known, 0, count * sizeof(*known));
    uint64_t *values = cinder_alloc(count * sizeof(*values));
    CinderType **types = cinder_alloc(count * sizeof(*types));
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
            if (inst->op == IR_CONST && integer_type(inst->type)) {
                known[inst->dst] = true; values[inst->dst] = (uint64_t)cinder_opt_normalize(inst->integer, inst->type); types[inst->dst] = inst->type;
            }
        }
    unsigned changes = 0U;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderIRBlock *block = &function->blocks.data[b];
        CINDER_VEC_TYPE(CinderIRInst) output = {NULL, 0U, 0U};
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst inst = block->instructions.data[i];
            bool multiply = inst.op == IR_MUL;
            bool divide = inst.op == IR_DIV_U || inst.op == IR_DIV_S;
            bool remainder = inst.op == IR_MOD_U;
            if ((multiply || divide || remainder) && integer_type(inst.type) && inst.right < original_count && known[inst.right]) {
                uint64_t factor = values[inst.right];
                if (factor == 1U && (multiply || divide)) {
                    inst.op = IR_COPY; inst.right = CINDER_INVALID_VALUE; inst.source_type = NULL; inst.integer = 0; ++changes;
                } else if (inst.type->is_unsigned && factor != 0U && (factor & (factor - 1U)) == 0U && function->value_count < 1000000U) {
                    unsigned shift = 0U; uint64_t reduced = factor;
                    while (reduced > 1U) { reduced >>= 1U; ++shift; }
                    unsigned width = (unsigned)(inst.type->size * 8U);
                    if (shift < width && (multiply || inst.op == IR_DIV_U || remainder)) {
                        CinderIRInst constant; memset(&constant, 0, sizeof(constant));
                        constant.op = IR_CONST; constant.type = types[inst.right]; constant.dst = (CinderValueId)function->value_count++;
                        constant.left = CINDER_INVALID_VALUE; constant.right = CINDER_INVALID_VALUE; constant.slot = -1; constant.loc = inst.loc;
                        uint64_t literal = remainder ? factor - 1U : shift;
                        constant.integer = literal <= INT64_MAX ? (int64_t)literal : -1 - (int64_t)~literal;
                        cinder_vec_push((CinderVec *)&output, &constant);
                        inst.op = multiply ? IR_SHL : remainder ? IR_BIT_AND : IR_SHR_U;
                        inst.right = constant.dst; inst.integer = 0; ++changes;
                    }
                }
            }
            cinder_vec_push((CinderVec *)&output, &inst);
        }
        free(block->instructions.data); block->instructions.data = output.data; block->instructions.len = output.len; block->instructions.cap = output.cap;
    }
    free(known); free(values); free(types); return changes;
}
