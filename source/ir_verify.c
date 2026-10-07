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

static bool same_value_type(const CinderType *left, const CinderType *right);

static bool member_value_type(const CinderType *actual, const CinderType *field, unsigned qualifiers, unsigned depth) {
    if (actual == NULL || field == NULL || depth >= 64U) return false;
    if (field->kind == TYPE_ARRAY) {
        return actual->kind == TYPE_ARRAY && actual->array_len == field->array_len && actual->size == field->size && actual->qualifiers == field->qualifiers && member_value_type(actual->base, field->base, qualifiers, depth + 1U);
    }
    CinderType qualified = *field; qualified.qualifiers |= qualifiers;
    return cinder_type_equal(actual, &qualified);
}

static void check_types(const CinderIRInst *inst, CinderType *const *types, CinderDiagnostics *diags) {
    bool result_fp = cinder_ir_floating(inst->type);
    if (inst->op == IR_ZERO_INIT && (inst->type->kind != TYPE_CHAR || types[inst->left]->kind != TYPE_POINTER || types[inst->left]->base->kind != TYPE_CHAR || inst->integer < 0 || (uint64_t)inst->integer > 64U * 1024U * 1024U)) cinder_diag(diags, CINDER_FATAL, inst->loc, "zero initializer has no byte pointer or bounded extent");
    if (inst->op == IR_OBJECT_COPY || inst->op == IR_OBJECT_INIT) {
        const CinderType *left = types[inst->left], *right = types[inst->right];
        bool aggregate = inst->type->kind == TYPE_ARRAY || inst->type->kind == TYPE_STRUCT || inst->type->kind == TYPE_UNION;
        if (!aggregate || !inst->type->complete || inst->type->size == 0U || inst->type->size > 64U * 1024U * 1024U || left->kind != TYPE_POINTER || right->kind != TYPE_POINTER || !same_value_type(left->base, inst->type) || !same_value_type(right->base, inst->type)) cinder_diag(diags, CINDER_FATAL, inst->loc, "object transfer has incompatible pointer types or extent");
        if (inst->op == IR_OBJECT_COPY && left->kind == TYPE_POINTER && (left->base->qualifiers & 1U) != 0U) cinder_diag(diags, CINDER_FATAL, inst->loc, "object copy modifies const storage");
    }
    if (inst->op == IR_FCONST || inst->op == IR_FARG || inst->op == IR_FNEG || (inst->op >= IR_FADD && inst->op <= IR_FDIV)) {
        if (!result_fp) cinder_diag(diags, CINDER_FATAL, inst->loc, "floating IR operation has a non-floating result");
    } else if (inst->op == IR_CONST || inst->op == IR_ARG || inst->op == IR_NEG || inst->op == IR_BIT_NOT || binary_integer(inst->op) || (inst->op >= IR_FCMP_EQ && inst->op <= IR_FCMP_GE)) {
        if (!integer(inst->type)) cinder_diag(diags, CINDER_FATAL, inst->loc, "integer IR operation has a non-integer result");
    }
    if (binary_integer(inst->op)) {
        if (!integer(types[inst->left]) || !integer(types[inst->right])) cinder_diag(diags, CINDER_FATAL, inst->loc, "integer IR operation has non-integer operands");
        bool comparison = inst->op >= IR_CMP_EQ && inst->op <= IR_CMP_GE_U;
        if (!comparison && (types[inst->left]->kind == TYPE_POINTER || types[inst->right]->kind == TYPE_POINTER)) cinder_diag(diags, CINDER_FATAL, inst->loc, "pointer arithmetic requires a typed pointer opcode");
    }
    if (binary_float(inst->op)) {
        if (!cinder_ir_floating(types[inst->left]) || !cinder_ir_floating(types[inst->right])) cinder_diag(diags, CINDER_FATAL, inst->loc, "floating IR operation has non-floating operands");
    }
    if (binary_integer(inst->op) || binary_float(inst->op)) {
        bool shift = inst->op == IR_SHL || inst->op == IR_SHR_S || inst->op == IR_SHR_U;
        bool comparison = (inst->op >= IR_CMP_EQ && inst->op <= IR_CMP_GE_U) || (inst->op >= IR_FCMP_EQ && inst->op <= IR_FCMP_GE);
        if (!shift && !same_value_type(types[inst->left], types[inst->right])) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR binary operands have inconsistent widths or types");
        if (!comparison && !same_value_type(inst->type, types[inst->left])) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR binary result has an inconsistent type");
        if (inst->source_type != NULL && !same_value_type(inst->source_type, types[inst->left])) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR binary source type disagrees with operands");
    }
    if (inst->op == IR_FNEG && !cinder_ir_floating(types[inst->left])) cinder_diag(diags, CINDER_FATAL, inst->loc, "floating negation has a non-floating operand");
    if ((inst->op == IR_NEG || inst->op == IR_BIT_NOT) && !integer(types[inst->left])) cinder_diag(diags, CINDER_FATAL, inst->loc, "integer unary operation has a non-integer operand");
    if ((inst->op == IR_NEG || inst->op == IR_BIT_NOT) && types[inst->left]->kind == TYPE_POINTER) cinder_diag(diags, CINDER_FATAL, inst->loc, "integer unary operation cannot consume a pointer");
    if (inst->op == IR_COPY && !same_value_type(inst->type, types[inst->left])) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR copy changes scalar register class without a conversion");
    if (inst->op == IR_CONVERT) {
        if (inst->source_type == NULL || !cinder_type_equal(inst->source_type, types[inst->left])) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR conversion source type disagrees with its operand");
        if (!(integer(inst->type) || result_fp) || !(integer(types[inst->left]) || cinder_ir_floating(types[inst->left]))) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR conversion does not have scalar types");
    }
    if (inst->op == IR_MEMORY_LOAD || inst->op == IR_MEMORY_STORE || inst->op == IR_MEMORY_INIT) {
        const CinderType *pointer = types[inst->left];
        const CinderType *value = inst->op == IR_MEMORY_LOAD ? inst->type : types[inst->right];
        if (pointer->kind != TYPE_POINTER || !same_value_type(pointer->base, inst->type) || !same_value_type(value, inst->type) || !(integer(inst->type) || result_fp)) cinder_diag(diags, CINDER_FATAL, inst->loc, "typed memory access disagrees with pointer or scalar storage");
        if (inst->op == IR_MEMORY_STORE && pointer->kind == TYPE_POINTER && (pointer->base->qualifiers & 1U) != 0U) cinder_diag(diags, CINDER_FATAL, inst->loc, "typed memory store modifies a const-qualified object");
    }
    if (inst->op == IR_POINTER_OFFSET || inst->op == IR_POINTER_DIFF) {
        const CinderType *base = types[inst->left], *right = types[inst->right];
        bool valid = base->kind == TYPE_POINTER && base->base->complete && base->base->size != 0U && base->base->kind != TYPE_VOID && base->base->kind != TYPE_FUNCTION && inst->integer > 0 && (uint64_t)inst->integer == base->base->size;
        if (inst->op == IR_POINTER_OFFSET) valid = valid && same_value_type(base, inst->type) && integer(right) && right->kind != TYPE_POINTER && (inst->operator_code == 1 || inst->operator_code == -1);
        else valid = valid && same_value_type(base, right) && inst->type->kind == TYPE_LONG && !inst->type->is_unsigned;
        if (!valid) cinder_diag(diags, CINDER_FATAL, inst->loc, "pointer arithmetic has invalid types, stride, or direction");
    }
    if (inst->op == IR_POINTER_MEMBER) {
        const CinderType *base = types[inst->left];
        if (base->kind != TYPE_POINTER || (base->base->kind != TYPE_STRUCT && base->base->kind != TYPE_UNION) || inst->type->kind != TYPE_POINTER || inst->slot < 0 || (size_t)inst->slot >= base->base->fields.len) cinder_diag(diags, CINDER_FATAL, inst->loc, "member pointer has no aggregate field");
        else {
            const CinderField *field = &base->base->fields.data[inst->slot];
            if (inst->integer < 0 || (uint64_t)inst->integer != field->offset || !member_value_type(inst->type->base, field->type, base->base->qualifiers, 0U)) cinder_diag(diags, CINDER_FATAL, inst->loc, "member pointer has incorrect offset or type");
        }
    }
    if (inst->op == IR_PHI)
        for (size_t a = 0U; a < inst->args.len; ++a)
            if (!cinder_type_equal(inst->type, types[inst->args.data[a]])) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR phi mixes scalar register classes");
}

