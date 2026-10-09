#include "opt_private.h"

#include <stdlib.h>
#include <string.h>

typedef struct { CinderValueId address, value; CinderType *type; bool stored; } MemoryFact;

static bool integer_scalar(const CinderType *type) {
    return type != NULL && ((type->kind >= TYPE_BOOL && type->kind <= TYPE_LLONG) || type->kind == TYPE_ENUM);
}

static bool volatile_object(const CinderType *type, unsigned depth) {
    if (type == NULL || depth >= 64U || (type->qualifiers & 2U) != 0U) return true;
    if (type->kind == TYPE_ARRAY) return volatile_object(type->base, depth + 1U);
    if (type->kind == TYPE_STRUCT || type->kind == TYPE_UNION)
        for (size_t f = 0U; f < type->fields.len; ++f)
            if (volatile_object(type->fields.data[f].type, depth + 1U)) return true;
    return false;
}

static CinderValueId origin(const CinderValueId *aliases, size_t count, CinderValueId value) {
    for (size_t hops = 0U; value < count && aliases[value] != value && hops < count; ++hops) value = aliases[value];
    return value;
}

static void copy_value(CinderIRInst *inst, CinderValueId value) {
    inst->op = IR_COPY; inst->left = value; inst->right = CINDER_INVALID_VALUE;
    inst->source_type = NULL; inst->slot = -1; inst->integer = 0; inst->operator_code = 0;
}

static void remove_store(CinderIRInst *inst) {
    CinderType *type = inst->type; CinderLoc loc = inst->loc;
    free(inst->callee); free(inst->args.data); free(inst->arg_floats.data); free(inst->phi_blocks.data);
    memset(inst, 0, sizeof(*inst)); inst->type = type; inst->loc = loc; inst->op = IR_NOP;
    inst->slot = -1; inst->dst = CINDER_INVALID_VALUE; inst->left = CINDER_INVALID_VALUE; inst->right = CINDER_INVALID_VALUE;
}

static void forget_locals(CinderValueId *local, size_t count) {
    for (size_t l = 0U; l < count; ++l) local[l] = CINDER_INVALID_VALUE;
}

static bool barrier(CinderIROp op) {
    return op == IR_CALL || op == IR_LOCAL_BEGIN || op == IR_LOCAL_END || op == IR_LOCAL_RESET || op == IR_LOCAL_FREEZE ||
        op == IR_GLOBAL_LOAD || op == IR_GLOBAL_STORE || op == IR_ZERO_INIT || op == IR_OBJECT_COPY || op == IR_OBJECT_INIT ||
        op == IR_AGG_ARG || op == IR_AGG_RETURN || op == IR_VA_START || op == IR_VA_COPY || op == IR_VA_END || op == IR_VA_ARG ||
        op == IR_BIT_LOAD || op == IR_BIT_STORE || op == IR_BIT_INIT;
}

/* Facts stay within a block. Known addresses originate in an actually declared,
 * nonvolatile local object. Arbitrary/argument/global pointer roots, floating
 * storage, volatile accesses, calls, lifetime changes, and object/bit/variadic
 * effects form barriers. A retained first access guards every later reuse. */
