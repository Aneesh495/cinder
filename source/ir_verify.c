#include "cinder.h"

#include <stdlib.h>
#include <string.h>

static bool integer(const CinderType *type) {
    return type != NULL && (type->kind == TYPE_BOOL || type->kind == TYPE_CHAR || type->kind == TYPE_SHORT || type->kind == TYPE_INT || type->kind == TYPE_LONG || type->kind == TYPE_LLONG || type->kind == TYPE_ENUM || type->kind == TYPE_POINTER);
}

static bool contains_block(const CinderBlockId *blocks, size_t count, CinderBlockId value) {
    for (size_t i = 0U; i < count; ++i) if (blocks[i] == value) return true;
    return false;
}

static bool dominates(const CinderCFGAnalysis *cfg, CinderBlockId definition, CinderBlockId use) {
    if (definition >= cfg->block_count || use >= cfg->block_count || !cfg->reachable[definition] || !cfg->reachable[use]) return false;
    for (size_t hops = 0U; hops < cfg->block_count; ++hops) {
        if (definition == use) return true;
        CinderBlockId parent = cfg->idom[use];
        if (parent == CINDER_INVALID_BLOCK || parent == use) return false;
        use = parent;
    }
    return false;
}

static void check_use(CinderValueId value, CinderBlockId block, size_t position, const CinderIRFunction *function, const CinderBlockId *definitions, const size_t *positions, const CinderCFGAnalysis *cfg, CinderLoc loc, CinderDiagnostics *diags) {
    if ((size_t)value >= function->value_count) {
        cinder_diag(diags, CINDER_FATAL, loc, "IR operand is absent or outside the value table in '%s'", function->name); return;
    }
    CinderBlockId definition = definitions[value];
    if (definition == CINDER_INVALID_BLOCK) {
        cinder_diag(diags, CINDER_FATAL, loc, "IR value %u has no definition in '%s'", value, function->name); return;
    }
    if (definition == block) {
        if (positions[value] >= position) cinder_diag(diags, CINDER_FATAL, loc, "IR value %u is used before its definition", value);
    } else if (cfg->reachable[block] && !dominates(cfg, definition, block)) {
        cinder_diag(diags, CINDER_FATAL, loc, "IR value %u does not dominate its use", value);
    }
}

static bool binary_integer(CinderIROp op) {
    return op == IR_ADD || op == IR_SUB || op == IR_MUL || (op >= IR_DIV_S && op <= IR_MOD_U) || (op >= IR_BIT_AND && op <= IR_CMP_GE_U);
}

static bool binary_float(CinderIROp op) {
    return (op >= IR_FADD && op <= IR_FDIV) || (op >= IR_FCMP_EQ && op <= IR_FCMP_GE);
}

static void check_types(const CinderIRInst *inst, CinderType *const *types, CinderDiagnostics *diags) {
    bool result_fp = cinder_ir_floating(inst->type);
    if (inst->op == IR_FCONST || inst->op == IR_FARG || inst->op == IR_FNEG || (inst->op >= IR_FADD && inst->op <= IR_FDIV)) {
        if (!result_fp) cinder_diag(diags, CINDER_FATAL, inst->loc, "floating IR operation has a non-floating result");
    } else if (inst->op == IR_CONST || inst->op == IR_ARG || inst->op == IR_NEG || inst->op == IR_BIT_NOT || binary_integer(inst->op) || (inst->op >= IR_FCMP_EQ && inst->op <= IR_FCMP_GE)) {
        if (!integer(inst->type)) cinder_diag(diags, CINDER_FATAL, inst->loc, "integer IR operation has a non-integer result");
    }
    if (binary_integer(inst->op)) {
        if (!integer(types[inst->left]) || !integer(types[inst->right])) cinder_diag(diags, CINDER_FATAL, inst->loc, "integer IR operation has non-integer operands");
    }
    if (binary_float(inst->op)) {
        if (!cinder_ir_floating(types[inst->left]) || !cinder_ir_floating(types[inst->right])) cinder_diag(diags, CINDER_FATAL, inst->loc, "floating IR operation has non-floating operands");
    }
    if (inst->op == IR_FNEG && !cinder_ir_floating(types[inst->left])) cinder_diag(diags, CINDER_FATAL, inst->loc, "floating negation has a non-floating operand");
    if ((inst->op == IR_NEG || inst->op == IR_BIT_NOT) && !integer(types[inst->left])) cinder_diag(diags, CINDER_FATAL, inst->loc, "integer unary operation has a non-integer operand");
    if (inst->op == IR_COPY && result_fp != cinder_ir_floating(types[inst->left])) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR copy changes scalar register class without a conversion");
    if (inst->op == IR_CONVERT) {
        if (inst->source_type == NULL || !cinder_type_equal(inst->source_type, types[inst->left])) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR conversion source type disagrees with its operand");
        if (!(integer(inst->type) || result_fp) || !(integer(types[inst->left]) || cinder_ir_floating(types[inst->left]))) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR conversion does not have scalar types");
    }
    if (inst->op == IR_PHI)
        for (size_t a = 0U; a < inst->args.len; ++a)
            if (result_fp != cinder_ir_floating(types[inst->args.data[a]])) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR phi mixes scalar register classes");
}

