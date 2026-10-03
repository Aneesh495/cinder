#include "cinder.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

static const CinderIRFunction *find_function(const CinderIRModule *module, const char *name) {
    for (size_t i = 0U; i < module->functions.len; ++i) if (strcmp(module->functions.data[i].name, name) == 0) return &module->functions.data[i];
    return NULL;
}

static const CinderIRGlobal *find_global(const CinderIRModule *module, const char *name) {
    for (size_t i = 0U; i < module->globals.len; ++i) if (strcmp(module->globals.data[i].name, name) == 0) return &module->globals.data[i];
    return NULL;
}

static int64_t integer_value(uint64_t bits, const CinderType *type) {
    unsigned width = type != NULL && type->size != 0U ? (unsigned)(type->size * 8U) : 64U;
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

static CinderInterpResult interpret_function(const CinderIRModule *module, const CinderIRFunction *function, const int64_t *args, size_t arg_count, const double *float_args, size_t float_arg_count, unsigned *steps, unsigned limit, CinderDiagnostics *diags) {
    CinderInterpResult failure = {false, false, 0, 0.0};
    int64_t *values = cinder_alloc(function->value_count == 0U ? 1U : function->value_count * sizeof(*values));
    double *float_values = cinder_alloc(function->value_count == 0U ? 1U : function->value_count * sizeof(*float_values));
    bool *value_is_float = cinder_alloc(function->value_count == 0U ? 1U : function->value_count * sizeof(*value_is_float));
    double *float_locals = cinder_alloc(function->local_count == 0U ? 1U : function->local_count * sizeof(*float_locals));
    bool *local_is_float = cinder_alloc(function->local_count == 0U ? 1U : function->local_count * sizeof(*local_is_float));
    int64_t *locals = cinder_alloc(function->local_count == 0U ? 1U : function->local_count * sizeof(*locals));
    memset(locals, 0, function->local_count * sizeof(*locals));
    memset(value_is_float, 0, function->value_count * sizeof(*value_is_float));
    memset(local_is_float, 0, function->local_count * sizeof(*local_is_float));
    CinderBlockId block_id = 0U;
    CinderBlockId previous_block = CINDER_INVALID_BLOCK;
    while (true) {
        if (++*steps > limit) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "IR interpreter step limit exceeded in '%s'", function->name); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure; }
        const CinderIRBlock *block = &function->blocks.data[block_id];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderIRInst *inst = &block->instructions.data[i];
            int64_t value = 0;
            switch (inst->op) {
                case IR_NOP: break;
                case IR_CONST: values[inst->dst] = inst->integer; value_is_float[inst->dst] = false; break;
                case IR_FCONST: float_values[inst->dst] = inst->floating; value_is_float[inst->dst] = true; break;
                case IR_GLOBAL_LOAD: { const CinderIRGlobal *global = find_global(module, inst->callee); if (global == NULL) { cinder_diag(diags, CINDER_ERROR, inst->loc, "IR interpreter cannot find global '%s'", inst->callee); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure; } values[inst->dst] = global->integer; value_is_float[inst->dst] = false; break; }
                case IR_GLOBAL_STORE: cinder_diag(diags, CINDER_ERROR, inst->loc, "IR interpreter does not mutate global '%s'", inst->callee); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure;
                case IR_ARG: values[inst->dst] = inst->operator_code >= 0 && (size_t)inst->operator_code < arg_count ? args[inst->operator_code] : 0; value_is_float[inst->dst] = false; break;
                case IR_FARG: if (inst->operator_code < 0 || (size_t)inst->operator_code >= float_arg_count) { cinder_diag(diags, CINDER_ERROR, inst->loc, "floating argument register is unavailable"); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure; } float_values[inst->dst] = float_args[inst->operator_code]; value_is_float[inst->dst] = true; break;
                case IR_VA_ARG: if (inst->slot < 0 || (size_t)inst->slot >= arg_count) { cinder_diag(diags, CINDER_ERROR, inst->loc, "variadic argument index is unavailable"); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure; } values[inst->dst] = args[inst->slot]; value_is_float[inst->dst] = false; break;
                case IR_LOCAL_LOAD: if (local_is_float[inst->slot]) { float_values[inst->dst] = float_locals[inst->slot]; value_is_float[inst->dst] = true; } else { values[inst->dst] = locals[inst->slot]; value_is_float[inst->dst] = false; } break;
                case IR_LOCAL_STORE: if (value_is_float[inst->left]) { float_locals[inst->slot] = float_values[inst->left]; local_is_float[inst->slot] = true; } else { locals[inst->slot] = values[inst->left]; local_is_float[inst->slot] = false; } break;
                case IR_CONVERT:
                    if (!scalar_convert(inst, value_is_float[inst->left] ? 0 : values[inst->left], value_is_float[inst->left] ? float_values[inst->left] : 0.0, value_is_float[inst->left], &values[inst->dst], &float_values[inst->dst], &value_is_float[inst->dst])) {
                        cinder_diag(diags, CINDER_ERROR, inst->loc, "undefined out-of-range scalar conversion in IR interpretation");
                        free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure;
                    }
                    break;
                case IR_COPY: if (value_is_float[inst->left]) { float_values[inst->dst] = float_values[inst->left]; value_is_float[inst->dst] = true; } else { values[inst->dst] = values[inst->left]; value_is_float[inst->dst] = false; } break;
                case IR_PHI: {
                    size_t incoming = SIZE_MAX;
                    for (size_t p = 0U; p < inst->phi_blocks.len; ++p) if (inst->phi_blocks.data[p] == previous_block) incoming = p;
                    if (incoming == SIZE_MAX || incoming >= inst->args.len) { cinder_diag(diags, CINDER_ERROR, inst->loc, "IR interpreter reached a phi without a matching predecessor"); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure; }
                    CinderValueId source = inst->args.data[incoming];
                    value_is_float[inst->dst] = value_is_float[source];
                    if (value_is_float[source]) float_values[inst->dst] = float_values[source];
                    else values[inst->dst] = values[source];
                    break;
                }
                case IR_FNEG: float_values[inst->dst] = -float_values[inst->left]; value_is_float[inst->dst] = true; break;
                case IR_FADD: case IR_FSUB: case IR_FMUL: case IR_FDIV: { double result = 0.0; int64_t comparison = 0; if (!eval_float(inst, float_values[inst->left], float_values[inst->right], &result, &comparison)) { cinder_diag(diags, CINDER_ERROR, inst->loc, "invalid floating operation during IR interpretation"); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure; } if (inst->op >= IR_FCMP_EQ && inst->op <= IR_FCMP_GE) { values[inst->dst] = comparison; value_is_float[inst->dst] = false; } else { float_values[inst->dst] = inst->type->kind == TYPE_FLOAT ? (double)(float)result : result; value_is_float[inst->dst] = true; } break; }
                case IR_FCMP_EQ: case IR_FCMP_NE: case IR_FCMP_LT: case IR_FCMP_LE: case IR_FCMP_GT: case IR_FCMP_GE: { double result = 0.0; int64_t comparison = 0; if (!eval_float(inst, float_values[inst->left], float_values[inst->right], &result, &comparison)) { cinder_diag(diags, CINDER_ERROR, inst->loc, "invalid floating comparison during IR interpretation"); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure; } values[inst->dst] = comparison; value_is_float[inst->dst] = false; break; }
                case IR_NEG:
                    if (!inst->type->is_unsigned && (values[inst->left] == INT64_MIN || (inst->type->size < 8U && values[inst->left] == -(INT64_C(1) << (inst->type->size * 8U - 1U))))) {
                        cinder_diag(diags, CINDER_ERROR, inst->loc, "undefined signed negation in IR interpretation");
                        free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure;
                    }
                    values[inst->dst] = integer_value(UINT64_C(0) - (uint64_t)values[inst->left], inst->type); break;
                case IR_BIT_NOT: values[inst->dst] = integer_value((uint64_t)~values[inst->left], inst->type); break;
                case IR_CALL: {
                    const CinderIRFunction *callee = find_function(module, inst->callee);
                    if (callee == NULL) { cinder_diag(diags, CINDER_ERROR, inst->loc, "IR interpreter cannot execute external call '%s'", inst->callee); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure; }
                    int64_t *call_args = cinder_alloc((inst->args.len == 0U ? 1U : inst->args.len) * sizeof(*call_args));
                    double *call_float_args = cinder_alloc((inst->args.len == 0U ? 1U : inst->args.len) * sizeof(*call_float_args));
                    size_t integer_count = 0U; size_t float_count = 0U;
                    for (size_t a = 0U; a < inst->args.len; ++a) { bool is_float = a < inst->arg_floats.len && inst->arg_floats.data[a]; if (is_float) call_float_args[float_count++] = float_values[inst->args.data[a]]; else call_args[integer_count++] = values[inst->args.data[a]]; }
                    CinderInterpResult result = interpret_function(module, callee, call_args, integer_count, call_float_args, float_count, steps, limit, diags); free(call_args); free(call_float_args);
                    if (!result.valid) { free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure; } if (inst->dst == CINDER_INVALID_VALUE) break; if (result.floating_result) { float_values[inst->dst] = result.floating; value_is_float[inst->dst] = true; } else { values[inst->dst] = result.value; value_is_float[inst->dst] = false; } break;
                }
                default:
                    if (!eval_binary(inst, values[inst->left], values[inst->right], &value)) { cinder_diag(diags, CINDER_ERROR, inst->loc, "invalid operation during IR interpretation"); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure; }
                    values[inst->dst] = integer_value((uint64_t)value, inst->type); value_is_float[inst->dst] = false; break;
            }
        }
        const CinderTerminator *term = &block->terminator;
        if (term->kind == TERM_RETURN) { if (function->type->return_type->kind == TYPE_FLOAT || function->type->return_type->kind == TYPE_DOUBLE) { double result = term->value == CINDER_INVALID_VALUE ? 0.0 : float_values[term->value]; free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); CinderInterpResult success = {true, true, 0, result}; return success; } int64_t result = term->value == CINDER_INVALID_VALUE ? 0 : values[term->value]; free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); CinderInterpResult success = {true, false, result, 0.0}; return success; }
        if (term->kind == TERM_JUMP) { previous_block = block_id; block_id = term->target; continue; }
        if (term->kind == TERM_BRANCH) { previous_block = block_id; block_id = values[term->condition] != 0 ? term->yes : term->no; continue; }
        cinder_diag(diags, CINDER_ERROR, term->loc, "IR interpreter reached an unterminated block"); free(values); free(float_values); free(value_is_float); free(locals); free(float_locals); free(local_is_float); return failure;
    }
}

CinderInterpResult cinder_interpret(const CinderIRModule *module, const char *function_name, const int64_t *args, size_t arg_count, unsigned step_limit, CinderDiagnostics *diags) {
    const CinderIRFunction *function = find_function(module, function_name);
    CinderInterpResult failure = {false, false, 0, 0.0};
    if (function == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "IR interpreter cannot find '%s'", function_name); return failure; }
    unsigned steps = 0U;
    return interpret_function(module, function, args, arg_count, NULL, 0U, &steps, step_limit == 0U ? 1000000U : step_limit, diags);
}