unsigned cinder_forward_local_memory(CinderIRFunction *function) {
    size_t values = function->value_count == 0U ? 1U : function->value_count;
    size_t locals = function->local_count == 0U ? 1U : function->local_count;
    CinderValueId *aliases = cinder_alloc(values * sizeof(*aliases));
    CinderType **types = cinder_alloc(values * sizeof(*types));
    bool *known_local = cinder_alloc(values * sizeof(*known_local));
    bool *addressed = cinder_alloc(locals * sizeof(*addressed));
    memset(types, 0, values * sizeof(*types)); memset(known_local, 0, values * sizeof(*known_local)); memset(addressed, 0, locals * sizeof(*addressed));
    for (size_t v = 0U; v < values; ++v) aliases[v] = (CinderValueId)v;
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
            if (inst->dst != CINDER_INVALID_VALUE) types[inst->dst] = inst->type;
            if (inst->op == IR_COPY) aliases[inst->dst] = inst->left;
            if (inst->op == IR_LOCAL_ADDRESS) addressed[inst->slot] = true;
        }
    bool changed = true;
    unsigned rounds = 0U;
    while (changed && rounds++ < 4096U) {
        changed = false;
        for (size_t b = 0U; b < function->blocks.len; ++b)
            for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
                const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
                if (inst->dst == CINDER_INVALID_VALUE || inst->type->kind != TYPE_POINTER || known_local[inst->dst] || (inst->type->base->qualifiers & 2U) != 0U) continue;
                bool safe = false;
                if (inst->op == IR_LOCAL_ADDRESS) safe = !volatile_object(function->local_types.data[inst->slot], 0U);
                else if (inst->op == IR_COPY || inst->op == IR_POINTER_MEMBER || inst->op == IR_POINTER_OFFSET ||
                         (inst->op == IR_CONVERT && inst->source_type->kind == TYPE_POINTER)) safe = known_local[inst->left];
                else if (inst->op == IR_PHI && inst->args.len != 0U) {
                    safe = true;
                    for (size_t p = 0U; p < inst->args.len; ++p) if (!known_local[inst->args.data[p]]) safe = false;
                }
                if (safe) { known_local[inst->dst] = true; changed = true; }
            }
    }
    unsigned changes = 0U;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderValueId *local = cinder_alloc(locals * sizeof(*local));
        forget_locals(local, locals);
        MemoryFact facts[256]; size_t length = 0U;
        CinderIRBlock *block = &function->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst *inst = &block->instructions.data[i];
            if (barrier(inst->op)) {
                length = 0U;
                forget_locals(local, locals);
                continue;
            }
            if (inst->op == IR_LOCAL_LOAD || inst->op == IR_LOCAL_STORE || inst->op == IR_LOCAL_INIT) {
                size_t slot = (size_t)inst->slot;
                if (addressed[slot] || !integer_scalar(inst->type) || (function->local_types.data[slot]->qualifiers & 2U) != 0U) {
                    length = 0U; forget_locals(local, locals); continue;
                }
                if (inst->op != IR_LOCAL_LOAD) { local[slot] = origin(aliases, values, inst->left); length = 0U; }
                else if (local[slot] != CINDER_INVALID_VALUE) { CinderValueId value = local[slot]; copy_value(inst, value); aliases[inst->dst] = value; ++changes; }
                else local[slot] = inst->dst;
                continue;
            }
            bool load = inst->op == IR_MEMORY_LOAD;
            bool store = inst->op == IR_MEMORY_STORE;
            bool initialize = inst->op == IR_MEMORY_INIT;
            if (!load && !store && !initialize) continue;
            CinderValueId address = origin(aliases, values, inst->left);
            bool safe = address < function->value_count && known_local[address] && integer_scalar(inst->type) &&
                (inst->type->qualifiers & 2U) == 0U && (types[inst->left]->base->qualifiers & 2U) == 0U;
            if (!safe) { length = 0U; forget_locals(local, locals); continue; }
            size_t match = SIZE_MAX;
            for (size_t f = 0U; f < length; ++f)
                if (facts[f].address == address && cinder_type_equal(facts[f].type, inst->type)) { match = f; break; }
            if (load) {
                if (match != SIZE_MAX) { CinderValueId value = facts[match].value; copy_value(inst, value); aliases[inst->dst] = value; ++changes; }
                else {
                    if (length == 256U) length = 0U;
                    facts[length++] = (MemoryFact){address, inst->dst, inst->type, false};
                }
            } else {
                CinderValueId value = origin(aliases, values, inst->right);
                if (store && match != SIZE_MAX && facts[match].stored && facts[match].value == value) { remove_store(inst); ++changes; continue; }
                /* Any real write can alias every existing entry. Initializing
                 * const storage does not prove that a later ordinary store is
                 * allowed, so only a retained ordinary store guards removal. */
                length = 0U;
                forget_locals(local, locals);
                facts[length++] = (MemoryFact){address, value, inst->type, store};
            }
        }
        free(local);
    }
    free(aliases); free(types); free(known_local); free(addressed); return changes;
}
