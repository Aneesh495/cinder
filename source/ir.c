#include "cinder.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static CinderIRBlock make_block(CinderIRFunction *function, const char *name) {
    CinderIRBlock block;
    memset(&block, 0, sizeof(block));
    block.id = (CinderBlockId)function->blocks.len;
    block.name = cinder_strndup(name, strlen(name));
    block.instructions.data = NULL; block.instructions.len = 0U; block.instructions.cap = 0U;
    block.predecessors.data = NULL; block.predecessors.len = 0U; block.predecessors.cap = 0U;
    block.successors.data = NULL; block.successors.len = 0U; block.successors.cap = 0U;
    block.terminator.kind = TERM_UNREACHABLE;
    block.terminator.value = CINDER_INVALID_VALUE;
    block.terminator.target = CINDER_INVALID_BLOCK;
    block.terminator.yes = CINDER_INVALID_BLOCK;
    block.terminator.no = CINDER_INVALID_BLOCK;
    block.terminator.condition = CINDER_INVALID_VALUE;
    return block;
}

static CinderIRBlock *block_at(CinderIRFunction *function, CinderBlockId id) {
    return id < function->blocks.len ? &function->blocks.data[id] : NULL;
}

static CinderValueId new_value(CinderIRFunction *function) {
    if (function->value_count >= (size_t)CINDER_INVALID_VALUE) abort();
    return (CinderValueId)function->value_count++;
}

static CinderIRInst *add_inst_ptr(CinderIRFunction *function, CinderBlockId block_id, CinderIROp op, CinderLoc loc) {
    CinderIRBlock *block = block_at(function, block_id);
    CinderIRInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.op = op; inst.dst = CINDER_INVALID_VALUE; inst.left = CINDER_INVALID_VALUE; inst.right = CINDER_INVALID_VALUE; inst.slot = -1; inst.loc = loc;
    inst.type = op == IR_FCONST ? function->types->double_type : function->types->int_type;
    inst.args.data = NULL; inst.args.len = 0U; inst.args.cap = 0U;
    inst.arg_floats.data = NULL; inst.arg_floats.len = 0U; inst.arg_floats.cap = 0U;
    inst.phi_blocks.data = NULL; inst.phi_blocks.len = 0U; inst.phi_blocks.cap = 0U;
    cinder_vec_push((CinderVec *)&block->instructions, &inst);
    return &block->instructions.data[block->instructions.len - 1U];
}

static void set_successor(CinderIRFunction *function, CinderBlockId from, CinderBlockId to) {
    CinderIRBlock *source = block_at(function, from);
    CinderIRBlock *target = block_at(function, to);
    if (source == NULL || target == NULL) return;
    cinder_vec_push((CinderVec *)&source->successors, &to);
    cinder_vec_push((CinderVec *)&target->predecessors, &from);
}

typedef struct {
    char *name;
    int slot;
    CinderType *type;
} LocalSlot;

typedef struct {
    CinderIRFunction *function;
    CinderIRModule *module;
    CinderDiagnostics *diags;
    CinderBlockId current;
    CINDER_VEC_TYPE(LocalSlot) locals;
    CINDER_VEC_TYPE(CinderBlockId) break_blocks;
    CINDER_VEC_TYPE(CinderBlockId) continue_blocks;
    CINDER_VEC_TYPE(int) active_slots;
    CINDER_VEC_TYPE(size_t) loop_scopes;
    CINDER_VEC_TYPE(size_t) break_scopes;
    CINDER_VEC_TYPE(int) expression_temporaries;
    CinderControlMap control;
} LowerContext;

static int find_local(const LowerContext *context, const char *name) {
    for (size_t i = context->locals.len; i > 0U; --i) if (strcmp(context->locals.data[i - 1U].name, name) == 0) return context->locals.data[i - 1U].slot;
    return -1;
}

static const char *object_symbol(const CinderExpr *expr) {
    CinderDecl *decl = expr->resolved_decl;
    if (decl == NULL || (decl->kind == DECL_VAR && !decl->is_static && !decl->is_extern && decl->canonical == NULL)) return NULL;
    if (decl->canonical != NULL) decl = decl->canonical;
    return decl->storage_symbol == NULL ? decl->name : decl->storage_symbol;
}

static int object_slot(const LowerContext *context, const CinderExpr *expr) {
    if (expr->resolved_decl != NULL && expr->resolved_decl->lowering_slot >= 0) return expr->resolved_decl->lowering_slot;
    return find_local(context, expr->as.name);
}

static CinderValueId lower_expr(LowerContext *context, CinderExpr *expr);
static CinderValueId lower_expr_impl(LowerContext *context, CinderExpr *expr);
static CinderValueId lower_aggregate(LowerContext *context, CinderExpr *expr);
static CinderValueId aggregate_snapshot(LowerContext *context, CinderValueId source, CinderType *type, CinderLoc loc);
static CinderValueId aggregate_temporary(LowerContext *context, CinderType *type, CinderLoc loc, int *slot);
static void freeze_temporary(LowerContext *context, int slot, CinderLoc loc);
static void lower_initializer_plan(LowerContext *context, CinderDecl *decl, int slot);

static bool aggregate_value(const CinderType *type) {
    return type != NULL && (type->kind == TYPE_STRUCT || type->kind == TYPE_UNION);
}

static CinderValueId lower_expr(LowerContext *context, CinderExpr *expr) {
    if (expr != NULL && expr->type != NULL && (expr->type->kind == TYPE_STRUCT || expr->type->kind == TYPE_UNION)) return lower_aggregate(context, expr);
    CinderValueId value = lower_expr_impl(context, expr);
    if (value != CINDER_INVALID_VALUE && expr != NULL) {
        CinderIRBlock *block = block_at(context->function, context->current);
        for (size_t i = block->instructions.len; i > 0U; --i)
            if (block->instructions.data[i - 1U].dst == value) {
                block->instructions.data[i - 1U].type = expr->type;
                break;
            }
    }
    return value;
}

static int new_local(LowerContext *context, CinderType *type) {
    int slot = (int)context->function->local_count++;
    cinder_vec_push((CinderVec *)&context->function->local_types, &type);
    size_t alignment = 0U;
    cinder_vec_push((CinderVec *)&context->function->local_alignments, &alignment);
    return slot;
}

static CinderValueId lower_call(LowerContext *context, CinderExpr *expr) {
    if (expr->as.call.callee->kind == EX_NAME) {
        const char *name = expr->as.call.callee->as.name;
        bool start = strcmp(name, "__cinder_va_start") == 0, copy = strcmp(name, "__cinder_va_copy") == 0, end = strcmp(name, "__cinder_va_end") == 0;
        if (start || copy || end) {
            CinderValueId destination = lower_expr(context, expr->as.call.args.data[0]);
            CinderValueId source = copy ? lower_expr(context, expr->as.call.args.data[1]) : CINDER_INVALID_VALUE;
            CinderIRInst *inst = add_inst_ptr(context->function, context->current, start ? IR_VA_START : copy ? IR_VA_COPY : IR_VA_END, expr->loc);
            inst->type = context->function->types->void_type; inst->left = destination; inst->right = source;
            return CINDER_INVALID_VALUE;
        }
    }
    bool direct = expr->as.call.callee->kind == EX_NAME && expr->as.call.callee->type->kind == TYPE_FUNCTION;
    CinderValueId callee_value = direct ? CINDER_INVALID_VALUE : lower_expr(context, expr->as.call.callee);
    CINDER_VEC_TYPE(CinderValueId) lowered = {NULL, 0U, 0U};
    CINDER_VEC_TYPE(bool) lowered_floats = {NULL, 0U, 0U};
    CinderParamVec actual = {NULL, 0U, 0U};
    int result_slot = -1;
    if (aggregate_value(expr->type)) (void)aggregate_temporary(context, expr->type, expr->loc, &result_slot);
    for (size_t i = 0U; i < expr->as.call.args.len; ++i) {
        CinderValueId arg = lower_expr(context, expr->as.call.args.data[i]);
        CinderType *type = expr->as.call.args.data[i]->type;
        if (aggregate_value(type)) arg = aggregate_snapshot(context, arg, type, expr->as.call.args.data[i]->loc);
        CinderParam parameter = {NULL, type, 0U}; cinder_vec_push((CinderVec *)&actual, &parameter);
        bool is_float = expr->as.call.args.data[i]->type != NULL && (expr->as.call.args.data[i]->type->kind == TYPE_FLOAT || expr->as.call.args.data[i]->type->kind == TYPE_DOUBLE);
        cinder_vec_push((CinderVec *)&lowered, &arg); cinder_vec_push((CinderVec *)&lowered_floats, &is_float);
    }
    CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CALL, expr->loc);
    inst->dst = expr->type != NULL && expr->type->kind == TYPE_VOID ? CINDER_INVALID_VALUE : new_value(context->function); inst->floating_result = expr->type != NULL && (expr->type->kind == TYPE_FLOAT || expr->type->kind == TYPE_DOUBLE);
    inst->type = aggregate_value(expr->type) ? cinder_type_pointer(context->function->types, expr->type) : expr->type;
    CinderDecl *contract = direct ? expr->as.call.callee->resolved_decl : NULL;
    if (contract != NULL && contract->canonical != NULL) contract = contract->canonical;
    inst->noreturn_call = contract != NULL && contract->is_noreturn;
    inst->slot = result_slot;
    inst->source_type = cinder_type_function(context->function->types, expr->type, &actual);
    inst->callee_type = expr->as.call.callee->type;
    if (inst->callee_type->kind == TYPE_POINTER) inst->callee_type = inst->callee_type->base;
    if (direct) inst->callee = cinder_strndup(expr->as.call.callee->as.name, strlen(expr->as.call.callee->as.name));
    else inst->left = callee_value;
    for (size_t i = 0U; i < lowered.len; ++i) { cinder_vec_push((CinderVec *)&inst->args, &lowered.data[i]); cinder_vec_push((CinderVec *)&inst->arg_floats, &lowered_floats.data[i]); }
    free(lowered.data); free(lowered_floats.data); free(actual.data);
    CinderValueId result = inst->dst;
    if (result_slot >= 0) freeze_temporary(context, result_slot, expr->loc);
    return result;
}