static bool validate_cfg(const CinderIRFunction *function, CinderDiagnostics *diags) {
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        const CinderIRBlock *block = &function->blocks.data[b];
        if (block->id != b) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "IR block ID does not match its table position");
        const CinderTerminator *term = &block->terminator;
        size_t expected = term->kind == TERM_JUMP ? 1U : term->kind == TERM_BRANCH ? (term->yes == term->no ? 1U : 2U) : 0U;
        if (term->kind < TERM_UNREACHABLE || term->kind > TERM_BRANCH || block->successors.len != expected) cinder_diag(diags, CINDER_FATAL, term->loc, "IR terminator and successor table disagree");
        if (term->kind == TERM_JUMP && !contains_block(block->successors.data, block->successors.len, term->target)) cinder_diag(diags, CINDER_FATAL, term->loc, "IR jump is absent from its successor table");
        if (term->kind == TERM_BRANCH && (!contains_block(block->successors.data, block->successors.len, term->yes) || !contains_block(block->successors.data, block->successors.len, term->no))) cinder_diag(diags, CINDER_FATAL, term->loc, "IR branch is absent from its successor table");
        for (size_t s = 0U; s < block->successors.len; ++s) {
            CinderBlockId target = block->successors.data[s];
            if (target >= function->blocks.len) { cinder_diag(diags, CINDER_FATAL, term->loc, "IR successor is outside the block table"); continue; }
            const CinderIRBlock *other = &function->blocks.data[target];
            if (!contains_block(other->predecessors.data, other->predecessors.len, (CinderBlockId)b)) cinder_diag(diags, CINDER_FATAL, term->loc, "IR edge is missing its reciprocal predecessor");
            for (size_t q = s + 1U; q < block->successors.len; ++q) if (target == block->successors.data[q]) cinder_diag(diags, CINDER_FATAL, term->loc, "duplicate IR successor");
        }
        for (size_t p = 0U; p < block->predecessors.len; ++p) {
            CinderBlockId source = block->predecessors.data[p];
            if (source >= function->blocks.len) { cinder_diag(diags, CINDER_FATAL, term->loc, "IR predecessor is outside the block table"); continue; }
            const CinderIRBlock *other = &function->blocks.data[source];
            if (!contains_block(other->successors.data, other->successors.len, (CinderBlockId)b)) cinder_diag(diags, CINDER_FATAL, term->loc, "IR predecessor has no reciprocal successor");
            for (size_t q = p + 1U; q < block->predecessors.len; ++q) if (source == block->predecessors.data[q]) cinder_diag(diags, CINDER_FATAL, term->loc, "duplicate IR predecessor");
        }
    }
    return diags->errors == 0U;
}

