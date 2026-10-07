#include "interp_private.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

static const CinderIRFunction *find_function(const CinderIRModule *module, const char *name) {
    for (size_t i = 0U; i < module->functions.len; ++i) if (strcmp(module->functions.data[i].name, name) == 0) return &module->functions.data[i];
    return NULL;
}

int64_t cinder_interp_integer(uint64_t bits, const CinderType *type) {
    unsigned width = type != NULL && type->size != 0U && type->size <= 8U ? (unsigned)(type->size * 8U) : 64U;
    if (type != NULL && type->kind == TYPE_BOOL) return bits != 0U;
    uint64_t mask = width == 64U ? UINT64_MAX : (UINT64_C(1) << width) - 1U;
    bits &= mask;
    if (type != NULL && !type->is_unsigned && width < 64U && (bits & (UINT64_C(1) << (width - 1U))) != 0U) bits |= ~mask;
    return bits <= (uint64_t)INT64_MAX ? (int64_t)bits : -1 - (int64_t)~bits;
}

static bool scalar_convert(const CinderIRInst *inst, int64_t integer, double floating, bool source_float, int64_t *result, double *result_float, bool *floating_result) {
    const CinderType *to = inst->type;
    *floating_result = cinder_ir_floating(to);
    if (*floating_result) {
        if (to->kind == TYPE_FLOAT) {
            float value = source_float ? (float)floating : inst->source_type->is_unsigned ? (float)(uint64_t)integer : (float)integer;
            *result_float = (double)value;
        } else *result_float = source_float ? floating : inst->source_type->is_unsigned ? (double)(uint64_t)integer : (double)integer;
        return true;
    }
    if (!source_float) { *result = cinder_interp_integer((uint64_t)integer, to); return true; }
    if (to->kind == TYPE_BOOL) { *result = floating != 0.0; return true; }
    if (!isfinite(floating)) return false;
    unsigned width = (unsigned)(to->size * 8U);
    double unsigned_bound = width == 64U ? 0x1p64 : (double)(UINT64_C(1) << width);
    if (to->is_unsigned) {
        if (floating <= -1.0 || floating >= unsigned_bound) return false;
        *result = cinder_interp_integer((uint64_t)floating, to);
    } else {
        double bound = unsigned_bound / 2.0;
        if ((width == 64U ? floating < -bound : floating <= -bound - 1.0) || floating >= bound) return false;
        /* For binary64 at the int64 lower boundary, -bound-1 rounds to
         * -bound; accept the exactly representable minimum explicitly. */
        if (width == 64U && floating == -bound) { *result = INT64_MIN; return true; }
        *result = cinder_interp_integer((uint64_t)(int64_t)floating, to);
    }
    return true;
}