static CinderValueId lower_va_arg(LowerContext *context, CinderExpr *expr) {
    CinderValueId list = lower_expr(context, expr->as.va_arg.list);
    int slot = -1;
    if (aggregate_value(expr->type)) (void)aggregate_temporary(context, expr->type, expr->loc, &slot);
    CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_VA_ARG, expr->loc);
    inst->dst = new_value(context->function); inst->left = list; inst->slot = slot;
    inst->source_type = expr->type;
    inst->type = slot >= 0 ? cinder_type_pointer(context->function->types, expr->type) : expr->type;
    CinderValueId result = inst->dst;
    if (slot >= 0) freeze_temporary(context, slot, expr->loc);
    return result;
}

static CinderBlockId create_block(LowerContext *context, const char *name) {
    CinderIRBlock block = make_block(context->function, name);
    CinderBlockId id = block.id;
    cinder_vec_push((CinderVec *)&context->function->blocks, &block);
    return id;
}

static CinderValueId emit_constant(LowerContext *context, int64_t value, CinderLoc loc) {
    CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CONST, loc);
    inst->dst = new_value(context->function);
    inst->integer = value;
    return inst->dst;
}

static CinderValueId emit_typed_constant(LowerContext *context, int64_t value, CinderType *type, CinderLoc loc) {
    CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CONST, loc);
    inst->dst = new_value(context->function); inst->type = type; inst->integer = value; return inst->dst;
}


static CinderValueId emit_operation(LowerContext *context, CinderIROp op, CinderValueId left, CinderValueId right, CinderLoc loc) {
    CinderIRInst *inst = add_inst_ptr(context->function, context->current, op, loc);
    inst->dst = new_value(context->function);
    inst->left = left;
    inst->right = right;
    return inst->dst;
}

static CinderValueId convert_value(LowerContext *context, CinderValueId value, CinderType *from, CinderType *to, CinderLoc loc) {
    if (cinder_type_equal(from, to)) return value;
    if (to->kind == TYPE_VOID) return CINDER_INVALID_VALUE;
    CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CONVERT, loc);
    inst->dst = new_value(context->function); inst->left = value; inst->type = to; inst->source_type = from;
    return inst->dst;
}

static void branch_to(LowerContext *context, CinderValueId condition, CinderBlockId yes, CinderBlockId no, CinderLoc loc) {
    CinderIRBlock *block = block_at(context->function, context->current);
    block->terminator.kind = TERM_BRANCH;
    block->terminator.condition = condition;
    block->terminator.yes = yes;
    block->terminator.no = no;
    block->terminator.loc = loc;
    set_successor(context->function, context->current, yes);
    set_successor(context->function, context->current, no);
}

static void ensure_block_jump(LowerContext *context, CinderBlockId target, CinderLoc loc);

static CinderValueId lower_truth(LowerContext *context, CinderExpr *expr) {
    CinderValueId value = lower_expr(context, expr);
    CinderValueId zero;
    bool floating = expr->type != NULL && (expr->type->kind == TYPE_FLOAT || expr->type->kind == TYPE_DOUBLE);
    if (floating) {
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_FCONST, expr->loc);
        inst->dst = new_value(context->function);
        inst->floating = 0.0; inst->type = expr->type;
        zero = inst->dst;
    } else { zero = emit_constant(context, 0, expr->loc); block_at(context->function, context->current)->instructions.data[block_at(context->function, context->current)->instructions.len - 1U].type = expr->type; }
    CinderValueId truth = emit_operation(context, floating ? IR_FCMP_NE : IR_CMP_NE, value, zero, expr->loc);
    CinderIRBlock *block = block_at(context->function, context->current);
    block->instructions.data[block->instructions.len - 1U].source_type = expr->type;
    return truth;
}

static void store_slot(LowerContext *context, int slot, CinderValueId value, CinderLoc loc, bool initializing) {
    CinderIRInst *store = add_inst_ptr(context->function, context->current, initializing ? IR_LOCAL_INIT : IR_LOCAL_STORE, loc);
    store->left = value; store->type = context->function->local_types.data[slot];
    store->slot = slot;
}

static char *literal_storage(LowerContext *context, CinderType *type, const CinderExpr *literal) {
    if (type->size == 0U || type->size > 64U * 1024U * 1024U) { cinder_diag(context->diags, CINDER_ERROR, literal->loc, "literal storage exceeds the target object limit"); return NULL; }
    CinderIRGlobal global; memset(&global, 0, sizeof(global));
    char name[64]; int length = snprintf(name, sizeof(name), ".LCS.%zu", context->module->globals.len);
    if (length < 0 || (size_t)length >= sizeof(name)) { cinder_diag(context->diags, CINDER_FATAL, literal->loc, "literal symbol exceeds storage"); return NULL; }
    global.name = cinder_strndup(name, (size_t)length); global.type = type;
    global.read_only = true; global.has_initializer = true; global.loc = literal->loc;
    global.byte_count = type->size; global.bytes = cinder_arena_alloc(&context->module->arena, type->size, _Alignof(char));
    memset(global.bytes, 0, type->size);
    size_t count = literal->literal_length + 1U; if (count > type->size) count = type->size;
    memcpy(global.bytes, literal->as.string, count);
    cinder_vec_push((CinderVec *)&context->module->globals, &global); return global.name;
}

static CinderValueId lower_address(LowerContext *context, CinderExpr *target) {
    if (target->kind == EX_GENERIC) {
        if (target->as.generic.selected >= target->as.generic.associations.len) { cinder_diag(context->diags, CINDER_FATAL, target->loc, "unresolved generic address expression"); return CINDER_INVALID_VALUE; }
        return lower_address(context, target->as.generic.associations.data[target->as.generic.selected].value);
    }
    if (target->kind == EX_COMPOUND_LITERAL) {
        CinderDecl *decl = target->as.compound_literal;
        if (decl->is_static) {
            if (!cinder_lower_static_object(context->module, context->function->ast, decl, context->diags)) return CINDER_INVALID_VALUE;
        } else {
            if (decl->lowering_slot < 0) { cinder_diag(context->diags, CINDER_FATAL, target->loc, "compound literal has no enclosing storage scope"); return CINDER_INVALID_VALUE; }
            lower_initializer_plan(context, decl, decl->lowering_slot);
        }
        CinderIRInst *address = add_inst_ptr(context->function, context->current, decl->is_static ? IR_GLOBAL_ADDRESS : IR_LOCAL_ADDRESS, target->loc);
        address->dst = new_value(context->function); address->type = cinder_type_pointer(context->function->types, decl->type);
        if (decl->is_static) address->callee = cinder_strndup(decl->name, strlen(decl->name));
        else address->slot = decl->lowering_slot;
        return address->dst;
    }
    if (target->kind == EX_STRING) {
        char *name = literal_storage(context, target->type, target); if (name == NULL) return CINDER_INVALID_VALUE;
        CinderIRInst *address = add_inst_ptr(context->function, context->current, IR_GLOBAL_ADDRESS, target->loc);
        address->dst = new_value(context->function); address->type = cinder_type_pointer(context->function->types, target->type);
        address->callee = cinder_strndup(name, strlen(name)); return address->dst;
    }
    if (target->kind == EX_NAME) {
        const char *symbol = object_symbol(target);
        int slot = symbol == NULL ? object_slot(context, target) : -1;
        CinderIRInst *address = add_inst_ptr(context->function, context->current, target->type->kind == TYPE_FUNCTION ? IR_FUNCTION_ADDRESS : slot >= 0 ? IR_LOCAL_ADDRESS : IR_GLOBAL_ADDRESS, target->loc);
        address->dst = new_value(context->function); address->type = cinder_type_pointer(context->function->types, target->type); address->slot = slot;
        if (slot < 0) { if (symbol == NULL) symbol = target->as.name; address->callee = cinder_strndup(symbol, strlen(symbol)); }
        return address->dst;
    }
    if (target->kind == EX_UNARY && target->as.unary.op == '*') return lower_expr(context, target->as.unary.value);
    if (target->kind == EX_INDEX) {
        CinderValueId base = lower_expr(context, target->as.index.base), index = lower_expr(context, target->as.index.index);
        CinderIRInst *offset = add_inst_ptr(context->function, context->current, IR_POINTER_OFFSET, target->loc);
        offset->dst = new_value(context->function); offset->left = base; offset->right = index; offset->integer = (int64_t)target->type->size; offset->operator_code = 1;
        offset->type = cinder_type_pointer(context->function->types, target->type); return offset->dst;
    }
    if (target->kind == EX_MEMBER) {
        CinderExpr *base = target->as.member.base;
        CinderValueId address = target->as.member.arrow ? lower_expr(context, base) : lower_aggregate(context, base);
        CinderType *aggregate = target->as.member.arrow ? base->type->base : base->type;
        for (size_t p = 0U; p < target->as.member.path.len; ++p) {
            size_t index = target->as.member.path.data[p]; const CinderField *field = &aggregate->fields.data[index];
            CinderType *child = cinder_type_qualified(context->function->types, field->type, field->type->qualifiers | aggregate->qualifiers);
            CinderIRInst *member = add_inst_ptr(context->function, context->current, IR_POINTER_MEMBER, target->loc);
            member->dst = new_value(context->function); member->left = address; member->integer = (int64_t)field->offset;
            member->slot = (int)index; member->type = cinder_type_pointer(context->function->types, child); address = member->dst; aggregate = child;
        }
        return address;
    }
    cinder_diag(context->diags, CINDER_ERROR, target->loc, "unsupported addressable expression"); return CINDER_INVALID_VALUE;
}

static CinderValueId load_address(LowerContext *context, CinderValueId address, CinderType *type, CinderLoc loc) {
    CinderIRInst *load = add_inst_ptr(context->function, context->current, IR_MEMORY_LOAD, loc);
    load->dst = new_value(context->function); load->left = address; load->type = type; return load->dst;
}

static CinderValueId load_lvalue(LowerContext *context, CinderExpr *target, CinderValueId address, CinderLoc loc) {
    if (target->bit_width == 0U) return load_address(context, address, target->type, loc);
    CinderIRInst *load = add_inst_ptr(context->function, context->current, IR_BIT_LOAD, loc);
    load->dst = new_value(context->function); load->left = address; load->type = target->type;
    load->integer = (int64_t)target->bit_width; load->operator_code = (int)target->bit_offset; return load->dst;
}

static CinderValueId bitfield_value(LowerContext *context, CinderExpr *target, CinderValueId value, CinderLoc loc) {
    if (target->bit_width == 0U) return value;
    CinderIRInst *convert = add_inst_ptr(context->function, context->current, IR_BIT_CONVERT, loc);
    convert->dst = new_value(context->function); convert->left = value; convert->type = target->type;
    convert->integer = (int64_t)target->bit_width; return convert->dst;
}

