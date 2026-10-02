#include "cinder.h"

#include <stdlib.h>

static bool valid_value(CinderValueId value, size_t count) { return value == CINDER_INVALID_VALUE || (size_t)value < count; }

int cinder_verify_ir(const CinderIRModule *module, CinderDiagnostics *diags) {
    for (size_t f = 0U; f < module->functions.len; ++f) {
        const CinderIRFunction *function = &module->functions.data[f];
        if (function->blocks.len == 0U) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "IR function '%s' has no entry block", function->name); continue; }
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            const CinderIRBlock *block = &function->blocks.data[b];
            if (block->terminator.kind == TERM_UNREACHABLE && b == 0U) cinder_diag(diags, CINDER_FATAL, block->terminator.loc, "entry block of '%s' is unterminated", function->name);
            for (size_t i = 0U; i < block->instructions.len; ++i) {
                const CinderIRInst *inst = &block->instructions.data[i];
                if (!valid_value(inst->dst, function->value_count) || !valid_value(inst->left, function->value_count) || !valid_value(inst->right, function->value_count)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR value out of range in '%s'", function->name);
                for (size_t a = 0U; a < inst->args.len; ++a) if (!valid_value(inst->args.data[a], function->value_count)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR call argument out of range in '%s'", function->name);
                if (inst->op == IR_PHI) {
                    if (inst->phi_blocks.len != inst->args.len) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR phi incoming edge/value count differs in '%s'", function->name);
                    for (size_t p = 0U; p < inst->phi_blocks.len; ++p) {
                        if (!valid_value(inst->args.data[p], function->value_count) || inst->phi_blocks.data[p] >= function->blocks.len) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR phi incoming value or block out of range in '%s'", function->name);
                        else {
                            bool predecessor = false;
                            for (size_t q = 0U; q < block->predecessors.len; ++q) if (block->predecessors.data[q] == inst->phi_blocks.data[p]) predecessor = true;
                            if (!predecessor) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR phi names a non-predecessor block in '%s'", function->name);
                        }
                    }
                }
            }
            const CinderTerminator *term = &block->terminator;
            if (!valid_value(term->value, function->value_count) || !valid_value(term->condition, function->value_count)) cinder_diag(diags, CINDER_FATAL, term->loc, "IR terminator value out of range in '%s'", function->name);
            if (term->kind == TERM_JUMP && term->target >= function->blocks.len) cinder_diag(diags, CINDER_FATAL, term->loc, "IR jump target out of range in '%s'", function->name);
            if (term->kind == TERM_BRANCH && (term->yes >= function->blocks.len || term->no >= function->blocks.len)) cinder_diag(diags, CINDER_FATAL, term->loc, "IR branch target out of range in '%s'", function->name);
        }
    }
    return diags->errors == 0U ? 0 : 1;
}