static bool eval_binary(const CinderIRInst *inst, int64_t left, int64_t right, int64_t *result) {
    CinderIROp op = inst->op;
    const CinderType *type = inst->source_type != NULL ? inst->source_type : inst->type;
    unsigned width = (unsigned)(type->size * 8U);
    bool unsig = type->is_unsigned;
    if (unsig && (op == IR_ADD || op == IR_SUB || op == IR_MUL)) {
        uint64_t a = (uint64_t)left, b = (uint64_t)right;
        *result = cinder_interp_integer(op == IR_ADD ? a + b : op == IR_SUB ? a - b : a * b, inst->type);
        return true;
    }
    if (width < 64U && (op == IR_ADD || op == IR_SUB || op == IR_MUL)) {
        int64_t value = op == IR_ADD ? left + right : op == IR_SUB ? left - right : left * right;
        int64_t bound = INT64_C(1) << (width - 1U);
        if (value < -bound || value >= bound) return false;
        *result = value; return true;
    }
    if ((op == IR_DIV_S || op == IR_MOD_S) && right == -1 && width < 64U && left == -(INT64_C(1) << (width - 1U))) return false;
    switch (op) {
        case IR_ADD:
            if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right)) return false;
            *result = left + right; return true;
        case IR_SUB:
            if ((right < 0 && left > INT64_MAX + right) || (right > 0 && left < INT64_MIN + right)) return false;
            *result = left - right; return true;
        case IR_MUL: {
            bool negative = (left < 0) != (right < 0);
            uint64_t a = left < 0 ? UINT64_C(0) - (uint64_t)left : (uint64_t)left;
            uint64_t b = right < 0 ? UINT64_C(0) - (uint64_t)right : (uint64_t)right;
            uint64_t bound = negative ? UINT64_C(1) << 63U : (uint64_t)INT64_MAX;
            if (b != 0U && a > bound / b) return false;
            *result = (int64_t)((uint64_t)left * (uint64_t)right); return true;
        }
        case IR_DIV_S: if (right == 0 || (left == INT64_MIN && right == -1)) return false; *result = left / right; return true;
        case IR_MOD_S: if (right == 0 || (left == INT64_MIN && right == -1)) return false; *result = left % right; return true;
        case IR_DIV_U: if (right == 0) return false; *result = (int64_t)((uint64_t)left / (uint64_t)right); return true;
        case IR_MOD_U: if (right == 0) return false; *result = (int64_t)((uint64_t)left % (uint64_t)right); return true;
        case IR_BIT_AND: *result = left & right; return true;
        case IR_BIT_OR: *result = left | right; return true;
        case IR_BIT_XOR: *result = left ^ right; return true;
        case IR_SHL:
            if (right < 0 || (uint64_t)right >= width) return false;
            if (!unsig && (left < 0 || (uint64_t)left > ((UINT64_C(1) << (width - 1U)) - 1U) >> (unsigned)right)) return false;
            *result = cinder_interp_integer((uint64_t)left << (unsigned)right, inst->type); return true;
        case IR_SHR_S: if (right < 0 || (uint64_t)right >= width) return false; *result = left >> (unsigned)right; return true;
        case IR_SHR_U: if (right < 0 || (uint64_t)right >= width) return false; *result = (int64_t)((uint64_t)left >> (unsigned)right); return true;
        case IR_CMP_EQ: *result = left == right; return true;
        case IR_CMP_NE: *result = left != right; return true;
        case IR_CMP_LT_S: *result = left < right; return true;
        case IR_CMP_LE_S: *result = left <= right; return true;
        case IR_CMP_GT_S: *result = left > right; return true;
        case IR_CMP_GE_S: *result = left >= right; return true;
        case IR_CMP_LT_U: *result = (uint64_t)left < (uint64_t)right; return true;
        case IR_CMP_LE_U: *result = (uint64_t)left <= (uint64_t)right; return true;
        case IR_CMP_GT_U: *result = (uint64_t)left > (uint64_t)right; return true;
        case IR_CMP_GE_U: *result = (uint64_t)left >= (uint64_t)right; return true;
        default: return false;
    }
}

static bool eval_float(const CinderIRInst *inst, double left, double right, double *result, int64_t *comparison) {
    CinderIROp op = inst->op;
    if (inst->type->kind == TYPE_FLOAT) {
        float a = (float)left, b = (float)right, value;
        switch (op) {
            case IR_FADD: value = a + b; break;
            case IR_FSUB: value = a - b; break;
            case IR_FMUL: value = a * b; break;
            case IR_FDIV: value = a / b; break;
            default: return false;
        }
        *result = (double)value; return true;
    }
    switch (op) { case IR_FADD: *result = left + right; return true; case IR_FSUB: *result = left - right; return true; case IR_FMUL: *result = left * right; return true; case IR_FDIV: *result = left / right; return true; case IR_FCMP_EQ: *comparison = left == right; return true; case IR_FCMP_NE: *comparison = left != right; return true; case IR_FCMP_LT: *comparison = left < right; return true; case IR_FCMP_LE: *comparison = left <= right; return true; case IR_FCMP_GT: *comparison = left > right; return true; case IR_FCMP_GE: *comparison = left >= right; return true; default: return false; }
}