static void store_lvalue(LowerContext *context, CinderExpr *target, CinderValueId address, CinderValueId value, CinderLoc loc) {
    if (address != CINDER_INVALID_VALUE) {
        CinderIRInst *store = add_inst_ptr(context->function, context->current, target->bit_width == 0U ? IR_MEMORY_STORE : IR_BIT_STORE, loc);
        store->integer = (int64_t)target->bit_width; store->operator_code = (int)target->bit_offset;
        store->left = address; store->right = value; store->type = target->type; return;
    }
    const char *symbol = object_symbol(target);
    int slot = symbol == NULL ? object_slot(context, target) : -1;
    if (slot >= 0) store_slot(context, slot, value, loc, false);
    else if (symbol != NULL) {
        CinderIRInst *store = add_inst_ptr(context->function, context->current, IR_GLOBAL_STORE, loc);
        store->left = value; store->type = target->type; store->callee = cinder_strndup(symbol, strlen(symbol));
    } else cinder_diag(context->diags, CINDER_ERROR, loc, "unknown assignment storage");
}

static CinderValueId pointer_offset(LowerContext *context, CinderValueId base, CinderValueId index, CinderType *type, bool subtract, CinderLoc loc) {
    CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_POINTER_OFFSET, loc);
    inst->dst = new_value(context->function); inst->left = base; inst->right = index; inst->type = type; inst->integer = (int64_t)type->base->size; inst->operator_code = subtract ? -1 : 1;
    return inst->dst;
}

static CinderValueId lower_choice(LowerContext *context, CinderExpr *condition, CinderExpr *yes, CinderExpr *no, bool logical, bool conjunction, CinderLoc loc) {
    CinderType *result_type = logical ? context->function->types->int_type : yes->type;
    int slot = new_local(context, result_type);
    CinderValueId test = lower_truth(context, condition);
    CinderBlockId yes_block = create_block(context, "choice.yes");
    CinderBlockId no_block = create_block(context, "choice.no");
    CinderBlockId merge = create_block(context, "choice.merge");
    branch_to(context, test, yes_block, no_block, loc);
    context->current = yes_block;
    CinderValueId yes_value = logical ? (conjunction ? lower_truth(context, yes) : emit_constant(context, 1, loc)) : lower_expr(context, yes);
    /* Each selected arm initializes the expression's internal result object.
     * Top-level qualifiers inherited from a const aggregate member cannot
     * turn that initialization into a forbidden ordinary source write. */
    store_slot(context, slot, yes_value, loc, true);
    ensure_block_jump(context, merge, loc);
    context->current = no_block;
    CinderValueId no_value = logical ? (conjunction ? emit_constant(context, 0, loc) : lower_truth(context, no)) : lower_expr(context, no);
    store_slot(context, slot, no_value, loc, true);
    ensure_block_jump(context, merge, loc);
    context->current = merge;
    CinderIRInst *load = add_inst_ptr(context->function, merge, IR_LOCAL_LOAD, loc);
    load->dst = new_value(context->function);
    load->slot = slot;
    return load->dst;
}

static CinderValueId aggregate_temporary(LowerContext *context, CinderType *type, CinderLoc loc, int *slot) {
    *slot = new_local(context, type);
    CinderIRInst *begin = add_inst_ptr(context->function, context->current, IR_LOCAL_BEGIN, loc);
    begin->slot = *slot; begin->type = type;
    cinder_vec_push((CinderVec *)&context->expression_temporaries, slot);
    CinderIRInst *address = add_inst_ptr(context->function, context->current, IR_LOCAL_ADDRESS, loc);
    address->dst = new_value(context->function); address->slot = *slot;
    address->type = cinder_type_pointer(context->function->types, type);
    return address->dst;
}

static void aggregate_transfer(LowerContext *context, CinderValueId destination, CinderValueId source, CinderType *type, bool initializing, CinderLoc loc) {
    CinderIRInst *copy = add_inst_ptr(context->function, context->current, initializing ? IR_OBJECT_INIT : IR_OBJECT_COPY, loc);
    copy->left = destination; copy->right = source; copy->type = type;
}

static void freeze_temporary(LowerContext *context, int slot, CinderLoc loc) {
    CinderIRInst *freeze = add_inst_ptr(context->function, context->current, IR_LOCAL_FREEZE, loc);
    freeze->slot = slot; freeze->type = context->function->local_types.data[slot];
}

static CinderValueId aggregate_snapshot(LowerContext *context, CinderValueId source, CinderType *type, CinderLoc loc) {
    int slot;
    CinderValueId destination = aggregate_temporary(context, type, loc, &slot);
    aggregate_transfer(context, destination, source, type, true, loc);
    freeze_temporary(context, slot, loc);
    return destination;
}

static CinderValueId lower_aggregate(LowerContext *context, CinderExpr *expr) {
    if (expr->kind == EX_GENERIC) {
        if (expr->as.generic.selected >= expr->as.generic.associations.len) { cinder_diag(context->diags, CINDER_FATAL, expr->loc, "unresolved generic aggregate expression"); return CINDER_INVALID_VALUE; }
        return lower_aggregate(context, expr->as.generic.associations.data[expr->as.generic.selected].value);
    }
    if (expr->is_lvalue || expr->kind == EX_MEMBER) return lower_address(context, expr);
    if (expr->kind == EX_CALL) return lower_call(context, expr);
    if (expr->kind == EX_VA_ARG) return lower_va_arg(context, expr);
    if (expr->kind == EX_ASSIGN) {
        CinderValueId destination = lower_address(context, expr->as.assign.target);
        CinderValueId source = lower_aggregate(context, expr->as.assign.value);
        aggregate_transfer(context, destination, source, expr->type, false, expr->loc);
        return aggregate_snapshot(context, destination, expr->type, expr->loc);
    }
    if (expr->kind == EX_BINARY && expr->as.binary.op == ',') {
        (void)lower_expr(context, expr->as.binary.left);
        CinderValueId source = lower_aggregate(context, expr->as.binary.right);
        return aggregate_snapshot(context, source, expr->type, expr->loc);
    }
    if (expr->kind == EX_CONDITIONAL) {
        int slot;
        CinderValueId destination = aggregate_temporary(context, expr->type, expr->loc, &slot);
        CinderValueId test = lower_truth(context, expr->as.conditional.condition);
        CinderBlockId yes = create_block(context, "aggregate.yes"), no = create_block(context, "aggregate.no"), merge = create_block(context, "aggregate.merge");
        branch_to(context, test, yes, no, expr->loc);
        context->current = yes;
        CinderValueId source = lower_aggregate(context, expr->as.conditional.yes);
        aggregate_transfer(context, destination, source, expr->type, true, expr->loc);
        ensure_block_jump(context, merge, expr->loc);
        context->current = no;
        source = lower_aggregate(context, expr->as.conditional.no);
        aggregate_transfer(context, destination, source, expr->type, true, expr->loc);
        ensure_block_jump(context, merge, expr->loc);
        context->current = merge; freeze_temporary(context, slot, expr->loc);
        return destination;
    }
    cinder_diag(context->diags, CINDER_ERROR, expr->loc, "aggregate expression requires implemented value lowering");
    return CINDER_INVALID_VALUE;
}

static void end_expression(LowerContext *context, size_t mark, CinderLoc loc) {
    for (size_t i = context->expression_temporaries.len; i > mark; --i) {
        int slot = context->expression_temporaries.data[i - 1U];
        CinderIRInst *end = add_inst_ptr(context->function, context->current, IR_LOCAL_END, loc);
        end->slot = slot; end->type = context->function->local_types.data[slot];
    }
    context->expression_temporaries.len = mark;
}

static CinderValueId lower_full_expression(LowerContext *context, CinderExpr *expr, bool truth, CinderLoc loc) {
    size_t mark = context->expression_temporaries.len;
    CinderValueId value = truth ? lower_truth(context, expr) : lower_expr(context, expr);
    end_expression(context, mark, loc);
    return value;
}

static CinderIROp compound_operation(int op, bool floating, bool unsig) {
    switch (op) {
        case TOK_PLUSEQ: return floating ? IR_FADD : IR_ADD;
        case TOK_MINUSEQ: return floating ? IR_FSUB : IR_SUB;
        case TOK_STAREQ: return floating ? IR_FMUL : IR_MUL;
        case TOK_SLASHEQ: return floating ? IR_FDIV : (unsig ? IR_DIV_U : IR_DIV_S);
        case TOK_PERCENTEQ: return unsig ? IR_MOD_U : IR_MOD_S;
        case TOK_ANDEQ: return IR_BIT_AND;
        case TOK_OREQ: return IR_BIT_OR;
        case TOK_XOREQ: return IR_BIT_XOR;
        case TOK_LSHIFT_EQ: return IR_SHL;
        case TOK_RSHIFT_EQ: return unsig ? IR_SHR_U : IR_SHR_S;
        default: return IR_COPY;
    }
}

