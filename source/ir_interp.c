#include "cinder.h"

#include <stdlib.h>
#include <string.h>

static const CinderIRFunction *find_function(const CinderIRModule *module, const char *name) {
    for (size_t i = 0U; i < module->functions.len; ++i) if (strcmp(module->functions.data[i].name, name) == 0) return &module->functions.data[i];
    return NULL;
}

static const CinderIRGlobal *find_global(const CinderIRModule *module, const char *name) {
    for (size_t i = 0U; i < module->globals.len; ++i) if (strcmp(module->globals.data[i].name, name) == 0) return &module->globals.data[i];
    return NULL;
}

static bool eval_binary(CinderIROp op, int64_t left, int64_t right, int64_t *result) {
    switch (op) {
        case IR_ADD: *result = left + right; return true;
        case IR_SUB: *result = left - right; return true;
        case IR_MUL: *result = left * right; return true;
        case IR_DIV_S: if (right == 0 || (left == INT64_MIN && right == -1)) return false; *result = left / right; return true;
        case IR_MOD_S: if (right == 0 || (left == INT64_MIN && right == -1)) return false; *result = left % right; return true;
        case IR_BIT_AND: *result = left & right; return true;
        case IR_BIT_OR: *result = left | right; return true;
        case IR_BIT_XOR: *result = left ^ right; return true;
        case IR_SHL: if (right < 0 || right >= 64) return false; *result = (int64_t)((uint64_t)left << (unsigned)right); return true;
        case IR_SHR_S: if (right < 0 || right >= 64) return false; *result = left >> (unsigned)right; return true;
        case IR_SHR_U: if (right < 0 || right >= 64) return false; *result = (int64_t)((uint64_t)left >> (unsigned)right); return true;
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

static bool eval_float(CinderIROp op, double left, double right, double *result, int64_t *comparison) {
    switch (op) { case IR_FADD: *result = left + right; return true; case IR_FSUB: *result = left - right; return true; case IR_FMUL: *result = left * right; return true; case IR_FDIV: *result = left / right; return true; case IR_FCMP_EQ: *comparison = left == right; return true; case IR_FCMP_NE: *comparison = left != right; return true; case IR_FCMP_LT: *comparison = left < right; return true; case IR_FCMP_LE: *comparison = left <= right; return true; case IR_FCMP_GT: *comparison = left > right; return true; case IR_FCMP_GE: *comparison = left >= right; return true; default: return false; }
}

static CinderInterpResult interpret_function(const CinderIRModule *module, const CinderIRFunction *function, const int64_t *args, size_t arg_count, unsigned *steps, unsigned limit, CinderDiagnostics *diags) {
    CinderInterpResult failure = {false, false, 0, 0.0};
    int64_t *values = cinder_alloc(function->value_count == 0U ? 1U : function->value_count * sizeof(*values));
    double *float_values = cinder_alloc(function->value_count == 0U ? 1U : function->value_count * sizeof(*float_values));
    int64_t *locals = cinder_alloc(function->local_count == 0U ? 1U : function->local_count * sizeof(*locals));
    memset(locals, 0, function->local_count * sizeof(*locals));
    CinderBlockId block_id = 0U;
    CinderBlockId previous_block = CINDER_INVALID_BLOCK;
    while (true) {
        if (++*steps > limit) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "IR interpreter step limit exceeded in '%s'", function->name); free(values); free(float_values); free(locals); return failure; }
        const CinderIRBlock *block = &function->blocks.data[block_id];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderIRInst *inst = &block->instructions.data[i];
            int64_t value = 0;
            switch (inst->op) {
                case IR_CONST: values[inst->dst] = inst->integer; break;
                case IR_FCONST: float_values[inst->dst] = inst->floating; break;
                case IR_GLOBAL_LOAD: { const CinderIRGlobal *global = find_global(module, inst->callee); if (global == NULL) { cinder_diag(diags, CINDER_ERROR, inst->loc, "IR interpreter cannot find global '%s'", inst->callee); free(values); free(float_values); free(locals); return failure; } values[inst->dst] = global->integer; break; }
                case IR_GLOBAL_STORE: cinder_diag(diags, CINDER_ERROR, inst->loc, "IR interpreter does not mutate global '%s'", inst->callee); free(values); free(float_values); free(locals); return failure;
                case IR_ARG: values[inst->dst] = inst->slot >= 0 && (size_t)inst->slot < arg_count ? args[inst->slot] : 0; break;
                case IR_VA_ARG: if (inst->slot < 0 || (size_t)inst->slot >= arg_count) { cinder_diag(diags, CINDER_ERROR, inst->loc, "variadic argument index is unavailable"); free(values); free(float_values); free(locals); return failure; } values[inst->dst] = args[inst->slot]; break;
                case IR_LOCAL_LOAD: values[inst->dst] = locals[inst->slot]; break;
                case IR_LOCAL_STORE: locals[inst->slot] = values[inst->left]; break;
                case IR_COPY: values[inst->dst] = values[inst->left]; break;
                case IR_PHI: {
                    size_t incoming = SIZE_MAX;
                    for (size_t p = 0U; p < inst->phi_blocks.len; ++p) if (inst->phi_blocks.data[p] == previous_block) incoming = p;
                    if (incoming == SIZE_MAX || incoming >= inst->args.len) { cinder_diag(diags, CINDER_ERROR, inst->loc, "IR interpreter reached a phi without a matching predecessor"); free(values); free(float_values); free(locals); return failure; }
                    values[inst->dst] = values[inst->args.data[incoming]];
                    break;
                }
                case IR_FNEG: float_values[inst->dst] = -float_values[inst->left]; break;
                case IR_FADD: case IR_FSUB: case IR_FMUL: case IR_FDIV: { double result = 0.0; int64_t comparison = 0; if (!eval_float(inst->op, float_values[inst->left], float_values[inst->right], &result, &comparison)) { cinder_diag(diags, CINDER_ERROR, inst->loc, "invalid floating operation during IR interpretation"); free(values); free(float_values); free(locals); return failure; } if (inst->op >= IR_FCMP_EQ && inst->op <= IR_FCMP_GE) values[inst->dst] = comparison; else float_values[inst->dst] = result; break; }
                case IR_FCMP_EQ: case IR_FCMP_NE: case IR_FCMP_LT: case IR_FCMP_LE: case IR_FCMP_GT: case IR_FCMP_GE: { double result = 0.0; int64_t comparison = 0; if (!eval_float(inst->op, float_values[inst->left], float_values[inst->right], &result, &comparison)) { cinder_diag(diags, CINDER_ERROR, inst->loc, "invalid floating comparison during IR interpretation"); free(values); free(float_values); free(locals); return failure; } values[inst->dst] = comparison; break; }
                case IR_NEG: values[inst->dst] = -values[inst->left]; break;
                case IR_BIT_NOT: values[inst->dst] = ~values[inst->left]; break;
                case IR_CALL: {
                    const CinderIRFunction *callee = find_function(module, inst->callee);
                    if (callee == NULL) { cinder_diag(diags, CINDER_ERROR, inst->loc, "IR interpreter cannot execute external call '%s'", inst->callee); free(values); free(float_values); free(locals); return failure; }
                    int64_t *call_args = cinder_alloc((inst->args.len == 0U ? 1U : inst->args.len) * sizeof(*call_args));
                    for (size_t a = 0U; a < inst->args.len; ++a) call_args[a] = values[inst->args.data[a]];
                    CinderInterpResult result = interpret_function(module, callee, call_args, inst->args.len, steps, limit, diags); free(call_args);
                    if (!result.valid) { free(values); free(float_values); free(locals); return failure; } if (result.floating_result) float_values[inst->dst] = result.floating; else values[inst->dst] = result.value; break;
                }
                default:
                    if (!eval_binary(inst->op, values[inst->left], values[inst->right], &value)) { cinder_diag(diags, CINDER_ERROR, inst->loc, "invalid operation during IR interpretation"); free(values); free(float_values); free(locals); return failure; }
                    values[inst->dst] = value; break;
            }
        }
        const CinderTerminator *term = &block->terminator;
        if (term->kind == TERM_RETURN) { if (function->type->return_type->kind == TYPE_FLOAT || function->type->return_type->kind == TYPE_DOUBLE) { double result = term->value == CINDER_INVALID_VALUE ? 0.0 : float_values[term->value]; free(values); free(float_values); free(locals); CinderInterpResult success = {true, true, 0, result}; return success; } int64_t result = term->value == CINDER_INVALID_VALUE ? 0 : values[term->value]; free(values); free(float_values); free(locals); CinderInterpResult success = {true, false, result, 0.0}; return success; }
        if (term->kind == TERM_JUMP) { previous_block = block_id; block_id = term->target; continue; }
        if (term->kind == TERM_BRANCH) { previous_block = block_id; block_id = values[term->condition] != 0 ? term->yes : term->no; continue; }
        cinder_diag(diags, CINDER_ERROR, term->loc, "IR interpreter reached an unterminated block"); free(values); free(float_values); free(locals); return failure;
    }
}

CinderInterpResult cinder_interpret(const CinderIRModule *module, const char *function_name, const int64_t *args, size_t arg_count, unsigned step_limit, CinderDiagnostics *diags) {
    const CinderIRFunction *function = find_function(module, function_name);
    CinderInterpResult failure = {false, false, 0, 0.0};
    if (function == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "IR interpreter cannot find '%s'", function_name); return failure; }
    unsigned steps = 0U;
    return interpret_function(module, function, args, arg_count, &steps, step_limit == 0U ? 1000000U : step_limit, diags);
}