const char *cinder_interp_class_name(CinderInterpClass classification) {
    static const char *names[] = {"defined", "signed_overflow", "division_zero", "invalid_shift", "uninitialized", "conversion_range", "unsupported", "resource_limit", "malformed", "pointer_bounds", "object_lifetime", "invalid_access", "readonly"};
    return (unsigned)classification < CINDER_ARRAY_LEN(names) ? names[classification] : "invalid_classification";
}

void cinder_interp_fail(InterpContext *context, CinderInterpClass classification, CinderLoc loc, const char *reason) {
    context->classification = classification;
    cinder_diag(context->diags, CINDER_ERROR, loc, "IR interpretation: %s", reason);
}

static size_t global_index(const CinderIRModule *module, const char *name) {
    for (size_t i = 0U; i < module->globals.len; ++i)
        if (strcmp(module->globals.data[i].name, name) == 0) return i;
    return SIZE_MAX;
}

static bool tick(InterpContext *context, CinderLoc loc) {
    if (context->steps >= context->limit) {
        cinder_interp_fail(context, INTERP_RESOURCE_LIMIT, loc, "instruction limit exceeded"); return false;
    }
    ++context->steps; return true;
}

static bool require_defined(InterpContext *context, const InterpValue *values, CinderValueId operand, CinderLoc loc) {
    if (values[operand].defined) return true;
    cinder_interp_fail(context, INTERP_UNINITIALIZED, loc, "undefined behavior from an uninitialized value");
    return false;
}

static bool phi_inputs(InterpContext *context, const CinderIRBlock *block, CinderBlockId previous, InterpValue *values, InterpValue *snapshot) {
    for (size_t i = 0U; i < block->instructions.len; ++i) {
        const CinderIRInst *phi = &block->instructions.data[i];
        if (phi->op != IR_PHI) continue;
        size_t incoming = SIZE_MAX;
        for (size_t p = 0U; p < phi->phi_blocks.len; ++p) if (phi->phi_blocks.data[p] == previous) incoming = p;
        if (incoming == SIZE_MAX) { cinder_interp_fail(context, INTERP_MALFORMED, phi->loc, "phi has no incoming predecessor"); return false; }
        snapshot[phi->dst] = values[phi->args.data[incoming]];
    }
    /* Commit after reading every input. Loop-header swaps and longer cycles
     * observe the predecessor state, independent of phi instruction order. */
    for (size_t i = 0U; i < block->instructions.len; ++i) {
        const CinderIRInst *phi = &block->instructions.data[i];
        if (phi->op == IR_PHI) values[phi->dst] = snapshot[phi->dst];
    }
    return true;
}