static CinderValueId lower_expr_impl(LowerContext *context, CinderExpr *expr) {
    if (expr == NULL) return CINDER_INVALID_VALUE;
    if (expr->kind == EX_OFFSETOF) {
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CONST, expr->loc);
        inst->dst = new_value(context->function); inst->integer = (int64_t)expr->as.offset.value; return inst->dst;
    }
    if (expr->kind == EX_GENERIC) {
        if (expr->as.generic.selected >= expr->as.generic.associations.len) { cinder_diag(context->diags, CINDER_FATAL, expr->loc, "unresolved generic value expression"); return CINDER_INVALID_VALUE; }
        return lower_expr(context, expr->as.generic.associations.data[expr->as.generic.selected].value);
    }
    if (expr->kind == EX_COMPOUND_LITERAL) return load_lvalue(context, expr, lower_address(context, expr), expr->loc);
    if (expr->kind == EX_INT || expr->kind == EX_CHAR) {
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CONST, expr->loc);
        inst->dst = new_value(context->function); inst->integer = expr->as.integer; return inst->dst;
    }
    if (expr->kind == EX_FLOAT) {
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_FCONST, expr->loc);
        inst->dst = new_value(context->function); inst->floating = expr->as.floating; return inst->dst;
    }
    if (expr->kind == EX_DECAY) {
        CinderExpr *object = expr->as.unary.value;
        CinderValueId address = lower_address(context, object);
        return convert_value(context, address, cinder_type_pointer(context->function->types, object->type), expr->type, expr->loc);
    }
    if (expr->kind == EX_INDEX || expr->kind == EX_MEMBER) return load_lvalue(context, expr, lower_address(context, expr), expr->loc);
    if (expr->kind == EX_NAME) {
        const char *symbol = object_symbol(expr);
        int slot = symbol == NULL ? object_slot(context, expr) : -1;
        if (slot >= 0) {
            CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_LOCAL_LOAD, expr->loc);
            inst->dst = new_value(context->function); inst->slot = slot; return inst->dst;
        }
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_GLOBAL_LOAD, expr->loc);
        inst->dst = new_value(context->function); if (symbol == NULL) symbol = expr->as.name; inst->callee = cinder_strndup(symbol, strlen(symbol)); return inst->dst;
    }
    if (expr->kind == EX_VA_ARG) {
        return lower_va_arg(context, expr);
    }
    if (expr->kind == EX_CALL) return lower_call(context, expr);
    if (expr->kind == EX_CONDITIONAL) return lower_choice(context, expr->as.conditional.condition, expr->as.conditional.yes, expr->as.conditional.no, false, false, expr->loc);
    if (expr->kind == EX_ASSIGN) {
        CinderExpr *target = expr->as.assign.target;
        CinderValueId address = target->kind == EX_NAME ? CINDER_INVALID_VALUE : lower_address(context, target);
        CinderValueId previous = CINDER_INVALID_VALUE;
        if (expr->as.assign.op != '=') previous = address == CINDER_INVALID_VALUE ? lower_expr(context, target) : load_lvalue(context, target, address, expr->loc);
        CinderValueId value = lower_expr(context, expr->as.assign.value);
        if (expr->as.assign.op != '=' && expr->type->kind == TYPE_POINTER) value = pointer_offset(context, previous, value, expr->type, expr->as.assign.op == TOK_MINUSEQ, expr->loc);
        else if (expr->as.assign.op != '=') {
            CinderType *operation_type = expr->as.assign.operation_type;
            previous = convert_value(context, previous, expr->as.assign.target->type, operation_type, expr->loc);
            bool floating = cinder_ir_floating(operation_type);
            bool unsig = operation_type->is_unsigned;
            value = emit_operation(context, compound_operation(expr->as.assign.op, floating, unsig), previous, value, expr->loc);
            CinderIRInst *operation = &block_at(context->function, context->current)->instructions.data[block_at(context->function, context->current)->instructions.len - 1U];
            operation->type = operation_type; operation->source_type = operation_type;
            value = convert_value(context, value, operation_type, expr->type, expr->loc);
        }
        value = bitfield_value(context, target, value, expr->loc);
        store_lvalue(context, target, address, value, expr->loc);
        return value;
    }
    if (expr->kind == EX_UNARY) {
        int op = expr->as.unary.op;
        if (op == TOK_PLUSPLUS || op == TOK_MINUSMINUS) {
            CinderExpr *target = expr->as.unary.value;
            CinderValueId address = target->kind == EX_NAME ? CINDER_INVALID_VALUE : lower_address(context, target);
            CinderValueId old = address == CINDER_INVALID_VALUE ? lower_expr(context, target) : load_lvalue(context, target, address, expr->loc);
            if (target->type->kind == TYPE_POINTER) {
                CinderValueId one = emit_constant(context, 1, expr->loc);
                CinderValueId updated = pointer_offset(context, old, one, target->type, op == TOK_MINUSMINUS, expr->loc);
                store_lvalue(context, target, address, updated, expr->loc); return expr->as.unary.postfix ? old : updated;
            }
            CinderType *target_type = expr->as.unary.value->type;
            CinderType *operation_type = target->bit_width != 0U && target->bit_width < 32U ? context->function->types->int_type : cinder_ir_floating(target_type) ? target_type : cinder_integer_promote(context->function->types, target_type);
            CinderValueId operand = convert_value(context, old, target_type, operation_type, expr->loc);
            CinderValueId one_value = new_value(context->function);
            bool floating = cinder_ir_floating(operation_type);
            CinderIRInst *one = add_inst_ptr(context->function, context->current, floating ? IR_FCONST : IR_CONST, expr->loc); one->dst = one_value; one->integer = 1; one->floating = 1.0; one->type = operation_type;
            CinderIROp add_op = floating ? (op == TOK_PLUSPLUS ? IR_FADD : IR_FSUB) : (op == TOK_PLUSPLUS ? IR_ADD : IR_SUB);
            CinderIRInst *add = add_inst_ptr(context->function, context->current, add_op, expr->loc); add->dst = new_value(context->function); add->left = operand; add->right = one_value; add->loc = expr->loc; add->type = operation_type; add->source_type = operation_type;
            CinderValueId updated = add->dst;
            updated = convert_value(context, updated, operation_type, target_type, expr->loc);
            updated = bitfield_value(context, target, updated, expr->loc);
            store_lvalue(context, target, address, updated, expr->loc);
            return expr->as.unary.postfix ? old : updated;
        }
        if (op == '!') {
            CinderValueId truth = lower_truth(context, expr->as.unary.value);
            CinderValueId zero = emit_constant(context, 0, expr->loc);
            return emit_operation(context, IR_CMP_EQ, truth, zero, expr->loc);
        }
        if (op == '&') return lower_address(context, expr->as.unary.value);
        if (op == '*') return load_address(context, lower_expr(context, expr->as.unary.value), expr->type, expr->loc);
        CinderValueId value = lower_expr(context, expr->as.unary.value);
        CinderIROp ir_op = expr->type != NULL && (expr->type->kind == TYPE_FLOAT || expr->type->kind == TYPE_DOUBLE) && op == '-' ? IR_FNEG : (op == '-' ? IR_NEG : (op == '~' ? IR_BIT_NOT : IR_COPY));
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, ir_op, expr->loc); inst->dst = new_value(context->function); inst->left = value; inst->source_type = expr->as.unary.value->type; return inst->dst;
    }
    if (expr->kind == EX_BINARY) {
        int operator_code = expr->as.binary.op;
        if (operator_code == TOK_ANDAND || operator_code == TOK_OROR)
            return lower_choice(context, expr->as.binary.left, expr->as.binary.right, expr->as.binary.right, true, operator_code == TOK_ANDAND, expr->loc);
        if (operator_code == ',') { (void)lower_expr(context, expr->as.binary.left); return lower_expr(context, expr->as.binary.right); }
        CinderValueId left = lower_expr(context, expr->as.binary.left); CinderValueId right = lower_expr(context, expr->as.binary.right);
        if (expr->as.binary.left->type->kind == TYPE_POINTER && (operator_code == '+' || operator_code == '-')) {
            if (expr->as.binary.right->type->kind != TYPE_POINTER) return pointer_offset(context, left, right, expr->type, operator_code == '-', expr->loc);
            CinderIRInst *difference = add_inst_ptr(context->function, context->current, IR_POINTER_DIFF, expr->loc);
            difference->dst = new_value(context->function); difference->left = left; difference->right = right; difference->type = expr->type; difference->integer = (int64_t)expr->as.binary.left->type->base->size; return difference->dst;
        }
        if (cinder_ir_floating(expr->as.binary.left->type) || cinder_ir_floating(expr->as.binary.right->type)) {
            CinderIROp fp_op = IR_FADD;
            switch (expr->as.binary.op) { case '+': fp_op = IR_FADD; break; case '-': fp_op = IR_FSUB; break; case '*': fp_op = IR_FMUL; break; case '/': fp_op = IR_FDIV; break; case TOK_EQEQ: fp_op = IR_FCMP_EQ; break; case TOK_NEQ: fp_op = IR_FCMP_NE; break; case '<': fp_op = IR_FCMP_LT; break; case TOK_LE: fp_op = IR_FCMP_LE; break; case '>': fp_op = IR_FCMP_GT; break; case TOK_GE: fp_op = IR_FCMP_GE; break; default: break; }
            CinderIRInst *inst = add_inst_ptr(context->function, context->current, fp_op, expr->loc); inst->dst = new_value(context->function); inst->left = left; inst->right = right; inst->source_type = expr->as.binary.left->type; return inst->dst;
        }
        CinderIROp op = IR_ADD;
        switch (expr->as.binary.op) {
            case '+': op = IR_ADD; break; case '-': op = IR_SUB; break; case '*': op = IR_MUL; break; case '/': op = IR_DIV_S; break; case '%': op = IR_MOD_S; break;
            case '&': op = IR_BIT_AND; break; case '|': op = IR_BIT_OR; break; case '^': op = IR_BIT_XOR; break; case TOK_SHL: op = IR_SHL; break; case TOK_SHR: op = IR_SHR_S; break;
            case TOK_EQEQ: op = IR_CMP_EQ; break; case TOK_NEQ: op = IR_CMP_NE; break; case '<': op = IR_CMP_LT_S; break; case TOK_LE: op = IR_CMP_LE_S; break; case '>': op = IR_CMP_GT_S; break; case TOK_GE: op = IR_CMP_GE_S; break;
            case TOK_ANDAND: op = IR_BIT_AND; break; case TOK_OROR: op = IR_BIT_OR; break; default: break;
        }
        bool unsig = expr->as.binary.left->type->is_unsigned || expr->as.binary.left->type->kind == TYPE_POINTER;
        if (unsig) {
            switch (op) {
                case IR_DIV_S: op = IR_DIV_U; break; case IR_MOD_S: op = IR_MOD_U; break; case IR_SHR_S: op = IR_SHR_U; break;
                case IR_CMP_LT_S: op = IR_CMP_LT_U; break; case IR_CMP_LE_S: op = IR_CMP_LE_U; break; case IR_CMP_GT_S: op = IR_CMP_GT_U; break; case IR_CMP_GE_S: op = IR_CMP_GE_U; break;
                default: break;
            }
        }
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, op, expr->loc); inst->dst = new_value(context->function); inst->left = left; inst->right = right; inst->source_type = expr->as.binary.left->type; return inst->dst;
    }
    if (expr->kind == EX_SIZEOF || expr->kind == EX_ALIGNOF) {
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CONST, expr->loc); inst->dst = new_value(context->function); inst->integer = expr->kind == EX_SIZEOF ? (int64_t)expr->queried_type->size : (int64_t)expr->queried_type->align; return inst->dst;
    }
    if (expr->kind == EX_CAST) {
        CinderValueId value = lower_expr(context, expr->as.cast.value);
        return convert_value(context, value, expr->as.cast.value->type, expr->type, expr->loc);
    }
    cinder_diag(context->diags, CINDER_ERROR, expr->loc, "expression lowering is not implemented for this source construct");
    return CINDER_INVALID_VALUE;
}

