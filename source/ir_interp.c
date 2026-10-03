#include "cinder.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

static const CinderIRFunction *find_function(const CinderIRModule *module, const char *name) {
    for (size_t i = 0U; i < module->functions.len; ++i) if (strcmp(module->functions.data[i].name, name) == 0) return &module->functions.data[i];
    return NULL;
}

static int64_t integer_value(uint64_t bits, const CinderType *type) {
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
    if (!source_float) { *result = integer_value((uint64_t)integer, to); return true; }
    if (to->kind == TYPE_BOOL) { *result = floating != 0.0; return true; }
    if (!isfinite(floating)) return false;
    unsigned width = (unsigned)(to->size * 8U);
    double unsigned_bound = width == 64U ? 0x1p64 : (double)(UINT64_C(1) << width);
    if (to->is_unsigned) {
        if (floating <= -1.0 || floating >= unsigned_bound) return false;
        *result = integer_value((uint64_t)floating, to);
    } else {
        double bound = unsigned_bound / 2.0;
        if ((width == 64U ? floating < -bound : floating <= -bound - 1.0) || floating >= bound) return false;
        /* For binary64 at the int64 lower boundary, -bound-1 rounds to
         * -bound; accept the exactly representable minimum explicitly. */
        if (width == 64U && floating == -bound) { *result = INT64_MIN; return true; }
        *result = integer_value((uint64_t)(int64_t)floating, to);
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
        *result = integer_value(op == IR_ADD ? a + b : op == IR_SUB ? a - b : a * b, inst->type);
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
            *result = integer_value((uint64_t)left << (unsigned)right, inst->type); return true;
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

typedef struct {
    int64_t integer;
    double floating;
    bool fp;
    bool defined;
} InterpValue;

typedef struct {
    const CinderIRModule *module;
    InterpValue *globals;
    unsigned steps;
    unsigned limit;
    unsigned depth;
    CinderInterpClass classification;
    CinderDiagnostics *diags;
} InterpContext;

static void failure(InterpContext *context, CinderInterpClass classification, CinderLoc loc, const char *reason) {
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
        failure(context, INTERP_RESOURCE_LIMIT, loc, "instruction limit exceeded"); return false;
    }
    ++context->steps; return true;
}

static bool require_defined(InterpContext *context, const InterpValue *values, CinderValueId operand, CinderLoc loc) {
    if (values[operand].defined) return true;
    failure(context, INTERP_UNINITIALIZED, loc, "undefined behavior from an uninitialized value");
    return false;
}

static bool phi_inputs(InterpContext *context, const CinderIRBlock *block, CinderBlockId previous, InterpValue *values, InterpValue *snapshot) {
    for (size_t i = 0U; i < block->instructions.len; ++i) {
        const CinderIRInst *phi = &block->instructions.data[i];
        if (phi->op != IR_PHI) continue;
        size_t incoming = SIZE_MAX;
        for (size_t p = 0U; p < phi->phi_blocks.len; ++p) if (phi->phi_blocks.data[p] == previous) incoming = p;
        if (incoming == SIZE_MAX) { failure(context, INTERP_MALFORMED, phi->loc, "phi has no incoming predecessor"); return false; }
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

static bool interpret_function(InterpContext *context, const CinderIRFunction *function, const int64_t *args, size_t arg_count, const double *float_args, size_t float_count, InterpValue *returned) {
    if (context->depth >= 256U) { failure(context, INTERP_RESOURCE_LIMIT, (CinderLoc){0}, "call-depth limit exceeded"); return false; }
    ++context->depth;
    size_t value_count = function->value_count == 0U ? 1U : function->value_count;
    size_t local_count = function->local_count == 0U ? 1U : function->local_count;
    InterpValue *values = cinder_alloc(value_count * sizeof(*values));
    InterpValue *snapshot = cinder_alloc(value_count * sizeof(*snapshot));
    InterpValue *locals = cinder_alloc(local_count * sizeof(*locals));
    memset(values, 0, value_count * sizeof(*values)); memset(locals, 0, local_count * sizeof(*locals));
    CinderBlockId block_id = 0U, previous = CINDER_INVALID_BLOCK;
    bool success = false;
    while (true) {
        const CinderIRBlock *block = &function->blocks.data[block_id];
        if (!phi_inputs(context, block, previous, values, snapshot)) goto done;
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderIRInst *inst = &block->instructions.data[i];
            if (!tick(context, inst->loc)) goto done;
            if (inst->op == IR_NOP || inst->op == IR_PHI) continue;
            if (inst->left != CINDER_INVALID_VALUE && !require_defined(context, values, inst->left, inst->loc)) goto done;
            if (inst->right != CINDER_INVALID_VALUE && !require_defined(context, values, inst->right, inst->loc)) goto done;
            InterpValue result = {0, 0.0, cinder_ir_floating(inst->type), true};
            switch (inst->op) {
                case IR_UNDEF: result.defined = false; break;
                case IR_CONST: result.integer = integer_value((uint64_t)inst->integer, inst->type); break;
                case IR_FCONST: result.floating = inst->floating; break;
                case IR_ARG:
                    if (inst->operator_code < 0 || (size_t)inst->operator_code >= arg_count) {
                        failure(context, INTERP_UNSUPPORTED, inst->loc, "integer argument is unavailable"); goto done;
                    }
                    result.integer = integer_value((uint64_t)args[inst->operator_code], inst->type); break;
                case IR_FARG:
                    if (inst->operator_code < 0 || (size_t)inst->operator_code >= float_count) {
                        failure(context, INTERP_UNSUPPORTED, inst->loc, "floating argument is unavailable"); goto done;
                    }
                    result.floating = float_args[inst->operator_code]; break;
                case IR_VA_ARG:
                    if (inst->slot < 0 || (size_t)inst->slot >= arg_count) {
                        failure(context, INTERP_UNSUPPORTED, inst->loc, "variadic integer ordinal is unavailable"); goto done;
                    }
                    result.integer = integer_value((uint64_t)args[inst->slot], inst->type); break;
                case IR_GLOBAL_LOAD: case IR_GLOBAL_STORE: {
                    size_t index = global_index(context->module, inst->callee);
                    if (index == SIZE_MAX || context->module->globals.data[index].is_extern) {
                        failure(context, INTERP_UNSUPPORTED, inst->loc, "external global storage is unavailable"); goto done;
                    }
                    if (inst->op == IR_GLOBAL_LOAD) result = context->globals[index];
                    else context->globals[index] = values[inst->left];
                    break;
                }
                case IR_LOCAL_LOAD:
                    if (!locals[inst->slot].defined) {
                        failure(context, INTERP_UNINITIALIZED, inst->loc, "undefined behavior from an uninitialized local"); goto done;
                    }
                    result = locals[inst->slot]; break;
                case IR_LOCAL_STORE: locals[inst->slot] = values[inst->left]; break;
                case IR_COPY: result = values[inst->left]; break;
                case IR_CONVERT: {
                    const InterpValue *source = &values[inst->left];
                    if (!scalar_convert(inst, source->integer, source->floating, source->fp, &result.integer, &result.floating, &result.fp)) {
                        failure(context, INTERP_CONVERSION_RANGE, inst->loc, "undefined out-of-range scalar conversion"); goto done;
                    }
                    break;
                }
                case IR_NEG:
                    if (!inst->type->is_unsigned && (values[inst->left].integer == INT64_MIN || (inst->type->size < 8U && values[inst->left].integer == -(INT64_C(1) << (inst->type->size * 8U - 1U))))) {
                        failure(context, INTERP_SIGNED_OVERFLOW, inst->loc, "undefined signed negation overflow"); goto done;
                    }
                    result.integer = integer_value(UINT64_C(0) - (uint64_t)values[inst->left].integer, inst->type); break;
                case IR_BIT_NOT: result.integer = integer_value((uint64_t)~values[inst->left].integer, inst->type); break;
                case IR_FNEG: result.floating = -values[inst->left].floating; break;
                case IR_FADD: case IR_FSUB: case IR_FMUL: case IR_FDIV:
                case IR_FCMP_EQ: case IR_FCMP_NE: case IR_FCMP_LT: case IR_FCMP_LE: case IR_FCMP_GT: case IR_FCMP_GE:
                    if (!eval_float(inst, values[inst->left].floating, values[inst->right].floating, &result.floating, &result.integer)) {
                        failure(context, INTERP_MALFORMED, inst->loc, "invalid floating operation"); goto done;
                    }
                    break;
                case IR_CALL: {
                    const CinderIRFunction *callee = find_function(context->module, inst->callee);
                    if (callee == NULL) { failure(context, INTERP_UNSUPPORTED, inst->loc, "external call is unavailable"); goto done; }
                    size_t count = inst->args.len == 0U ? 1U : inst->args.len;
                    int64_t *integers = cinder_alloc(count * sizeof(*integers));
                    double *floats = cinder_alloc(count * sizeof(*floats));
                    size_t ni = 0U, nf = 0U; bool ready = true;
                    for (size_t a = 0U; a < inst->args.len; ++a) {
                        if (!require_defined(context, values, inst->args.data[a], inst->loc)) { ready = false; break; }
                        if (inst->arg_floats.data[a]) floats[nf++] = values[inst->args.data[a]].floating;
                        else integers[ni++] = values[inst->args.data[a]].integer;
                    }
                    bool called = ready && interpret_function(context, callee, integers, ni, floats, nf, &result);
                    free(floats); free(integers);
                    if (!called) goto done;
                    break;
                }
                default:
                    if (!eval_binary(inst, values[inst->left].integer, values[inst->right].integer, &result.integer)) {
                        CinderInterpClass classification = INTERP_SIGNED_OVERFLOW;
                        const char *reason = "undefined signed arithmetic overflow";
                        if (inst->op == IR_SHL || inst->op == IR_SHR_S || inst->op == IR_SHR_U) { classification = INTERP_INVALID_SHIFT; reason = "undefined shift operation"; }
                        if ((inst->op == IR_DIV_S || inst->op == IR_MOD_S || inst->op == IR_DIV_U || inst->op == IR_MOD_U) && values[inst->right].integer == 0) { classification = INTERP_DIVISION_ZERO; reason = "undefined division by zero"; }
                        failure(context, classification, inst->loc, reason); goto done;
                    }
                    result.integer = integer_value((uint64_t)result.integer, inst->type); break;
            }
            if (inst->dst != CINDER_INVALID_VALUE) values[inst->dst] = result;
        }
        const CinderTerminator *term = &block->terminator;
        if (!tick(context, term->loc)) goto done;
        if (term->kind == TERM_RETURN) {
            if (term->value != CINDER_INVALID_VALUE) {
                if (!require_defined(context, values, term->value, term->loc)) goto done;
                *returned = values[term->value];
            } else *returned = (InterpValue){0, 0.0, false, true};
            success = true; break;
        }
        previous = block_id;
        if (term->kind == TERM_JUMP) block_id = term->target;
        else if (term->kind == TERM_BRANCH) {
            if (!require_defined(context, values, term->condition, term->loc)) goto done;
            block_id = values[term->condition].integer != 0 ? term->yes : term->no;
        } else { failure(context, INTERP_MALFORMED, term->loc, "reached an unterminated block"); goto done; }
    }
done:
    free(locals); free(snapshot); free(values); --context->depth;
    return success;
}

CinderInterpResult cinder_interpret(const CinderIRModule *module, const char *function_name, const int64_t *args, size_t arg_count, unsigned step_limit, CinderDiagnostics *diags) {
    CinderInterpResult result = {false, false, 0, 0.0, INTERP_MALFORMED};
    if (cinder_verify_ir(module, diags) != 0) return result;
    const CinderIRFunction *function = find_function(module, function_name);
    if (function == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "IR interpreter cannot find '%s'", function_name); return result; }
    size_t count = module->globals.len == 0U ? 1U : module->globals.len;
    InterpValue *globals = cinder_alloc(count * sizeof(*globals));
    for (size_t i = 0U; i < module->globals.len; ++i) {
        const CinderIRGlobal *global = &module->globals.data[i];
        globals[i] = (InterpValue){integer_value((uint64_t)global->integer, global->type), global->floating, cinder_ir_floating(global->type), true};
    }
    InterpContext context = {module, globals, 0U, step_limit == 0U ? 1000000U : step_limit, 0U, INTERP_DEFINED, diags};
    InterpValue returned = {0, 0.0, false, false};
    result.valid = interpret_function(&context, function, args, arg_count, NULL, 0U, &returned);
    result.floating_result = returned.fp; result.value = returned.integer; result.floating = returned.floating;
    result.classification = context.classification;
    free(globals); return result;
}