static bool interpret_function(InterpContext *context, const CinderIRFunction *function, const InterpValue *args, size_t arg_count, InterpValue *returned, const InterpValue *aggregate_return, const CinderType *actual_signature) {
    if (context->depth >= 256U) { cinder_interp_fail(context, INTERP_RESOURCE_LIMIT, (CinderLoc){0}, "call-depth limit exceeded"); return false; }
    ++context->depth;
    uint64_t frame = ++context->frames;
    size_t value_count = function->value_count == 0U ? 1U : function->value_count;
    size_t local_count = function->local_count == 0U ? 1U : function->local_count;
    InterpValue *values = cinder_alloc(value_count * sizeof(*values));
    InterpValue *snapshot = cinder_alloc(value_count * sizeof(*snapshot));
    uint32_t *locals = cinder_alloc(local_count * sizeof(*locals));
    memset(values, 0, value_count * sizeof(*values)); memset(locals, 0, local_count * sizeof(*locals));
    CinderBlockId block_id = 0U, previous = CINDER_INVALID_BLOCK;
    bool success = false;
    for (size_t slot = 0U; slot < function->local_count; ++slot) {
        locals[slot] = cinder_interp_object(context, function->local_types.data[slot], false, false, (CinderLoc){0});
        if (locals[slot] == 0U) goto done;
    }
    while (true) {
        const CinderIRBlock *block = &function->blocks.data[block_id];
        if (!phi_inputs(context, block, previous, values, snapshot)) goto done;
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderIRInst *inst = &block->instructions.data[i];
            if (!tick(context, inst->loc)) goto done;
            if (inst->op == IR_NOP || inst->op == IR_PHI) continue;
            if (inst->left != CINDER_INVALID_VALUE && !require_defined(context, values, inst->left, inst->loc)) goto done;
            if (inst->right != CINDER_INVALID_VALUE && !require_defined(context, values, inst->right, inst->loc)) goto done;
            InterpValue result = {.fp = cinder_ir_floating(inst->type), .defined = true};
            if ((inst->op == IR_VA_START || inst->op == IR_VA_COPY || inst->op == IR_VA_END || inst->op == IR_VA_ARG) &&
                (!values[inst->left].pointer || (inst->op == IR_VA_COPY && !values[inst->right].pointer))) {
                cinder_interp_fail(context, INTERP_INVALID_ACCESS, inst->loc, "va_list lacks object provenance"); goto done;
            }
            switch (inst->op) {
                case IR_UNDEF: result.defined = false; break;
                case IR_CONST: result.integer = cinder_interp_integer((uint64_t)inst->integer, inst->type); result.pointer = inst->type->kind == TYPE_POINTER && result.integer == 0; break;
                case IR_FCONST: result.floating = inst->floating; break;
                case IR_ARG: case IR_FARG:
                    if (inst->slot < 0 || (size_t)inst->slot >= arg_count) {
                        cinder_interp_fail(context, INTERP_UNSUPPORTED, inst->loc, "function argument is unavailable"); goto done;
                    }
                    result = args[inst->slot]; break;
                case IR_VA_START:
                    if (!cinder_interp_va_start(context, values[inst->left].address, args, arg_count, actual_signature, function->params.len, frame, inst->loc)) goto done;
                    break;
                case IR_VA_COPY:
                    if (!cinder_interp_va_copy(context, values[inst->left].address, values[inst->right].address, frame, inst->loc)) goto done;
                    break;
                case IR_VA_END:
                    if (!cinder_interp_va_end(context, values[inst->left].address, frame, inst->loc)) goto done;
                    break;
                case IR_VA_ARG:
                    if (!cinder_interp_va_arg(context, values[inst->left].address, inst->source_type, &result, inst->loc)) goto done;
                    if (inst->slot >= 0) {
                        InterpValue destination = cinder_interp_address(context, locals[inst->slot]);
                        if (!result.pointer) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, inst->loc, "aggregate variadic argument lacks object provenance"); goto done; }
                        if (!cinder_interp_object_copy(context, destination.address, result.address, inst->source_type, true, inst->loc)) goto done;
                        result = destination;
                    }
                    break;
                case IR_AGG_ARG: {
                    size_t parameter = (size_t)inst->operator_code;
                    if (parameter >= arg_count || !args[parameter].pointer) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, inst->loc, "aggregate argument is unavailable"); goto done; }
                    InterpValue destination = cinder_interp_address(context, locals[inst->slot]);
                    if (!cinder_interp_object_copy(context, destination.address, args[parameter].address, inst->type, true, inst->loc)) goto done;
                    break;
                }
                case IR_AGG_RETURN:
                    if (aggregate_return == NULL || !aggregate_return->pointer || !values[inst->left].pointer) { cinder_interp_fail(context, INTERP_UNSUPPORTED, inst->loc, "aggregate return storage is unavailable"); goto done; }
                    if (!cinder_interp_object_copy(context, aggregate_return->address, values[inst->left].address, inst->type, true, inst->loc)) goto done;
                    break;
                case IR_GLOBAL_LOAD: case IR_GLOBAL_STORE: case IR_GLOBAL_ADDRESS: {
                    size_t index = global_index(context->module, inst->callee);
                    if (index == SIZE_MAX || context->module->globals.data[index].is_extern) {
                        cinder_interp_fail(context, INTERP_UNSUPPORTED, inst->loc, "external global storage is unavailable"); goto done;
                    }
                    InterpValue address = cinder_interp_address(context, context->globals[index]);
                    if (inst->op == IR_GLOBAL_ADDRESS) result = address;
                    else if (inst->op == IR_GLOBAL_LOAD) { if (!cinder_interp_load(context, address.address, inst->type, &result, inst->loc)) goto done; }
                    else if (!cinder_interp_store(context, address.address, inst->type, &values[inst->left], false, inst->loc)) goto done;
                    break;
                }
                case IR_FUNCTION_ADDRESS: {
                    uint32_t identity = 0U;
                    for (size_t object = 0U; object < context->objects.len; ++object) if (context->objects.data[object].function_name != NULL && strcmp(context->objects.data[object].function_name, inst->callee) == 0) { identity = (uint32_t)object + 1U; break; }
                    if (identity == 0U) {
                        identity = cinder_interp_object(context, inst->type->base, true, true, inst->loc); if (identity == 0U) goto done;
                        context->objects.data[identity - 1U].function_name = inst->callee;
                    }
                    result = cinder_interp_address(context, identity); break;
                }
                case IR_LOCAL_BEGIN:
                    cinder_interp_retire(context, locals[inst->slot]);
                    locals[inst->slot] = cinder_interp_object(context, function->local_types.data[inst->slot], false, false, inst->loc);
                    if (locals[inst->slot] == 0U) goto done;
                    break;
                case IR_LOCAL_END: cinder_interp_retire(context, locals[inst->slot]); break;
                case IR_LOCAL_FREEZE: context->objects.data[locals[inst->slot] - 1U].readonly = true; break;
                case IR_LOCAL_ADDRESS: result = cinder_interp_address(context, locals[inst->slot]); break;
                case IR_OBJECT_COPY: case IR_OBJECT_INIT:
                    if (!values[inst->left].pointer || !values[inst->right].pointer) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, inst->loc, "object transfer lacks pointer provenance"); goto done; }
                    if (!cinder_interp_object_copy(context, values[inst->left].address, values[inst->right].address, inst->type, inst->op == IR_OBJECT_INIT, inst->loc)) goto done;
                    break;
                case IR_LOCAL_LOAD: {
                    InterpValue address = cinder_interp_address(context, locals[inst->slot]);
                    if (!cinder_interp_load(context, address.address, inst->type, &result, inst->loc)) goto done;
                    break;
                }
                case IR_LOCAL_STORE: case IR_LOCAL_INIT: {
                    InterpValue address = cinder_interp_address(context, locals[inst->slot]);
                    if (!cinder_interp_store(context, address.address, inst->type, &values[inst->left], inst->op == IR_LOCAL_INIT, inst->loc)) goto done;
                    break;
                }
                case IR_ZERO_INIT:
                    if (!values[inst->left].pointer) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, inst->loc, "zero initializer has no pointer provenance"); goto done; }
                    if (!cinder_interp_zero(context, values[inst->left].address, (size_t)inst->integer, inst->loc)) goto done;
                    break;
                case IR_MEMORY_LOAD: case IR_MEMORY_STORE: case IR_MEMORY_INIT: case IR_POINTER_OFFSET: case IR_POINTER_MEMBER: case IR_POINTER_DIFF: {
                    const InterpValue *pointer = &values[inst->left];
                    if (!pointer->pointer) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, inst->loc, "pointer has no object provenance"); goto done; }
                    if (inst->op == IR_MEMORY_LOAD) { if (!cinder_interp_load(context, pointer->address, inst->type, &result, inst->loc)) goto done; }
                    else if ((inst->op == IR_MEMORY_STORE || inst->op == IR_MEMORY_INIT)) { if (!cinder_interp_store(context, pointer->address, inst->type, &values[inst->right], inst->op == IR_MEMORY_INIT, inst->loc)) goto done; }
                    else if (inst->op == IR_POINTER_OFFSET) {
                        const CinderType *index_type = cinder_ir_value_type(function, inst->right);
                        if (index_type->is_unsigned && values[inst->right].integer < 0) { cinder_interp_fail(context, INTERP_POINTER_BOUNDS, inst->loc, "unsigned pointer index exceeds the object domain"); goto done; }
                        if (!cinder_interp_offset(context, pointer->address, values[inst->right].integer, inst->operator_code, (size_t)inst->integer, &result, inst->loc)) goto done;
                    }
                    else if (inst->op == IR_POINTER_MEMBER) { if (!cinder_interp_member(context, pointer->address, (size_t)inst->integer, inst->type->base->size, &result, inst->loc)) goto done; }
                    else {
                        if (!values[inst->right].pointer) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, inst->loc, "pointer subtraction has no object provenance"); goto done; }
                        if (!cinder_interp_difference(context, pointer->address, values[inst->right].address, (size_t)inst->integer, &result.integer, inst->loc)) goto done;
                    }
                    break;
                }
                case IR_COPY: result = values[inst->left]; break;
                case IR_CONVERT: {
                    const InterpValue *source = &values[inst->left];
                    if (inst->source_type->kind == TYPE_POINTER && inst->type->kind == TYPE_BOOL) {
                        InterpValue null = {.defined = true, .pointer = true};
                        if (!cinder_interp_compare(context, IR_CMP_NE, source, &null, &result.integer, inst->loc)) goto done;
                        break;
                    }
                    if (inst->type->kind == TYPE_POINTER) {
                        result = *source; result.fp = false; result.pointer = source->pointer || source->integer == 0;
                        if (inst->source_type->kind == TYPE_POINTER && inst->source_type->base->kind == TYPE_ARRAY && inst->source_type->base->complete && result.pointer && result.address.object != 0U) {
                            if (!cinder_interp_member(context, result.address, 0U, inst->source_type->base->size, &result, inst->loc)) goto done;
                        }
                        break;
                    }
                    if (source->pointer && !cinder_ir_floating(inst->type) && inst->type->size == 8U) { result = *source; result.fp = false; break; }
                    if (!scalar_convert(inst, source->integer, source->floating, source->fp, &result.integer, &result.floating, &result.fp)) {
                        cinder_interp_fail(context, INTERP_CONVERSION_RANGE, inst->loc, "undefined out-of-range scalar conversion"); goto done;
                    }
                    break;
                }
                case IR_NEG:
                    if (!inst->type->is_unsigned && (values[inst->left].integer == INT64_MIN || (inst->type->size < 8U && values[inst->left].integer == -(INT64_C(1) << (inst->type->size * 8U - 1U))))) {
                        cinder_interp_fail(context, INTERP_SIGNED_OVERFLOW, inst->loc, "undefined signed negation overflow"); goto done;
                    }
                    result.integer = cinder_interp_integer(UINT64_C(0) - (uint64_t)values[inst->left].integer, inst->type); break;
                case IR_BIT_NOT: result.integer = cinder_interp_integer((uint64_t)~values[inst->left].integer, inst->type); break;
                case IR_FNEG: result.floating = -values[inst->left].floating; break;
                case IR_FADD: case IR_FSUB: case IR_FMUL: case IR_FDIV:
                case IR_FCMP_EQ: case IR_FCMP_NE: case IR_FCMP_LT: case IR_FCMP_LE: case IR_FCMP_GT: case IR_FCMP_GE:
                    if (!eval_float(inst, values[inst->left].floating, values[inst->right].floating, &result.floating, &result.integer)) {
                        cinder_interp_fail(context, INTERP_MALFORMED, inst->loc, "invalid floating operation"); goto done;
                    }
                    break;
                case IR_CALL: {
                    const char *name = inst->callee;
                    if (name == NULL) {
                        const InterpValue *address = &values[inst->left];
                        if (!address->pointer || address->address.object == 0U || (size_t)address->address.object > context->objects.len || address->address.offset != 0) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, inst->loc, "invalid indirect function pointer"); goto done; }
                        const InterpObject *target = &context->objects.data[address->address.object - 1U];
                        if (!target->alive || target->function_name == NULL || !cinder_type_compatible(target->type, inst->callee_type)) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, inst->loc, "indirect call target or signature is invalid"); goto done; }
                        name = target->function_name;
                    }
                    const CinderIRFunction *callee = find_function(context->module, name);
                    if (callee == NULL) { cinder_interp_fail(context, INTERP_UNSUPPORTED, inst->loc, "external call is unavailable"); goto done; }
                    size_t count = inst->args.len == 0U ? 1U : inst->args.len;
                    InterpValue *arguments = cinder_alloc(count * sizeof(*arguments)); bool ready = true;
                    for (size_t a = 0U; a < inst->args.len; ++a) {
                        if (!require_defined(context, values, inst->args.data[a], inst->loc)) { ready = false; break; }
                        arguments[a] = values[inst->args.data[a]];
                    }
                    InterpValue destination = {.defined = true};
                    bool aggregate = callee->type->return_type->kind == TYPE_STRUCT || callee->type->return_type->kind == TYPE_UNION;
                    if (aggregate) destination = cinder_interp_address(context, locals[inst->slot]);
                    bool called = ready && interpret_function(context, callee, arguments, inst->args.len, &result, aggregate ? &destination : NULL, inst->source_type);
                    free(arguments);
                    if (!called) goto done;
                    break;
                }
                default:
                    if (inst->source_type != NULL && inst->source_type->kind == TYPE_POINTER) {
                        if (!cinder_interp_compare(context, inst->op, &values[inst->left], &values[inst->right], &result.integer, inst->loc)) goto done;
                        break;
                    }
                    if (!eval_binary(inst, values[inst->left].integer, values[inst->right].integer, &result.integer)) {
                        CinderInterpClass classification = INTERP_SIGNED_OVERFLOW;
                        const char *reason = "undefined signed arithmetic overflow";
                        if (inst->op == IR_SHL || inst->op == IR_SHR_S || inst->op == IR_SHR_U) { classification = INTERP_INVALID_SHIFT; reason = "undefined shift operation"; }
                        if ((inst->op == IR_DIV_S || inst->op == IR_MOD_S || inst->op == IR_DIV_U || inst->op == IR_MOD_U) && values[inst->right].integer == 0) { classification = INTERP_DIVISION_ZERO; reason = "undefined division by zero"; }
                        cinder_interp_fail(context, classification, inst->loc, reason); goto done;
                    }
                    result.integer = cinder_interp_integer((uint64_t)result.integer, inst->type); break;
            }
            if (inst->dst != CINDER_INVALID_VALUE) values[inst->dst] = result;
        }
        const CinderTerminator *term = &block->terminator;
        if (!tick(context, term->loc)) goto done;
        if (term->kind == TERM_RETURN) {
            if (term->value != CINDER_INVALID_VALUE) {
                if (!require_defined(context, values, term->value, term->loc)) goto done;
                *returned = values[term->value];
            } else *returned = aggregate_return != NULL ? *aggregate_return : (InterpValue){.defined = true};
            success = true; break;
        }
        previous = block_id;
        if (term->kind == TERM_JUMP) block_id = term->target;
        else if (term->kind == TERM_BRANCH) {
            if (!require_defined(context, values, term->condition, term->loc)) goto done;
            block_id = values[term->condition].integer != 0 ? term->yes : term->no;
        } else { cinder_interp_fail(context, INTERP_MALFORMED, term->loc, "reached an unterminated block"); goto done; }
    }