static void ensure_block_jump(LowerContext *context, CinderBlockId target, CinderLoc loc) {
    CinderIRBlock *block = block_at(context->function, context->current);
    if (block->terminator.kind == TERM_UNREACHABLE) {
        block->terminator.kind = TERM_JUMP; block->terminator.target = target; block->terminator.loc = loc; set_successor(context->function, context->current, target);
    }
}

static void begin_declarations(LowerContext *context, CinderDecl *first) {
    for (CinderDecl *decl = first; decl != NULL; decl = decl->next) {
        if (decl->kind != DECL_VAR || decl->name == NULL || decl->is_static || decl->is_extern) continue;
        if (decl->lowering_slot < 0) decl->lowering_slot = new_local(context, decl->type);
        context->function->local_alignments.data[decl->lowering_slot] = decl->alignment;
        CinderIRInst *begin = add_inst_ptr(context->function, context->current, IR_LOCAL_BEGIN, decl->loc); begin->slot = decl->lowering_slot; begin->type = decl->type;
        cinder_vec_push((CinderVec *)&context->active_slots, &decl->lowering_slot);
    }
}

static void end_scope(LowerContext *context, size_t mark, CinderLoc loc) {
    for (size_t i = context->active_slots.len; i > mark; --i) {
        int slot = context->active_slots.data[i - 1U];
        CinderIRInst *end = add_inst_ptr(context->function, context->current, IR_LOCAL_END, loc); end->slot = slot; end->type = context->function->local_types.data[slot];
    }
}

static void prepare_control(LowerContext *context, CinderStmt *body) {
    cinder_control_init(&context->control);
    (void)cinder_control_build(&context->control, body, context->diags);
    for (size_t s = 0U; s < context->control.scopes.len; ++s) {
        CinderControlScope *scope = &context->control.scopes.data[s];
        for (size_t d = 0U; d < scope->objects.len; ++d) {
            CinderDecl *decl = scope->objects.data[d]; decl->lowering_slot = new_local(context, decl->type);
            context->function->local_alignments.data[decl->lowering_slot] = decl->alignment;
        }
    }
    for (size_t l = 0U; l < context->control.labels.len; ++l) {
        CinderStmt *label = context->control.labels.data[l]; label->as.label.block = create_block(context, label->as.label.name);
    }
    for (size_t l = 0U; l < context->control.cases.len; ++l) {
        CinderStmt *label = context->control.cases.data[l]; label->as.case_label.block = create_block(context, label->kind == ST_CASE ? "switch.case" : "switch.default");
    }
}

static bool ancestor_scope(const CinderControlMap *map, int ancestor, int scope) {
    for (int current = scope; current >= 0; current = map->scopes.data[current].parent) if (current == ancestor) return true;
    return ancestor < 0;
}

static void transition_scopes(LowerContext *context, int from, int to, CinderLoc loc) {
    int common = from;
    while (!ancestor_scope(&context->control, common, to)) common = context->control.scopes.data[common].parent;
    for (int scope = from; scope != common; scope = context->control.scopes.data[scope].parent) {
        CinderControlScope *leaving = &context->control.scopes.data[scope];
        for (size_t d = leaving->objects.len; d > 0U; --d) {
            CinderDecl *decl = leaving->objects.data[d - 1U];
            CinderIRInst *end = add_inst_ptr(context->function, context->current, IR_LOCAL_END, loc); end->slot = decl->lowering_slot; end->type = decl->type;
        }
    }
    CINDER_VEC_TYPE(int) entering = {NULL, 0U, 0U};
    for (int scope = to; scope != common; scope = context->control.scopes.data[scope].parent) cinder_vec_push((CinderVec *)&entering, &scope);
    for (size_t s = entering.len; s > 0U; --s) {
        CinderControlScope *scope = &context->control.scopes.data[entering.data[s - 1U]];
        for (size_t d = 0U; d < scope->objects.len; ++d) {
            CinderDecl *decl = scope->objects.data[d];
            CinderIRInst *begin = add_inst_ptr(context->function, context->current, IR_LOCAL_BEGIN, loc); begin->slot = decl->lowering_slot; begin->type = decl->type;
        }
    }
    free(entering.data);
}

static CinderValueId initializer_address(LowerContext *context, CinderValueId base, size_t offset, CinderType *type, CinderLoc loc) {
    CinderType *bytes = cinder_type_pointer(context->function->types, context->function->types->char_type);
    CinderValueId address = base;
    if (offset != 0U) {
        CinderIRInst *constant = add_inst_ptr(context->function, context->current, IR_CONST, loc);
        constant->dst = new_value(context->function); constant->type = context->function->types->long_type; constant->integer = (int64_t)offset;
        CinderValueId index = constant->dst;
        address = pointer_offset(context, base, index, bytes, false, loc);
    }
    return convert_value(context, address, bytes, cinder_type_pointer(context->function->types, type), loc);
}

static void zero_initializer_span(LowerContext *context, CinderValueId base, size_t begin, size_t end, CinderLoc loc) {
    if (begin >= end) return;
    CinderValueId address = initializer_address(context, base, begin, context->function->types->char_type, loc);
    CinderIRInst *zero = add_inst_ptr(context->function, context->current, IR_ZERO_INIT, loc);
    zero->left = address; zero->integer = (int64_t)(end - begin); zero->type = context->function->types->char_type;
}

static void lower_initializer_plan(LowerContext *context, CinderDecl *decl, int slot) {
    CinderIRInst *root = add_inst_ptr(context->function, context->current, IR_LOCAL_ADDRESS, decl->loc);
    root->dst = new_value(context->function); root->slot = slot; root->type = cinder_type_pointer(context->function->types, decl->type);
    CinderValueId object = root->dst;
    CinderType *byte_pointer = cinder_type_pointer(context->function->types, context->function->types->char_type);
    CinderValueId bytes = convert_value(context, object, cinder_type_pointer(context->function->types, decl->type), byte_pointer, decl->loc);
    for (size_t a = 0U; a < decl->init_actions.len; ++a) {
        CinderInitAction *action = &decl->init_actions.data[a]; CinderExpr *value = action->value;
        if (action->bit_width != 0U && (action->zero || value != NULL)) {
            CinderValueId destination = initializer_address(context, bytes, action->offset, action->type, decl->loc);
            CinderValueId source = action->zero ? emit_typed_constant(context, 0, action->type, decl->loc) : lower_expr(context, value);
            CinderIRInst *store = add_inst_ptr(context->function, context->current, IR_BIT_INIT, decl->loc);
            store->left = destination; store->right = source; store->type = action->type;
            store->integer = (int64_t)action->bit_width; store->operator_code = (int)action->bit_offset; continue;
        }
        if (action->zero) { zero_initializer_span(context, bytes, action->offset, action->offset + action->type->size, decl->loc); continue; }
        if (value == NULL) continue;
        CinderValueId destination = initializer_address(context, bytes, action->offset, action->type, value->loc);
        if (action->type->kind == TYPE_ARRAY && value->kind == EX_STRING) {
            char *name = literal_storage(context, action->type, value); if (name == NULL) continue;
            CinderIRInst *address = add_inst_ptr(context->function, context->current, IR_GLOBAL_ADDRESS, value->loc);
            address->dst = new_value(context->function); address->type = cinder_type_pointer(context->function->types, action->type); address->callee = cinder_strndup(name, strlen(name));
            CinderValueId source = address->dst;
            CinderIRInst *copy = add_inst_ptr(context->function, context->current, IR_OBJECT_INIT, value->loc);
            copy->type = action->type; copy->left = destination; copy->right = source;
        } else if (action->type->kind == TYPE_STRUCT || action->type->kind == TYPE_UNION) {
            CinderValueId source = lower_aggregate(context, value);
            CinderIRInst *copy = add_inst_ptr(context->function, context->current, IR_OBJECT_INIT, value->loc);
            copy->type = action->type; copy->left = destination; copy->right = source;
        } else {
            CinderValueId source = lower_expr(context, value);
            CinderIRInst *store = add_inst_ptr(context->function, context->current, IR_MEMORY_INIT, value->loc);
            store->type = action->type; store->left = destination; store->right = source;
        }
    }
}