int cinder_verify_ir(const CinderIRModule *module, CinderDiagnostics *diags) {
    for (size_t f = 0U; f < module->functions.len && diags->errors == 0U; ++f) {
        const CinderIRFunction *function = &module->functions.data[f];
        if (function->blocks.len == 0U) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "IR function has no entry block"); continue; }
        if (!validate_cfg(function, diags)) continue;
        size_t count = function->value_count == 0U ? 1U : function->value_count;
        CinderBlockId *definitions = cinder_alloc(count * sizeof(*definitions));
        size_t *positions = cinder_alloc(count * sizeof(*positions));
        CinderType **types = cinder_alloc(count * sizeof(*types));
        for (size_t v = 0U; v < count; ++v) { definitions[v] = CINDER_INVALID_BLOCK; positions[v] = SIZE_MAX; types[v] = NULL; }
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            const CinderIRBlock *block = &function->blocks.data[b];
            bool ordinary = false;
            for (size_t i = 0U; i < block->instructions.len; ++i) {
                const CinderIRInst *inst = &block->instructions.data[i];
                if (inst->op < IR_NOP || inst->op > IR_CONVERT) cinder_diag(diags, CINDER_FATAL, inst->loc, "invalid IR opcode");
                if (inst->op == IR_PHI && ordinary) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR phi follows an ordinary instruction");
                if (inst->op != IR_PHI && inst->op != IR_NOP) ordinary = true;
                bool has_result = inst->op != IR_NOP && inst->op != IR_LOCAL_STORE && inst->op != IR_GLOBAL_STORE && !(inst->op == IR_CALL && inst->type != NULL && inst->type->kind == TYPE_VOID);
                if (has_result) {
                    if ((size_t)inst->dst >= function->value_count) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR instruction has no valid result ID");
                    else if (definitions[inst->dst] != CINDER_INVALID_BLOCK) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR result ID has multiple definitions");
                    else { definitions[inst->dst] = (CinderBlockId)b; positions[inst->dst] = i; types[inst->dst] = inst->type; }
                    if (inst->type == NULL) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR result has no type");
                } else if (inst->dst != CINDER_INVALID_VALUE) cinder_diag(diags, CINDER_FATAL, inst->loc, "effect-only IR instruction defines a value");
                if ((inst->op == IR_LOCAL_LOAD || inst->op == IR_LOCAL_STORE || inst->op == IR_PHI) && (inst->slot < 0 || (size_t)inst->slot >= function->local_count)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR local slot is outside storage table");
                if ((inst->op == IR_GLOBAL_LOAD || inst->op == IR_GLOBAL_STORE || inst->op == IR_CALL) && inst->callee == NULL) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR symbol operation has no symbol");
            }
        }
        CinderCFGAnalysis cfg; cinder_cfg_init(&cfg);
        if (diags->errors == 0U && cinder_analyze_cfg(function, &cfg, diags) == 0) {
            for (size_t b = 0U; b < function->blocks.len && diags->errors == 0U; ++b) {
                const CinderIRBlock *block = &function->blocks.data[b];
                for (size_t i = 0U; i < block->instructions.len && diags->errors == 0U; ++i) {
                    const CinderIRInst *inst = &block->instructions.data[i];
                    bool needs_left = binary_integer(inst->op) || binary_float(inst->op) || inst->op == IR_COPY || inst->op == IR_CONVERT || inst->op == IR_NEG || inst->op == IR_FNEG || inst->op == IR_BIT_NOT || inst->op == IR_LOCAL_STORE || inst->op == IR_GLOBAL_STORE;
                    bool needs_right = binary_integer(inst->op) || binary_float(inst->op);
                    if (needs_left) check_use(inst->left, (CinderBlockId)b, i, function, definitions, positions, &cfg, inst->loc, diags);
                    else if (inst->left != CINDER_INVALID_VALUE) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR instruction has an unexpected left operand");
                    if (needs_right) check_use(inst->right, (CinderBlockId)b, i, function, definitions, positions, &cfg, inst->loc, diags);
                    else if (inst->right != CINDER_INVALID_VALUE) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR instruction has an unexpected right operand");
                    if (inst->op == IR_PHI) {
                        if (inst->args.len != block->predecessors.len || inst->phi_blocks.len != inst->args.len) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR phi does not cover every predecessor exactly once");
                        for (size_t p = 0U; p < inst->phi_blocks.len && p < inst->args.len; ++p) {
                            CinderBlockId predecessor = inst->phi_blocks.data[p];
                            if (!contains_block(block->predecessors.data, block->predecessors.len, predecessor)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR phi names a non-predecessor");
                            else check_use(inst->args.data[p], predecessor, function->blocks.data[predecessor].instructions.len, function, definitions, positions, &cfg, inst->loc, diags);
                            for (size_t q = p + 1U; q < inst->phi_blocks.len; ++q) if (predecessor == inst->phi_blocks.data[q]) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR phi repeats an incoming predecessor");
                        }
                    } else if (inst->op == IR_CALL) {
                        if (inst->arg_floats.len != inst->args.len) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR call argument classes are incomplete");
                        for (size_t a = 0U; a < inst->args.len; ++a) check_use(inst->args.data[a], (CinderBlockId)b, i, function, definitions, positions, &cfg, inst->loc, diags);
                    } else if (inst->args.len != 0U || inst->phi_blocks.len != 0U) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR instruction has unexpected variable operands");
                    if (diags->errors == 0U) check_types(inst, types, diags);
                }
                const CinderTerminator *term = &block->terminator;
                if (term->kind == TERM_BRANCH) {
                    check_use(term->condition, (CinderBlockId)b, block->instructions.len, function, definitions, positions, &cfg, term->loc, diags);
                    if ((size_t)term->condition < function->value_count && !integer(types[term->condition])) cinder_diag(diags, CINDER_FATAL, term->loc, "IR branch condition is not integer-valued");
                }
                if (term->kind == TERM_RETURN && term->value != CINDER_INVALID_VALUE) check_use(term->value, (CinderBlockId)b, block->instructions.len, function, definitions, positions, &cfg, term->loc, diags);
            }
        }
        cinder_cfg_destroy(&cfg); free(types); free(positions); free(definitions);
    }
    return diags->errors == 0U ? 0 : 1;
}