static bool same_value_type(const CinderType *left, const CinderType *right) {
    if (left == NULL || right == NULL) return false;
    CinderType a = *left, b = *right; a.qualifiers = 0U; b.qualifiers = 0U;
    return cinder_type_compatible(&a, &b);
}

static void check_storage(const CinderIRModule *module, const CinderIRFunction *function, const CinderIRInst *inst, CinderType *const *types, CinderDiagnostics *diags) {
    if ((inst->op == IR_LOCAL_BEGIN || inst->op == IR_LOCAL_END) && !cinder_type_equal(inst->type, function->local_types.data[inst->slot])) cinder_diag(diags, CINDER_FATAL, inst->loc, "local lifetime type disagrees with storage");
    if (inst->op == IR_LOCAL_ADDRESS && (inst->type->kind != TYPE_POINTER || !cinder_type_equal(inst->type->base, function->local_types.data[inst->slot]))) cinder_diag(diags, CINDER_FATAL, inst->loc, "local address type disagrees with object storage");
    if (inst->op == IR_GLOBAL_ADDRESS) {
        const CinderIRGlobal *global = NULL;
        for (size_t g = 0U; g < module->globals.len; ++g) if (strcmp(module->globals.data[g].name, inst->callee) == 0) global = &module->globals.data[g];
        if (global == NULL || inst->type->kind != TYPE_POINTER || !cinder_type_compatible(inst->type->base, global->type)) cinder_diag(diags, CINDER_FATAL, inst->loc, "global address has an unknown symbol or incorrect object type");
    }
    if (inst->op == IR_LOCAL_LOAD || inst->op == IR_LOCAL_STORE || inst->op == IR_LOCAL_INIT) {
        const CinderType *slot_type = function->local_types.data[inst->slot];
        if (inst->op == IR_LOCAL_STORE && (slot_type->qualifiers & 1U) != 0U) cinder_diag(diags, CINDER_FATAL, inst->loc, "local store modifies a const-qualified object");
        const CinderType *value = inst->op == IR_LOCAL_LOAD ? inst->type : types[inst->left];
        if (!same_value_type(slot_type, value) || !same_value_type(slot_type, inst->type) || !(integer(slot_type) || cinder_ir_floating(slot_type))) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR local scalar access type disagrees with storage");
    }
    if (inst->op == IR_GLOBAL_LOAD || inst->op == IR_GLOBAL_STORE) {
        const CinderIRGlobal *global = NULL;
        for (size_t g = 0U; g < module->globals.len; ++g)
            if (strcmp(module->globals.data[g].name, inst->callee) == 0) global = &module->globals.data[g];
        const CinderType *value = inst->op == IR_GLOBAL_LOAD ? inst->type : types[inst->left];
        if (global == NULL || !same_value_type(global->type, value) || !same_value_type(global->type, inst->type) || !(integer(global->type) || cinder_ir_floating(global->type))) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR global scalar access has an unknown symbol or incorrect type");
        if (global != NULL && inst->op == IR_GLOBAL_STORE && (global->type->qualifiers & 1U) != 0U) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR store modifies a const global");
    }
    if (inst->op == IR_ARG || inst->op == IR_FARG) {
        if (inst->slot < 0 || (size_t)inst->slot >= function->params.len || inst->operator_code < 0) { cinder_diag(diags, CINDER_FATAL, inst->loc, "IR argument has no parameter"); return; }
        size_t ordinal = 0U; bool fp = inst->op == IR_FARG;
        for (size_t p = 0U; p < (size_t)inst->slot; ++p) if (cinder_ir_floating(function->params.data[p]->type) == fp) ++ordinal;
        if ((size_t)inst->operator_code != ordinal || fp != cinder_ir_floating(inst->type) || !same_value_type(inst->type, function->params.data[inst->slot]->type)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR argument class, ordinal, or type disagrees with prototype");
    }
    if (inst->op == IR_FUNCTION_ADDRESS) {
        if (inst->type->kind != TYPE_POINTER || inst->type->base->kind != TYPE_FUNCTION) cinder_diag(diags, CINDER_FATAL, inst->loc, "function address has no function pointer type");
        else for (size_t f = 0U; f < module->functions.len; ++f) if (strcmp(module->functions.data[f].name, inst->callee) == 0 && !cinder_type_compatible(inst->type->base, module->functions.data[f].type)) cinder_diag(diags, CINDER_FATAL, inst->loc, "function address type disagrees with definition");
    }
    if (inst->op == IR_CALL) {
        const CinderType *signature = inst->callee_type;
        if (signature == NULL || signature->kind != TYPE_FUNCTION || signature->return_type == NULL) { cinder_diag(diags, CINDER_FATAL, inst->loc, "IR call has no function signature"); return; }
        if (inst->callee == NULL && (types[inst->left]->kind != TYPE_POINTER || !cinder_type_equal(types[inst->left]->base, signature))) cinder_diag(diags, CINDER_FATAL, inst->loc, "indirect callee type disagrees with function signature");
        if (inst->args.len < signature->params.len || (!signature->variadic && inst->args.len != signature->params.len) || !same_value_type(inst->type, signature->return_type) || inst->floating_result != cinder_ir_floating(inst->type)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR call arity or return class disagrees with signature");
        for (size_t a = 0U; a < inst->args.len; ++a) {
            const CinderType *type = types[inst->args.data[a]];
            if (inst->arg_floats.data[a] != cinder_ir_floating(type)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR call argument class disagrees with operand type");
            if (a < signature->params.len && !same_value_type(type, signature->params.data[a].type)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR call argument type disagrees with prototype");
            if (a >= signature->params.len && (type->kind == TYPE_FLOAT || type->size < 4U)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR variadic argument lacks default promotion");
        }
        for (size_t f = 0U; inst->callee != NULL && f < module->functions.len; ++f)
            if (strcmp(module->functions.data[f].name, inst->callee) == 0 && !cinder_type_compatible(signature, module->functions.data[f].type)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR call signature disagrees with function definition");
    } else if (inst->callee_type != NULL) cinder_diag(diags, CINDER_FATAL, inst->loc, "non-call IR instruction carries a function signature");
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

static const CinderType *pointer_storage(const CinderType *type, size_t offset, unsigned depth) {
    if (type == NULL || depth >= 64U || offset >= type->size) return NULL;
    if (type->kind == TYPE_POINTER) return offset == 0U ? type : NULL;
    if (type->kind == TYPE_ARRAY && type->base->size != 0U) return pointer_storage(type->base, offset % type->base->size, depth + 1U);
    if (type->kind == TYPE_STRUCT || type->kind == TYPE_UNION) {
        for (size_t f = 0U; f < type->fields.len; ++f) {
            const CinderField *field = &type->fields.data[f];
            if (offset >= field->offset && offset - field->offset < field->type->size) {
                const CinderType *found = pointer_storage(field->type, offset - field->offset, depth + 1U);
                if (found != NULL) return found;
            }
        }
    }
    return NULL;
}

static void verify_addresses(const CinderIRModule *module, const CinderIRGlobal *global, CinderDiagnostics *diags) {
    if (global->addresses.len != 0U && (global->is_extern || !global->has_initializer)) { cinder_diag(diags, CINDER_FATAL, global->loc, "address initializer has no owned definition"); return; }
    for (size_t a = 0U; a < global->addresses.len; ++a) {
        const CinderIRAddress *address = &global->addresses.data[a];
        if (address->symbol == NULL || address->symbol[0] == '\0' || address->pointer_type == NULL || address->pointer_type->kind != TYPE_POINTER || address->target_type == NULL) { cinder_diag(diags, CINDER_FATAL, global->loc, "address initializer has no symbol or pointer/target type"); continue; }
        const CinderType *storage = pointer_storage(global->type, address->offset, 0U);
        if (storage == NULL || !same_value_type(storage, address->pointer_type) || global->type->size - address->offset < 8U) cinder_diag(diags, CINDER_FATAL, global->loc, "address initializer is outside compatible pointer storage");
        if (address->function != (address->target_type->kind == TYPE_FUNCTION) || address->domain_begin > address->domain_end || address->domain_end > (address->function ? 1U : address->target_type->size)) cinder_diag(diags, CINDER_FATAL, global->loc, "address initializer has an invalid target domain");
        if (address->function && (address->addend != 0 || address->domain_begin != 0U || address->domain_end != 1U)) cinder_diag(diags, CINDER_FATAL, global->loc, "function address initializer has a displacement");
        bool object_declared = false;
        for (size_t g = 0U; g < module->globals.len; ++g) {
            const CinderIRGlobal *target = &module->globals.data[g];
            if (target->name != NULL && strcmp(target->name, address->symbol) == 0) object_declared = true;
            if (target->name != NULL && strcmp(target->name, address->symbol) == 0 && (address->function || !cinder_type_compatible(target->type, address->target_type))) cinder_diag(diags, CINDER_FATAL, global->loc, "address initializer target disagrees with global declaration");
        }
        if (!address->function && !object_declared) cinder_diag(diags, CINDER_FATAL, global->loc, "address initializer references an undeclared object");
        for (size_t f = 0U; f < module->functions.len; ++f) {
            const CinderIRFunction *target = &module->functions.data[f];
            if (target->name != NULL && strcmp(target->name, address->symbol) == 0 && (!address->function || !cinder_type_compatible(target->type, address->target_type))) cinder_diag(diags, CINDER_FATAL, global->loc, "address initializer target disagrees with function declaration");
        }
        for (size_t earlier = 0U; earlier < a; ++earlier) {
            size_t offset = global->addresses.data[earlier].offset;
            if (offset <= SIZE_MAX - 8U && address->offset <= SIZE_MAX - 8U && offset < address->offset + 8U && address->offset < offset + 8U) cinder_diag(diags, CINDER_FATAL, global->loc, "address initializers overlap");
        }
    }
}

int cinder_verify_ir(const CinderIRModule *module, CinderDiagnostics *diags) {
    for (size_t g = 0U; g < module->globals.len; ++g) {
        const CinderIRGlobal *global = &module->globals.data[g];
        if (global->name == NULL || global->name[0] == '\0' || global->type == NULL || (!global->is_extern && !global->type->complete)) cinder_diag(diags, CINDER_FATAL, global->loc, "IR global has no complete declaration");
        if (global->type != NULL && global->byte_count > global->type->size) cinder_diag(diags, CINDER_FATAL, global->loc, "IR global initializer exceeds object storage");
        if (global->type != NULL) verify_addresses(module, global, diags);
        for (size_t previous = 0U; previous < g && global->name != NULL; ++previous)
            if (module->globals.data[previous].name != NULL && strcmp(global->name, module->globals.data[previous].name) == 0) cinder_diag(diags, CINDER_FATAL, global->loc, "IR global has duplicate definitions");
    }
    for (size_t f = 0U; f < module->functions.len && diags->errors == 0U; ++f) {
        const CinderIRFunction *function = &module->functions.data[f];
        if (function->name == NULL || function->type == NULL || function->type->kind != TYPE_FUNCTION || function->type->return_type == NULL || function->params.len != function->type->params.len || function->local_types.len != function->local_count || function->value_count > 1000000U || function->blocks.len > 65536U) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "IR function declaration or resource bounds are invalid"); continue; }
        bool declarations_valid = true;
        for (size_t l = 0U; l < function->local_types.len; ++l) if (function->local_types.data[l] == NULL || function->local_types.data[l]->kind == TYPE_VOID || !function->local_types.data[l]->complete) declarations_valid = false;
        for (size_t p = 0U; p < function->params.len; ++p) if (function->params.data[p] == NULL || !same_value_type(function->params.data[p]->type, function->type->params.data[p].type)) declarations_valid = false;
        if (!declarations_valid) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "IR storage declarations disagree with function types"); continue; }
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
                if (inst->op < IR_NOP || inst->op > IR_UNDEF) cinder_diag(diags, CINDER_FATAL, inst->loc, "invalid IR opcode");
                if (inst->op == IR_PHI && ordinary) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR phi follows an ordinary instruction");
                if (inst->op != IR_PHI && inst->op != IR_NOP) ordinary = true;
                if (inst->op != IR_NOP && inst->type == NULL) cinder_diag(diags, CINDER_FATAL, inst->loc, "typed IR instruction has no type");
                bool has_result = inst->op != IR_NOP && inst->op != IR_LOCAL_STORE && inst->op != IR_LOCAL_INIT && inst->op != IR_GLOBAL_STORE && inst->op != IR_MEMORY_STORE && inst->op != IR_MEMORY_INIT && inst->op != IR_ZERO_INIT && inst->op != IR_OBJECT_COPY && inst->op != IR_OBJECT_INIT && inst->op != IR_LOCAL_BEGIN && inst->op != IR_LOCAL_END && !(inst->op == IR_CALL && inst->type != NULL && inst->type->kind == TYPE_VOID);
                if (has_result) {
                    if ((size_t)inst->dst >= function->value_count) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR instruction has no valid result ID");
                    else if (definitions[inst->dst] != CINDER_INVALID_BLOCK) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR result ID has multiple definitions");
                    else { definitions[inst->dst] = (CinderBlockId)b; positions[inst->dst] = i; types[inst->dst] = inst->type; }
                    if (inst->type == NULL || inst->type->kind == TYPE_VOID || inst->type->kind == TYPE_ERROR || !inst->type->complete) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR result has no type");
                    if (!(integer(inst->type) || cinder_ir_floating(inst->type))) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR result requires a supported scalar value type");
                } else if (inst->dst != CINDER_INVALID_VALUE) cinder_diag(diags, CINDER_FATAL, inst->loc, "effect-only IR instruction defines a value");
                if ((inst->op == IR_LOCAL_LOAD || inst->op == IR_LOCAL_STORE || inst->op == IR_LOCAL_INIT || inst->op == IR_LOCAL_ADDRESS || inst->op == IR_LOCAL_BEGIN || inst->op == IR_LOCAL_END || inst->op == IR_PHI) && (inst->slot < 0 || (size_t)inst->slot >= function->local_count)) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR local slot is outside storage table");
                if ((inst->op == IR_GLOBAL_LOAD || inst->op == IR_GLOBAL_STORE || inst->op == IR_GLOBAL_ADDRESS || inst->op == IR_FUNCTION_ADDRESS) && inst->callee == NULL) cinder_diag(diags, CINDER_FATAL, inst->loc, "IR symbol operation has no symbol");
            }
        }
        CinderCFGAnalysis cfg; cinder_cfg_init(&cfg);
        if (diags->errors == 0U && cinder_analyze_cfg(function, &cfg, diags) == 0) {
            for (size_t b = 0U; b < function->blocks.len && diags->errors == 0U; ++b) {
                const CinderIRBlock *block = &function->blocks.data[b];
                for (size_t i = 0U; i < block->instructions.len && diags->errors == 0U; ++i) {
                    const CinderIRInst *inst = &block->instructions.data[i];
                    bool needs_left = (inst->op == IR_CALL && inst->callee == NULL) || binary_integer(inst->op) || binary_float(inst->op) || inst->op == IR_COPY || inst->op == IR_CONVERT || inst->op == IR_NEG || inst->op == IR_FNEG || inst->op == IR_BIT_NOT || inst->op == IR_LOCAL_STORE || inst->op == IR_LOCAL_INIT || inst->op == IR_GLOBAL_STORE || inst->op == IR_MEMORY_LOAD || inst->op == IR_MEMORY_STORE || inst->op == IR_MEMORY_INIT || inst->op == IR_ZERO_INIT || inst->op == IR_OBJECT_COPY || inst->op == IR_OBJECT_INIT || inst->op == IR_POINTER_OFFSET || inst->op == IR_POINTER_DIFF || inst->op == IR_POINTER_MEMBER;
                    bool needs_right = binary_integer(inst->op) || binary_float(inst->op) || inst->op == IR_MEMORY_STORE || inst->op == IR_MEMORY_INIT || inst->op == IR_OBJECT_COPY || inst->op == IR_OBJECT_INIT || inst->op == IR_POINTER_OFFSET || inst->op == IR_POINTER_DIFF;
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
                    if (diags->errors == 0U) check_storage(module, function, inst, types, diags);
                }
                const CinderTerminator *term = &block->terminator;
                if (term->kind == TERM_BRANCH) {
                    check_use(term->condition, (CinderBlockId)b, block->instructions.len, function, definitions, positions, &cfg, term->loc, diags);
                    if ((size_t)term->condition < function->value_count && !integer(types[term->condition])) cinder_diag(diags, CINDER_FATAL, term->loc, "IR branch condition is not integer-valued");
                }
                if (term->kind == TERM_RETURN) {
                    if (term->value != CINDER_INVALID_VALUE) {
                        check_use(term->value, (CinderBlockId)b, block->instructions.len, function, definitions, positions, &cfg, term->loc, diags);
                        if (diags->errors == 0U && !same_value_type(types[term->value], function->type->return_type)) cinder_diag(diags, CINDER_FATAL, term->loc, "IR return type disagrees with function");
                    } else if (function->type->return_type->kind != TYPE_VOID) cinder_diag(diags, CINDER_FATAL, term->loc, "non-void IR return has no value");
                }
            }
        }
        cinder_cfg_destroy(&cfg); free(types); free(positions); free(definitions);
    }
    return diags->errors == 0U ? 0 : 1;
}