static void lower_stmt(LowerContext *context, CinderStmt *stmt) {
    if (stmt == NULL) return;
    if (stmt->kind != ST_LABEL && stmt->kind != ST_CASE && stmt->kind != ST_DEFAULT && block_at(context->function, context->current)->terminator.kind != TERM_UNREACHABLE) context->current = create_block(context, "dead.statement");
    size_t literal_mark = context->active_slots.len;
    for (size_t i = 0U; i < stmt->literal_objects.len; ++i)
        if (stmt->literal_objects.data[i]->literal_evaluated) begin_declarations(context, stmt->literal_objects.data[i]);
    switch (stmt->kind) {
        case ST_EMPTY: break;
        case ST_EXPR: (void)lower_full_expression(context, stmt->as.expr, false, stmt->loc); break;
        case ST_DECL: {
            for (CinderDecl *decl = stmt->as.decl; decl != NULL; decl = decl->next) {
                if (decl->kind != DECL_VAR || decl->name == NULL || decl->is_static || decl->is_extern) continue;
                if (decl->lowering_slot < 0) begin_declarations(context, decl);
                int slot = decl->lowering_slot;
                CinderIRInst *reset = add_inst_ptr(context->function, context->current, IR_LOCAL_RESET, decl->loc); reset->slot = slot; reset->type = decl->type;
                size_t expression_mark = context->expression_temporaries.len;
                LocalSlot local = {decl->name, slot, decl->type}; cinder_vec_push((CinderVec *)&context->locals, &local);
                if (decl->init_actions.len != 0U) lower_initializer_plan(context, decl, slot);
                else if (decl->initializer != NULL && decl->initializer->kind == EX_STRING && decl->type->kind == TYPE_ARRAY) {
                    char *name = literal_storage(context, decl->type, decl->initializer); if (name == NULL) continue;
                    CinderIRInst *destination = add_inst_ptr(context->function, context->current, IR_LOCAL_ADDRESS, decl->loc);
                    destination->dst = new_value(context->function); destination->slot = slot; destination->type = cinder_type_pointer(context->function->types, decl->type);
                    CinderValueId destination_value = destination->dst;
                    CinderIRInst *source = add_inst_ptr(context->function, context->current, IR_GLOBAL_ADDRESS, decl->loc);
                    source->dst = new_value(context->function); source->type = cinder_type_pointer(context->function->types, decl->type); source->callee = cinder_strndup(name, strlen(name));
                    CinderValueId source_value = source->dst;
                    CinderIRInst *copy = add_inst_ptr(context->function, context->current, IR_OBJECT_INIT, decl->loc);
                    copy->type = decl->type; copy->left = destination_value; copy->right = source_value;
                } else if (decl->initializer != NULL) { CinderValueId value = lower_expr(context, decl->initializer); CinderIRInst *store = add_inst_ptr(context->function, context->current, IR_LOCAL_INIT, decl->loc); store->left = value; store->slot = slot; store->type = decl->type; }
                end_expression(context, expression_mark, decl->loc);
            }
            break;
        }
        case ST_RETURN: {
            CinderValueId value = CINDER_INVALID_VALUE;
            if (aggregate_value(context->function->type->return_type) && stmt->as.ret.value != NULL) {
                size_t mark = context->expression_temporaries.len;
                CinderValueId source = lower_aggregate(context, stmt->as.ret.value);
                CinderIRInst *copy = add_inst_ptr(context->function, context->current, IR_AGG_RETURN, stmt->loc);
                copy->left = source; copy->type = context->function->type->return_type;
                end_expression(context, mark, stmt->loc);
            } else if (stmt->as.ret.value != NULL) value = lower_full_expression(context, stmt->as.ret.value, false, stmt->loc);
            end_scope(context, 0U, stmt->loc);
            CinderIRBlock *block = block_at(context->function, context->current);
            block->terminator.kind = TERM_RETURN; block->terminator.value = value; block->terminator.loc = stmt->loc; break;
        }
        case ST_BLOCK: {
            size_t scope_mark = context->locals.len, lifetime_mark = context->active_slots.len;
            for (size_t i = 0U; i < stmt->as.block.items.len; ++i) if (stmt->as.block.items.data[i]->kind == ST_DECL) begin_declarations(context, stmt->as.block.items.data[i]->as.decl);
            for (size_t i = 0U; i < stmt->as.block.items.len; ++i) {
                lower_stmt(context, stmt->as.block.items.data[i]);
            }
            if (block_at(context->function, context->current)->terminator.kind == TERM_UNREACHABLE) end_scope(context, lifetime_mark, stmt->loc);
            context->active_slots.len = lifetime_mark; context->locals.len = scope_mark;
            break;
        }
        case ST_IF: {
            CinderValueId condition = lower_full_expression(context, stmt->as.if_stmt.condition, true, stmt->loc);
            CinderBlockId then_id = (CinderBlockId)context->function->blocks.len; CinderIRBlock then_block = make_block(context->function, "then"); cinder_vec_push((CinderVec *)&context->function->blocks, &then_block);
            CinderBlockId else_id = (CinderBlockId)context->function->blocks.len; CinderIRBlock else_block = make_block(context->function, "else"); cinder_vec_push((CinderVec *)&context->function->blocks, &else_block);
            CinderBlockId merge_id = (CinderBlockId)context->function->blocks.len; CinderIRBlock merge_block = make_block(context->function, "merge"); cinder_vec_push((CinderVec *)&context->function->blocks, &merge_block);
            CinderIRBlock *current = block_at(context->function, context->current); current->terminator.kind = TERM_BRANCH; current->terminator.condition = condition; current->terminator.yes = then_id; current->terminator.no = else_id; current->terminator.loc = stmt->loc; set_successor(context->function, context->current, then_id); set_successor(context->function, context->current, else_id);
            context->current = then_id; lower_stmt(context, stmt->as.if_stmt.then_branch); ensure_block_jump(context, merge_id, stmt->loc);
            context->current = else_id; lower_stmt(context, stmt->as.if_stmt.else_branch); ensure_block_jump(context, merge_id, stmt->loc);
            context->current = merge_id; break;
        }
        case ST_WHILE:
        case ST_DO:
        case ST_FOR: {
            bool for_loop = stmt->kind == ST_FOR;
            bool do_loop = stmt->kind == ST_DO;
            size_t scope_mark = context->locals.len, lifetime_mark = context->active_slots.len;
            if (for_loop && stmt->as.for_stmt.init != NULL && stmt->as.for_stmt.init->kind == ST_DECL) begin_declarations(context, stmt->as.for_stmt.init->as.decl);
            if (for_loop) lower_stmt(context, stmt->as.for_stmt.init);
            CinderExpr *condition = for_loop ? stmt->as.for_stmt.condition : stmt->as.loop.condition;
            CinderStmt *body = for_loop ? stmt->as.for_stmt.body : stmt->as.loop.body;
            CinderBlockId cond_id = create_block(context, "loop.cond");
            CinderBlockId body_id = create_block(context, "loop.body");
            CinderBlockId step_id = for_loop ? create_block(context, "loop.step") : cond_id;
            CinderBlockId after_id = create_block(context, "loop.after");
            ensure_block_jump(context, do_loop ? body_id : cond_id, stmt->loc);
            context->current = cond_id;
            CinderValueId test = condition == NULL ? emit_constant(context, 1, stmt->loc) : lower_full_expression(context, condition, true, stmt->loc);
            branch_to(context, test, body_id, after_id, stmt->loc);
            cinder_vec_push((CinderVec *)&context->break_blocks, &after_id);
            cinder_vec_push((CinderVec *)&context->continue_blocks, &step_id);
            size_t loop_scope = context->active_slots.len; cinder_vec_push((CinderVec *)&context->loop_scopes, &loop_scope); cinder_vec_push((CinderVec *)&context->break_scopes, &loop_scope);
            context->current = body_id;
            lower_stmt(context, body);
            ensure_block_jump(context, step_id, stmt->loc);
            if (for_loop) {
                context->current = step_id;
                (void)lower_full_expression(context, stmt->as.for_stmt.step, false, stmt->loc);
                ensure_block_jump(context, cond_id, stmt->loc);
            }
            --context->break_blocks.len; --context->break_scopes.len;
            --context->continue_blocks.len; --context->loop_scopes.len;
            context->locals.len = scope_mark;
            context->current = after_id;
            end_scope(context, lifetime_mark, stmt->loc); context->active_slots.len = lifetime_mark;
            break;
        }
        case ST_SWITCH: {
            CinderValueId control = lower_full_expression(context, stmt->as.selection.control, false, stmt->loc);
            CinderType *type = stmt->as.selection.control->type;
            CinderBlockId after_id = create_block(context, "switch.after");
            for (size_t i = 0U; i < stmt->as.selection.cases.len; ++i) {
                CinderStmt *label = stmt->as.selection.cases.data[i];
                CinderIRInst *constant = add_inst_ptr(context->function, context->current, IR_CONST, label->loc);
                constant->type = type; constant->integer = label->as.case_label.value; constant->dst = new_value(context->function); CinderValueId value = constant->dst;
                CinderIRInst *equal = add_inst_ptr(context->function, context->current, IR_CMP_EQ, label->loc);
                equal->left = control; equal->right = value; equal->source_type = type; equal->dst = new_value(context->function); CinderValueId test = equal->dst;
                CinderBlockId entering = create_block(context, "switch.enter"), next = create_block(context, "switch.next");
                branch_to(context, test, entering, next, label->loc);
                context->current = entering; transition_scopes(context, stmt->control_scope, label->control_scope, label->loc); ensure_block_jump(context, label->as.case_label.block, label->loc);
                context->current = next;
            }
            CinderStmt *fallback = stmt->as.selection.default_label;
            if (fallback != NULL) transition_scopes(context, stmt->control_scope, fallback->control_scope, fallback->loc);
            ensure_block_jump(context, fallback == NULL ? after_id : fallback->as.case_label.block, stmt->loc);
            size_t break_scope = context->active_slots.len;
            cinder_vec_push((CinderVec *)&context->break_blocks, &after_id); cinder_vec_push((CinderVec *)&context->break_scopes, &break_scope);
            context->current = create_block(context, "switch.body.unreached"); lower_stmt(context, stmt->as.selection.body); ensure_block_jump(context, after_id, stmt->loc);
            --context->break_blocks.len; --context->break_scopes.len;
            context->current = after_id; break;
        }
        case ST_CASE: case ST_DEFAULT:
            ensure_block_jump(context, stmt->as.case_label.block, stmt->loc);
            context->current = stmt->as.case_label.block; lower_stmt(context, stmt->as.case_label.body); break;
        case ST_BREAK: if (context->break_blocks.len > 0U) { end_scope(context, context->break_scopes.data[context->break_scopes.len - 1U], stmt->loc); CinderIRBlock *block = block_at(context->function, context->current); block->terminator.kind = TERM_JUMP; block->terminator.target = context->break_blocks.data[context->break_blocks.len - 1U]; set_successor(context->function, context->current, block->terminator.target); } break;
        case ST_CONTINUE: if (context->continue_blocks.len > 0U) { end_scope(context, context->loop_scopes.data[context->loop_scopes.len - 1U], stmt->loc); CinderIRBlock *block = block_at(context->function, context->current); block->terminator.kind = TERM_JUMP; block->terminator.target = context->continue_blocks.data[context->continue_blocks.len - 1U]; set_successor(context->function, context->current, block->terminator.target); } break;
        case ST_LABEL:
            ensure_block_jump(context, stmt->as.label.block, stmt->loc);
            context->current = stmt->as.label.block; lower_stmt(context, stmt->as.label.body); break;
        case ST_GOTO:
            if (stmt->as.jump.target != NULL) {
                transition_scopes(context, stmt->control_scope, stmt->as.jump.target->control_scope, stmt->loc);
                ensure_block_jump(context, stmt->as.jump.target->as.label.block, stmt->loc);
            }
            break;
    }
    if (stmt->literal_objects.len != 0U) {
        if (block_at(context->function, context->current)->terminator.kind == TERM_UNREACHABLE) end_scope(context, literal_mark, stmt->loc);
        context->active_slots.len = literal_mark;
    }
}

void cinder_ir_init(CinderIRModule *module, CinderTypeContext *types) {
    module->functions.data = NULL; module->functions.len = 0U; module->functions.cap = 0U; module->globals.data = NULL; module->globals.len = 0U; module->globals.cap = 0U; module->types = types; cinder_arena_init(&module->arena, 32768U);
}