done:
    success = cinder_interp_va_finish(context, frame, success, (CinderLoc){0});
    for (size_t slot = 0U; slot < function->local_count; ++slot) cinder_interp_retire(context, locals[slot]);
    free(locals); free(snapshot); free(values); --context->depth;
    return success;
}

CinderInterpResult cinder_interpret(const CinderIRModule *module, const char *function_name, const int64_t *args, size_t arg_count, unsigned step_limit, CinderDiagnostics *diags) {
    CinderInterpResult result = {false, false, 0, 0.0, INTERP_MALFORMED};
    if (cinder_verify_ir(module, diags) != 0) return result;
    const CinderIRFunction *function = find_function(module, function_name);
    if (function == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "IR interpreter cannot find '%s'", function_name); return result; }
    size_t count = module->globals.len == 0U ? 1U : module->globals.len;
    uint32_t *globals = cinder_alloc(count * sizeof(*globals)); memset(globals, 0, count * sizeof(*globals));
    InterpContext context; memset(&context, 0, sizeof(context)); context.module = module; context.globals = globals;
    context.limit = step_limit == 0U ? 1000000U : step_limit; context.classification = INTERP_DEFINED; context.diags = diags;
    bool ready = true;
    for (size_t i = 0U; i < module->globals.len; ++i) {
        const CinderIRGlobal *global = &module->globals.data[i];
        if (global->is_extern) continue;
        globals[i] = cinder_interp_object(&context, global->type, true, false, global->loc);
        if (globals[i] == 0U) { ready = false; break; }
        InterpObject *object = &context.objects.data[globals[i] - 1U];
        if (global->bytes != NULL) {
            if (global->byte_count > object->size) { cinder_interp_fail(&context, INTERP_MALFORMED, global->loc, "global initializer exceeds its object"); ready = false; break; }
            memcpy(object->bytes, global->bytes, global->byte_count);
        } else if (global->type->kind != TYPE_ARRAY && global->type->kind != TYPE_STRUCT && global->type->kind != TYPE_UNION) {
            uint64_t bits = (uint64_t)global->integer;
            if (global->type->kind == TYPE_DOUBLE) memcpy(&bits, &global->floating, sizeof(bits));
            else if (global->type->kind == TYPE_FLOAT) { float single = (float)global->floating; uint32_t narrow; memcpy(&narrow, &single, sizeof(narrow)); bits = narrow; }
            for (size_t byte = 0U; byte < object->size; ++byte) object->bytes[byte] = (unsigned char)(bits >> (byte * 8U));
        }
        object->readonly = global->read_only || (global->type->qualifiers & 1U) != 0U;
    }
    for (size_t g = 0U; g < module->globals.len && ready; ++g) {
        const CinderIRGlobal *global = &module->globals.data[g];
        for (size_t a = 0U; a < global->addresses.len; ++a) {
            const CinderIRAddress *address = &global->addresses.data[a]; uint32_t target = 0U;
            if (address->function) {
                for (size_t o = 0U; o < context.objects.len; ++o) if (context.objects.data[o].function_name != NULL && strcmp(context.objects.data[o].function_name, address->symbol) == 0) { target = (uint32_t)o + 1U; break; }
                if (target == 0U) {
                    target = cinder_interp_object(&context, address->target_type, true, true, global->loc);
                    if (target != 0U) context.objects.data[target - 1U].function_name = address->symbol;
                }
            } else {
                size_t index = global_index(module, address->symbol);
                if (index != SIZE_MAX) target = globals[index];
            }
            if (target == 0U) { cinder_interp_fail(&context, INTERP_UNSUPPORTED, global->loc, "address initializer target storage is unavailable"); ready = false; break; }
            if (address->addend < 0 || (uint64_t)address->addend < address->domain_begin || (uint64_t)address->addend > address->domain_end) { cinder_interp_fail(&context, INTERP_POINTER_BOUNDS, global->loc, "address initializer exceeds its object domain"); ready = false; break; }
            InterpValue value = cinder_interp_pointer_value((InterpPointer){target, address->addend, address->domain_begin, address->domain_end});
            InterpObject *object = &context.objects.data[globals[g] - 1U];
            for (size_t byte = 0U; byte < 8U; ++byte) object->bytes[address->offset + byte] = (unsigned char)((uint64_t)value.integer >> (byte * 8U));
            InterpStoredPointer stored = {address->offset, value.address}; cinder_vec_push((CinderVec *)&object->pointers, &stored);
        }
    }
    size_t argument_storage = arg_count == 0U ? 1U : arg_count;
    InterpValue *arguments = cinder_alloc(argument_storage * sizeof(*arguments)); memset(arguments, 0, argument_storage * sizeof(*arguments));
    for (size_t a = 0U; a < arg_count; ++a) { arguments[a].integer = args[a]; arguments[a].defined = true; }
    InterpValue returned = {0};
    result.valid = ready && interpret_function(&context, function, arguments, arg_count, &returned, NULL, function->type);
    result.floating_result = returned.fp; result.value = returned.integer; result.floating = returned.floating;
    result.classification = context.classification;
    free(arguments); cinder_interp_memory_destroy(&context); free(globals); return result;
}