static void destroy_inst(CinderIRInst *inst) { free(inst->callee); free(inst->args.data); free(inst->arg_floats.data); free(inst->phi_blocks.data); }
static void destroy_function(CinderIRFunction *function) {
    free(function->name); free(function->params.data); free(function->local_types.data); free(function->local_alignments.data);
    for (size_t b = 0U; b < function->blocks.len; ++b) { CinderIRBlock *block = &function->blocks.data[b]; free(block->name); for (size_t i = 0U; i < block->instructions.len; ++i) destroy_inst(&block->instructions.data[i]); free(block->instructions.data); free(block->predecessors.data); free(block->successors.data); }
    free(function->blocks.data);
}

void cinder_ir_destroy(CinderIRModule *module) {
    for (size_t i = 0U; i < module->functions.len; ++i) destroy_function(&module->functions.data[i]);
    for (size_t i = 0U; i < module->globals.len; ++i) {
        CinderIRGlobal *global = &module->globals.data[i]; free(global->name);
        for (size_t a = 0U; a < global->addresses.len; ++a) { free(global->addresses.data[a].symbol); free(global->addresses.data[a].origin_path.data); }
        free(global->addresses.data);
    }
    free(module->globals.data); free(module->functions.data); cinder_arena_destroy(&module->arena);
}

static void lower_global_plan(CinderIRModule *module, CinderAst *ast, CinderDecl *decl, CinderIRGlobal *global, CinderDiagnostics *diags) {
    if (global->type->size == 0U || global->type->size > 64U * 1024U * 1024U) { cinder_diag(diags, CINDER_ERROR, decl->loc, "global initializer requires bounded complete storage"); return; }
    global->byte_count = global->type->size;
    global->bytes = cinder_arena_alloc(&module->arena, global->byte_count, _Alignof(char)); memset(global->bytes, 0, global->byte_count);
    global->has_initializer = true;
    for (size_t a = 0U; a < decl->init_actions.len; ++a) {
        const CinderInitAction *action = &decl->init_actions.data[a]; CinderExpr *value = action->value;
        if (value == NULL && !action->zero) continue;
        if (action->offset > global->byte_count || action->type->size > global->byte_count - action->offset) { cinder_diag(diags, CINDER_ERROR, decl->loc, "global initializer subobject exceeds its storage"); continue; }
        for (size_t r = 0U; r < global->addresses.len;) {
            size_t old = global->addresses.data[r].offset;
            if (old < action->offset + action->type->size && action->offset < old + 8U) {
                free(global->addresses.data[r].symbol); free(global->addresses.data[r].origin_path.data); global->addresses.data[r] = global->addresses.data[--global->addresses.len];
            } else ++r;
        }
        if (action->zero && action->bit_width == 0U) { memset(global->bytes + action->offset, 0, action->type->size); continue; }
        if (action->bit_width != 0U) {
            int64_t integer = 0; double floating = 0.0;
            if (!action->zero && !cinder_constant_scalar(ast, value, action->type, &integer, &floating)) { cinder_diag(diags, CINDER_ERROR, value->loc, "global bitfield initializer requires an arithmetic constant"); continue; }
            for (unsigned bit = 0U; bit < action->bit_width; ++bit) {
                size_t position = (size_t)action->bit_offset + bit, byte = action->offset + position / 8U;
                unsigned char mask = (unsigned char)(1U << (position % 8U));
                unsigned char old = (unsigned char)global->bytes[byte];
                global->bytes[byte] = (char)((old & (unsigned char)~mask) | (((uint64_t)integer >> bit & 1U) != 0U ? mask : 0U));
            }
            continue;
        }
        if (action->type->kind == TYPE_ARRAY && value->kind == EX_STRING) {
            size_t count = value->literal_length + 1U; if (count > action->type->size) count = action->type->size;
            memcpy(global->bytes + action->offset, value->as.string, count); continue;
        }
        if (action->type->kind == TYPE_POINTER) {
            CinderIRAddress address;
            if (!cinder_static_address(module, ast, value, &address, diags)) cinder_diag(diags, CINDER_ERROR, value->loc, "global pointer subobject requires an address constant");
            else if (address.symbol != NULL) { address.offset = action->offset; address.pointer_type = action->type; cinder_vec_push((CinderVec *)&global->addresses, &address); }
            continue;
        }
        CinderExpr *boolean = value;
        if (boolean->kind == EX_CAST && action->type->kind == TYPE_BOOL) boolean = boolean->as.cast.value;
        if (action->type->kind == TYPE_BOOL && boolean->type->kind == TYPE_POINTER) {
            CinderIRAddress address;
            if (!cinder_static_address(module, ast, boolean, &address, diags) || address.addend < 0 || (uint64_t)address.addend < address.domain_begin || (uint64_t)address.addend > address.domain_end) cinder_diag(diags, CINDER_ERROR, value->loc, "boolean subobject requires a supported constant address");
            else global->bytes[action->offset] = address.symbol != NULL;
            free(address.symbol); free(address.origin_path.data); continue;
        }
        int64_t integer = 0; double floating = 0.0;
        if (!cinder_constant_scalar(ast, value, action->type, &integer, &floating)) { cinder_diag(diags, CINDER_ERROR, value->loc, "global subobject initializer requires an arithmetic constant"); continue; }
        uint64_t bits = (uint64_t)integer;
        if (action->type->kind == TYPE_FLOAT) { float single = (float)floating; uint32_t narrow; memcpy(&narrow, &single, sizeof(narrow)); bits = narrow; }
        else if (action->type->kind == TYPE_DOUBLE) memcpy(&bits, &floating, sizeof(bits));
        for (size_t byte = 0U; byte < action->type->size && byte < 8U; ++byte) global->bytes[action->offset + byte] = (char)(bits >> (byte * 8U));
    }
}

static void lower_global_decl(CinderIRModule *module, CinderAst *ast, CinderDecl *decl, CinderDiagnostics *diags) {
    CinderDecl *canonical = decl;
    if (canonical->emission != NULL) decl = canonical->emission;
    CinderIRGlobal global;
    memset(&global, 0, sizeof(global));
    const char *symbol = decl->storage_symbol == NULL ? decl->name : decl->storage_symbol;
    global.name = cinder_strndup(symbol, strlen(symbol));
    global.type = canonical->type;
    global.alignment = canonical->alignment;
    global.read_only = (global.type->qualifiers & 1U) != 0U;
    for (CinderType *element = global.type; element->kind == TYPE_ARRAY; element = element->base) if ((element->base->qualifiers & 1U) != 0U) global.read_only = true;
    global.global = !canonical->is_static;
    global.is_extern = !canonical->has_definition && !canonical->tentative;
    global.loc = decl->loc;
    bool numeric = (global.type->kind >= TYPE_BOOL && global.type->kind <= TYPE_DOUBLE) || global.type->kind == TYPE_ENUM;
    CinderExpr *boolean = decl->initializer;
    if (boolean != NULL && boolean->kind == EX_CAST && boolean->type->kind == TYPE_BOOL && global.type->kind == TYPE_BOOL) boolean = boolean->as.cast.value;
    if (decl->initializer != NULL && decl->initializer->kind == EX_INIT_LIST) lower_global_plan(module, ast, decl, &global, diags);
    else if (decl->initializer != NULL && numeric && cinder_constant_scalar(ast, decl->initializer, global.type, &global.integer, &global.floating)) global.has_initializer = true;
    else if (decl->initializer != NULL && global.type->kind == TYPE_POINTER) {
        CinderIRAddress address;
        if (!cinder_static_address(module, ast, decl->initializer, &address, diags)) cinder_diag(diags, CINDER_ERROR, decl->loc, "pointer initializer for '%s' is not a supported address constant", decl->name);
        else {
            address.pointer_type = global.type;
            if (address.symbol != NULL) cinder_vec_push((CinderVec *)&global.addresses, &address);
            global.has_initializer = true;
        }
    }
    else if (boolean != NULL && global.type->kind == TYPE_BOOL && boolean->type->kind == TYPE_POINTER) {
        CinderIRAddress address;
        if (!cinder_static_address(module, ast, boolean, &address, diags) || address.addend < 0 || (uint64_t)address.addend < address.domain_begin || (uint64_t)address.addend > address.domain_end) cinder_diag(diags, CINDER_ERROR, decl->loc, "boolean initializer for '%s' is not a supported constant", decl->name);
        else { global.integer = address.symbol != NULL; global.has_initializer = true; }
        free(address.symbol); free(address.origin_path.data);
    }
    else if (decl->initializer != NULL && decl->initializer->kind == EX_STRING) {
        global.byte_count = decl->initializer->literal_length + 1U;
        if (global.byte_count > global.type->size) global.byte_count = global.type->size;
        global.bytes = cinder_arena_strndup(&module->arena, decl->initializer->as.string, global.byte_count);
        global.has_initializer = true;
        global.read_only = global.type->kind == TYPE_ARRAY && (global.type->base->qualifiers & 1U) != 0U;
        if (global.type->kind == TYPE_ARRAY && global.type->array_len == 0U) { global.type->array_len = global.byte_count + 1U; global.type->size = global.type->array_len * global.type->base->size; global.type->complete = true; }
    } else if (decl->initializer != NULL) cinder_diag(diags, CINDER_ERROR, decl->loc, "global initializer for '%s' is not a supported constant", decl->name);
    cinder_vec_push((CinderVec *)&module->globals, &global);
}

bool cinder_lower_static_object(CinderIRModule *module, CinderAst *ast, CinderDecl *decl, CinderDiagnostics *diags) {
    const char *symbol = decl->storage_symbol == NULL ? decl->name : decl->storage_symbol;
    for (size_t g = 0U; g < module->globals.len; ++g) if (strcmp(module->globals.data[g].name, symbol) == 0) return true;
    unsigned before = diags->errors;
    lower_global_decl(module, ast, decl, diags);
    return diags->errors == before;
}

static const CinderIRInst *condition_definition(const CinderIRFunction *function, CinderValueId value) {
    if (value == CINDER_INVALID_VALUE) return NULL;
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
            if (inst->dst == value) return inst;
        }
    return NULL;
}

/* Diagnostic reachability only. Do not assume a returning function honors
 * its contract when deciding whether its own definition needs a warning. */
static bool constant_condition(const CinderIRFunction *function, CinderValueId value, bool *truth, unsigned depth) {
    if (depth >= 32U) return false;
    const CinderIRInst *inst = condition_definition(function, value);
    if (inst == NULL) return false;
    if (inst->op == IR_CONST) { *truth = inst->integer != 0; return true; }
    if (inst->op == IR_FCONST) { *truth = inst->floating != 0.0; return true; }
    if (inst->op == IR_COPY || (inst->op == IR_CONVERT && inst->type->kind == TYPE_BOOL)) return constant_condition(function, inst->left, truth, depth + 1U);
    bool equality = inst->op == IR_CMP_EQ || inst->op == IR_FCMP_EQ;
    if (equality || inst->op == IR_CMP_NE || inst->op == IR_FCMP_NE) {
        const CinderIRInst *zero = condition_definition(function, inst->right);
        if (zero == NULL || !((zero->op == IR_CONST && zero->integer == 0) || (zero->op == IR_FCONST && zero->floating == 0.0))) return false;
        if (!constant_condition(function, inst->left, truth, depth + 1U)) return false;
        if (equality) *truth = !*truth;
        return true;
    }
    return false;
}

static bool function_can_return(const CinderIRFunction *function) {
    size_t count = function->blocks.len;
    if (count == 0U) return false;
    bool *seen = cinder_alloc(count * sizeof(*seen)); memset(seen, 0, count * sizeof(*seen));
    CinderBlockId *work = cinder_alloc(count * sizeof(*work)); size_t pending = 1U;
    work[0] = 0U; seen[0] = true; bool can_return = false;
    while (pending > 0U && !can_return) {
        const CinderIRBlock *block = &function->blocks.data[work[--pending]];
        bool ends = false;
        for (size_t i = 0U; i < block->instructions.len; ++i) if (block->instructions.data[i].noreturn_call) { ends = true; break; }
        if (ends) continue;
        const CinderTerminator *term = &block->terminator;
        if (term->kind == TERM_RETURN) { can_return = true; break; }
        CinderBlockId targets[2]; size_t target_count = 0U;
        if (term->kind == TERM_JUMP) targets[target_count++] = term->target;
        else if (term->kind == TERM_BRANCH) {
            bool truth;
            if (constant_condition(function, term->condition, &truth, 0U)) targets[target_count++] = truth ? term->yes : term->no;
            else { targets[target_count++] = term->yes; targets[target_count++] = term->no; }
        }
        for (size_t t = 0U; t < target_count; ++t) {
            CinderBlockId id = targets[t];
            if (id < count && !seen[id]) { seen[id] = true; work[pending++] = id; }
        }
    }
    free(seen); free(work); return can_return;
}

int cinder_lower_ir(CinderIRModule *module, CinderAst *ast, CinderDiagnostics *diags) {
    for (size_t i = 0U; i < ast->static_literals.len; ++i) (void)cinder_lower_static_object(module, ast, ast->static_literals.data[i], diags);
    for (size_t i = 0U; i < ast->stored_objects.len; ++i) (void)cinder_lower_static_object(module, ast, ast->stored_objects.data[i], diags);
    for (size_t i = 0U; i < ast->declarations.len; ++i) {
        CinderDecl *decl = ast->declarations.data[i];
        if (decl->kind == DECL_VAR) { if (decl->canonical == decl) lower_global_decl(module, ast, decl, diags); continue; }
        if (decl->kind != DECL_FUNCTION || !decl->is_definition) continue;
        CinderIRFunction function; memset(&function, 0, sizeof(function)); function.name = cinder_strndup(decl->name, strlen(decl->name)); function.type = decl->type; function.ast = ast; function.params.data = NULL; function.params.len = 0U; function.params.cap = 0U; function.blocks.data = NULL; function.blocks.len = 0U; function.blocks.cap = 0U; function.value_count = 0U; function.local_count = 0U; function.float_param_count = 0U; function.types = module->types; function.global = !decl->is_static; function.is_noreturn = decl->canonical != NULL ? decl->canonical->is_noreturn : decl->is_noreturn;
        for (size_t p = 0U; p < decl->params.len; ++p) { CinderDecl *param = decl->params.data[p]; cinder_vec_push((CinderVec *)&function.params, &param); }
        CinderIRBlock entry = make_block(&function, "entry"); cinder_vec_push((CinderVec *)&function.blocks, &entry);
        LowerContext context; memset(&context, 0, sizeof(context)); context.function = &function; context.module = module; context.diags = diags; context.current = 0U; context.locals.data = NULL; context.locals.len = 0U; context.locals.cap = 0U; context.break_blocks.data = NULL; context.break_blocks.len = 0U; context.break_blocks.cap = 0U; context.continue_blocks.data = NULL; context.continue_blocks.len = 0U; context.continue_blocks.cap = 0U;
        size_t integer_param_count = 0U;
        for (size_t p = 0U; p < decl->params.len; ++p) { CinderDecl *param = decl->params.data[p]; LocalSlot local = {param->name, new_local(&context, param->type), param->type}; param->lowering_slot = local.slot; cinder_vec_push((CinderVec *)&context.locals, &local); if (aggregate_value(param->type)) { CinderIRInst *copy = add_inst_ptr(&function, 0U, IR_AGG_ARG, param->loc); copy->slot = local.slot; copy->operator_code = (int)p; copy->type = param->type; ++integer_param_count; continue; } bool is_float = param->type != NULL && (param->type->kind == TYPE_FLOAT || param->type->kind == TYPE_DOUBLE); CinderIRInst *arg = add_inst_ptr(&function, 0U, is_float ? IR_FARG : IR_ARG, param->loc); arg->dst = new_value(&function); arg->slot = (int)p; arg->operator_code = is_float ? (int)function.float_param_count++ : (int)integer_param_count++; arg->type = param->type; CinderValueId argument_value = arg->dst; CinderIRInst *store = add_inst_ptr(&function, 0U, IR_LOCAL_INIT, param->loc); store->left = argument_value; store->type = param->type; store->slot = local.slot; }
        prepare_control(&context, decl->body);
        lower_stmt(&context, decl->body);
        if (block_at(&function, context.current)->terminator.kind == TERM_UNREACHABLE) {
            CinderValueId returned = CINDER_INVALID_VALUE;
            if (!function.is_noreturn && aggregate_value(function.type->return_type)) {
                int slot = new_local(&context, function.type->return_type);
                CinderIRInst *address = add_inst_ptr(&function, context.current, IR_LOCAL_ADDRESS, decl->loc);
                address->slot = slot; address->type = cinder_type_pointer(function.types, function.type->return_type); address->dst = new_value(&function);
                CinderValueId source = address->dst;
                CinderIRInst *copy = add_inst_ptr(&function, context.current, IR_AGG_RETURN, decl->loc); copy->left = source; copy->type = function.type->return_type;
            } else if (!function.is_noreturn && function.type->return_type->kind != TYPE_VOID) {
                CinderIRInst *value = add_inst_ptr(&function, context.current, strcmp(function.name, "main") == 0 ? IR_CONST : IR_UNDEF, decl->loc);
                value->type = function.type->return_type; value->dst = new_value(&function); returned = value->dst;
            }
            CinderIRBlock *block = block_at(&function, context.current); block->terminator.kind = TERM_RETURN; block->terminator.value = returned;
        }
        if (function.is_noreturn && function_can_return(&function)) cinder_diag(diags, CINDER_WARNING, decl->loc, "function '%s' declared _Noreturn appears capable of returning", decl->name);
        free(context.locals.data); free(context.break_blocks.data); free(context.continue_blocks.data); free(context.active_slots.data); free(context.loop_scopes.data); free(context.break_scopes.data); free(context.expression_temporaries.data); cinder_control_destroy(&context.control);
        cinder_vec_push((CinderVec *)&module->functions, &function);
    }
    return diags->errors == 0U ? 0 : 1;
}

void cinder_dump_ir(const CinderIRModule *module, FILE *out) {
    for (size_t g = 0U; g < module->globals.len; ++g) fprintf(out, "global %s : %s%s\n", module->globals.data[g].name, cinder_type_name(module->globals.data[g].type), module->globals.data[g].bytes != NULL ? " string" : "");
    for (size_t f = 0U; f < module->functions.len; ++f) {
        const CinderIRFunction *function = &module->functions.data[f]; fprintf(out, "function %s() -> %s {\n", function->name, cinder_type_name(function->type->return_type)); if (function->is_noreturn) fputs("    contract _Noreturn\n", out);
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            const CinderIRBlock *block = &function->blocks.data[b]; fprintf(out, "  block %u %s:\n", block->id, block->name);
            for (size_t i = 0U; i < block->instructions.len; ++i) { const CinderIRInst *inst = &block->instructions.data[i]; fprintf(out, "    "); if (inst->dst != CINDER_INVALID_VALUE) fprintf(out, "%%v%u = ", inst->dst); fprintf(out, "%s", cinder_ir_op_name(inst->op)); if (inst->op == IR_CONST) fprintf(out, " %" PRId64, inst->integer); else if (inst->op == IR_GLOBAL_LOAD || inst->op == IR_GLOBAL_STORE) fprintf(out, " %s", inst->callee); else if (inst->op == IR_LOCAL_LOAD || inst->op == IR_LOCAL_STORE || inst->op == IR_ARG || inst->op == IR_FARG) fprintf(out, " slot=%d", inst->slot); else if (inst->op == IR_PHI) { fputs(" <", out); for (size_t a = 0U; a < inst->args.len; ++a) fprintf(out, "block %u:%%v%u%s", inst->phi_blocks.data[a], inst->args.data[a], a + 1U == inst->args.len ? "" : ", "); fputc('>', out); } else if (inst->op == IR_CALL) { if (inst->callee != NULL) fprintf(out, " %s(", inst->callee); else fprintf(out, " %%v%u(", inst->left); for (size_t a = 0U; a < inst->args.len; ++a) fprintf(out, "%%v%u%s", inst->args.data[a], a + 1U == inst->args.len ? "" : ", "); fputc(')', out); } else if (inst->left != CINDER_INVALID_VALUE) { fprintf(out, " %%v%u", inst->left); if (inst->right != CINDER_INVALID_VALUE) fprintf(out, ", %%v%u", inst->right); } fputc('\n', out); }
            switch (block->terminator.kind) { case TERM_RETURN: fprintf(out, "    return"); if (block->terminator.value != CINDER_INVALID_VALUE) fprintf(out, " %%v%u", block->terminator.value); fputc('\n', out); break; case TERM_JUMP: fprintf(out, "    jump block %u\n", block->terminator.target); break; case TERM_BRANCH: fprintf(out, "    branch %%v%u, block %u, block %u\n", block->terminator.condition, block->terminator.yes, block->terminator.no); break; default: fputs("    unreachable\n", out); break; }
        }
        fputs("}\n", out);
    }
}
