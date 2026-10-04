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
    CinderDiagnostics *diags;
    CinderBlockId current;
    CINDER_VEC_TYPE(LocalSlot) locals;
    CINDER_VEC_TYPE(CinderBlockId) break_blocks;
    CINDER_VEC_TYPE(CinderBlockId) continue_blocks;
    size_t va_index;
} LowerContext;

static int find_local(const LowerContext *context, const char *name) {
    for (size_t i = context->locals.len; i > 0U; --i) if (strcmp(context->locals.data[i - 1U].name, name) == 0) return context->locals.data[i - 1U].slot;
    return -1;
}

static CinderDecl *find_global_decl(const CinderIRFunction *function, const char *name) {
    for (size_t i = 0U; i < function->ast->declarations.len; ++i) { CinderDecl *decl = function->ast->declarations.data[i]; if (decl->kind == DECL_VAR && strcmp(decl->name, name) == 0) return decl; }
    return NULL;
}

static CinderValueId lower_expr(LowerContext *context, CinderExpr *expr);
static CinderValueId lower_expr_impl(LowerContext *context, CinderExpr *expr);

bool cinder_ir_floating(const CinderType *type) {
    return type != NULL && (type->kind == TYPE_FLOAT || type->kind == TYPE_DOUBLE);
}

CinderType *cinder_ir_value_type(const CinderIRFunction *function, CinderValueId value) {
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i)
            if (function->blocks.data[b].instructions.data[i].dst == value) return function->blocks.data[b].instructions.data[i].type;
    return NULL;
}

static CinderValueId lower_expr(LowerContext *context, CinderExpr *expr) {
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
    return slot;
}

static CinderValueId lower_call(LowerContext *context, CinderExpr *expr) {
    if (expr->as.call.callee->kind == EX_NAME && (strcmp(expr->as.call.callee->as.name, "va_start") == 0 || strcmp(expr->as.call.callee->as.name, "va_end") == 0)) {
        (void)add_inst_ptr(context->function, context->current, IR_NOP, expr->loc);
        return CINDER_INVALID_VALUE;
    }
    CINDER_VEC_TYPE(CinderValueId) lowered = {NULL, 0U, 0U};
    CINDER_VEC_TYPE(bool) lowered_floats = {NULL, 0U, 0U};
    for (size_t i = 0U; i < expr->as.call.args.len; ++i) {
        CinderValueId arg = lower_expr(context, expr->as.call.args.data[i]);
        bool is_float = expr->as.call.args.data[i]->type != NULL && (expr->as.call.args.data[i]->type->kind == TYPE_FLOAT || expr->as.call.args.data[i]->type->kind == TYPE_DOUBLE);
        cinder_vec_push((CinderVec *)&lowered, &arg); cinder_vec_push((CinderVec *)&lowered_floats, &is_float);
    }
    CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CALL, expr->loc);
    inst->dst = expr->type != NULL && expr->type->kind == TYPE_VOID ? CINDER_INVALID_VALUE : new_value(context->function); inst->floating_result = expr->type != NULL && (expr->type->kind == TYPE_FLOAT || expr->type->kind == TYPE_DOUBLE);
    inst->type = expr->type;
    inst->callee_type = expr->as.call.callee->type;
    if (inst->callee_type->kind == TYPE_POINTER) inst->callee_type = inst->callee_type->base;
    if (expr->as.call.callee->kind == EX_NAME) inst->callee = cinder_strndup(expr->as.call.callee->as.name, strlen(expr->as.call.callee->as.name));
    else inst->callee = cinder_strndup("<indirect>", 10U);
    for (size_t i = 0U; i < lowered.len; ++i) { cinder_vec_push((CinderVec *)&inst->args, &lowered.data[i]); cinder_vec_push((CinderVec *)&inst->arg_floats, &lowered_floats.data[i]); }
    free(lowered.data); free(lowered_floats.data);
    return inst->dst;
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
    return emit_operation(context, floating ? IR_FCMP_NE : IR_CMP_NE, value, zero, expr->loc);
}

static void store_slot(LowerContext *context, int slot, CinderValueId value, CinderLoc loc) {
    CinderIRInst *store = add_inst_ptr(context->function, context->current, IR_LOCAL_STORE, loc);
    store->left = value; store->type = context->function->local_types.data[slot];
    store->slot = slot;
}

static void store_name(LowerContext *context, CinderExpr *target, CinderValueId value, CinderLoc loc) {
    if (target->kind != EX_NAME) {
        cinder_diag(context->diags, CINDER_ERROR, loc, "address-based lvalue lowering is not implemented");
        return;
    }
    int slot = find_local(context, target->as.name);
    if (slot >= 0) store_slot(context, slot, value, loc);
    else if (find_global_decl(context->function, target->as.name) != NULL) {
        CinderIRInst *store = add_inst_ptr(context->function, context->current, IR_GLOBAL_STORE, loc);
        store->left = value;
        store->type = target->type;
        store->callee = cinder_strndup(target->as.name, strlen(target->as.name));
    } else cinder_diag(context->diags, CINDER_ERROR, loc, "unknown assignment storage");
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
    store_slot(context, slot, yes_value, loc);
    ensure_block_jump(context, merge, loc);
    context->current = no_block;
    CinderValueId no_value = logical ? (conjunction ? emit_constant(context, 0, loc) : lower_truth(context, no)) : lower_expr(context, no);
    store_slot(context, slot, no_value, loc);
    ensure_block_jump(context, merge, loc);
    context->current = merge;
    CinderIRInst *load = add_inst_ptr(context->function, merge, IR_LOCAL_LOAD, loc);
    load->dst = new_value(context->function);
    load->slot = slot;
    return load->dst;
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
    if (expr->kind == EX_INT || expr->kind == EX_CHAR) {
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CONST, expr->loc);
        inst->dst = new_value(context->function); inst->integer = expr->as.integer; return inst->dst;
    }
    if (expr->kind == EX_FLOAT) {
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_FCONST, expr->loc);
        inst->dst = new_value(context->function); inst->floating = expr->as.floating; return inst->dst;
    }
    if (expr->kind == EX_NAME) {
        int slot = find_local(context, expr->as.name);
        if (slot >= 0) {
            CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_LOCAL_LOAD, expr->loc);
            inst->dst = new_value(context->function); inst->slot = slot; return inst->dst;
        }
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_GLOBAL_LOAD, expr->loc);
        inst->dst = new_value(context->function); inst->callee = cinder_strndup(expr->as.name, strlen(expr->as.name)); return inst->dst;
    }
    if (expr->kind == EX_VA_ARG) {
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_VA_ARG, expr->loc); inst->dst = new_value(context->function); inst->slot = (int)(context->function->params.len + context->va_index++); return inst->dst;
    }
    if (expr->kind == EX_CALL) return lower_call(context, expr);
    if (expr->kind == EX_CONDITIONAL) return lower_choice(context, expr->as.conditional.condition, expr->as.conditional.yes, expr->as.conditional.no, false, false, expr->loc);
    if (expr->kind == EX_ASSIGN) {
        CinderValueId previous = CINDER_INVALID_VALUE;
        if (expr->as.assign.op != '=') previous = lower_expr(context, expr->as.assign.target);
        CinderValueId value = lower_expr(context, expr->as.assign.value);
        if (expr->as.assign.op != '=') {
            CinderType *operation_type = expr->as.assign.operation_type;
            previous = convert_value(context, previous, expr->as.assign.target->type, operation_type, expr->loc);
            bool floating = cinder_ir_floating(operation_type);
            bool unsig = operation_type->is_unsigned;
            value = emit_operation(context, compound_operation(expr->as.assign.op, floating, unsig), previous, value, expr->loc);
            CinderIRInst *operation = &block_at(context->function, context->current)->instructions.data[block_at(context->function, context->current)->instructions.len - 1U];
            operation->type = operation_type; operation->source_type = operation_type;
            value = convert_value(context, value, operation_type, expr->type, expr->loc);
        }
        store_name(context, expr->as.assign.target, value, expr->loc);
        return value;
    }
    if (expr->kind == EX_UNARY) {
        int op = expr->as.unary.op;
        if (op == TOK_PLUSPLUS || op == TOK_MINUSMINUS) {
            CinderValueId old = lower_expr(context, expr->as.unary.value);
            CinderType *target_type = expr->as.unary.value->type;
            CinderType *operation_type = cinder_ir_floating(target_type) ? target_type : cinder_integer_promote(context->function->types, target_type);
            CinderValueId operand = convert_value(context, old, target_type, operation_type, expr->loc);
            CinderValueId one_value = new_value(context->function);
            bool floating = cinder_ir_floating(operation_type);
            CinderIRInst *one = add_inst_ptr(context->function, context->current, floating ? IR_FCONST : IR_CONST, expr->loc); one->dst = one_value; one->integer = 1; one->floating = 1.0; one->type = operation_type;
            CinderIROp add_op = floating ? (op == TOK_PLUSPLUS ? IR_FADD : IR_FSUB) : (op == TOK_PLUSPLUS ? IR_ADD : IR_SUB);
            CinderIRInst *add = add_inst_ptr(context->function, context->current, add_op, expr->loc); add->dst = new_value(context->function); add->left = operand; add->right = one_value; add->loc = expr->loc; add->type = operation_type; add->source_type = operation_type;
            CinderValueId updated = add->dst;
            updated = convert_value(context, updated, operation_type, target_type, expr->loc);
            store_name(context, expr->as.unary.value, updated, expr->loc);
            return expr->as.unary.postfix ? old : updated;
        }
        if (op == '!') {
            CinderValueId truth = lower_truth(context, expr->as.unary.value);
            CinderValueId zero = emit_constant(context, 0, expr->loc);
            return emit_operation(context, IR_CMP_EQ, truth, zero, expr->loc);
        }
        if (op == '&' || op == '*') {
            cinder_diag(context->diags, CINDER_ERROR, expr->loc, "pointer address lowering is not implemented");
            return CINDER_INVALID_VALUE;
        }
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
        bool unsig = expr->as.binary.left->type->is_unsigned;
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

static void lower_stmt(LowerContext *context, CinderStmt *stmt) {
    if (stmt == NULL) return;
    switch (stmt->kind) {
        case ST_EMPTY: break;
        case ST_EXPR: (void)lower_expr(context, stmt->as.expr); break;
        case ST_DECL: {
            int slot = new_local(context, stmt->as.decl->type);
            LocalSlot local = {stmt->as.decl->name, slot, stmt->as.decl->type}; cinder_vec_push((CinderVec *)&context->locals, &local);
            if (stmt->as.decl->initializer != NULL) { CinderValueId value = lower_expr(context, stmt->as.decl->initializer); CinderIRInst *store = add_inst_ptr(context->function, context->current, IR_LOCAL_STORE, stmt->loc); store->left = value; store->slot = slot; store->type = stmt->as.decl->type; }
            break;
        }
        case ST_RETURN: {
            CinderValueId value = stmt->as.ret.value == NULL ? CINDER_INVALID_VALUE : lower_expr(context, stmt->as.ret.value);
            CinderIRBlock *block = block_at(context->function, context->current);
            block->terminator.kind = TERM_RETURN; block->terminator.value = value; block->terminator.loc = stmt->loc; break;
        }
        case ST_BLOCK: {
            size_t scope_mark = context->locals.len;
            for (size_t i = 0U; i < stmt->as.block.items.len; ++i) {
                lower_stmt(context, stmt->as.block.items.data[i]);
                if (block_at(context->function, context->current)->terminator.kind != TERM_UNREACHABLE) break;
            }
            context->locals.len = scope_mark;
            break;
        }
        case ST_IF: {
            CinderValueId condition = lower_truth(context, stmt->as.if_stmt.condition);
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
            size_t scope_mark = context->locals.len;
            if (for_loop) lower_stmt(context, stmt->as.for_stmt.init);
            CinderExpr *condition = for_loop ? stmt->as.for_stmt.condition : stmt->as.loop.condition;
            CinderStmt *body = for_loop ? stmt->as.for_stmt.body : stmt->as.loop.body;
            CinderBlockId cond_id = create_block(context, "loop.cond");
            CinderBlockId body_id = create_block(context, "loop.body");
            CinderBlockId step_id = for_loop ? create_block(context, "loop.step") : cond_id;
            CinderBlockId after_id = create_block(context, "loop.after");
            ensure_block_jump(context, do_loop ? body_id : cond_id, stmt->loc);
            context->current = cond_id;
            CinderValueId test = condition == NULL ? emit_constant(context, 1, stmt->loc) : lower_truth(context, condition);
            branch_to(context, test, body_id, after_id, stmt->loc);
            cinder_vec_push((CinderVec *)&context->break_blocks, &after_id);
            cinder_vec_push((CinderVec *)&context->continue_blocks, &step_id);
            context->current = body_id;
            lower_stmt(context, body);
            ensure_block_jump(context, step_id, stmt->loc);
            if (for_loop) {
                context->current = step_id;
                (void)lower_expr(context, stmt->as.for_stmt.step);
                ensure_block_jump(context, cond_id, stmt->loc);
            }
            --context->break_blocks.len;
            --context->continue_blocks.len;
            context->locals.len = scope_mark;
            context->current = after_id;
            break;
        }
        case ST_BREAK: if (context->break_blocks.len > 0U) { CinderIRBlock *block = block_at(context->function, context->current); block->terminator.kind = TERM_JUMP; block->terminator.target = context->break_blocks.data[context->break_blocks.len - 1U]; set_successor(context->function, context->current, block->terminator.target); } break;
        case ST_CONTINUE: if (context->continue_blocks.len > 0U) { CinderIRBlock *block = block_at(context->function, context->current); block->terminator.kind = TERM_JUMP; block->terminator.target = context->continue_blocks.data[context->continue_blocks.len - 1U]; set_successor(context->function, context->current, block->terminator.target); } break;
    }
}

void cinder_ir_init(CinderIRModule *module, CinderTypeContext *types) {
    module->functions.data = NULL; module->functions.len = 0U; module->functions.cap = 0U; module->globals.data = NULL; module->globals.len = 0U; module->globals.cap = 0U; module->types = types; cinder_arena_init(&module->arena, 32768U);
}

static void destroy_inst(CinderIRInst *inst) { free(inst->callee); free(inst->args.data); free(inst->arg_floats.data); free(inst->phi_blocks.data); }
static void destroy_function(CinderIRFunction *function) {
    free(function->name); free(function->params.data); free(function->local_types.data);
    for (size_t b = 0U; b < function->blocks.len; ++b) { CinderIRBlock *block = &function->blocks.data[b]; free(block->name); for (size_t i = 0U; i < block->instructions.len; ++i) destroy_inst(&block->instructions.data[i]); free(block->instructions.data); free(block->predecessors.data); free(block->successors.data); }
    free(function->blocks.data);
}

void cinder_ir_destroy(CinderIRModule *module) { for (size_t i = 0U; i < module->functions.len; ++i) destroy_function(&module->functions.data[i]); for (size_t i = 0U; i < module->globals.len; ++i) free(module->globals.data[i].name); free(module->globals.data); free(module->functions.data); cinder_arena_destroy(&module->arena); }

static char *decode_string_literal(CinderArena *arena, const char *raw, size_t *length) {
    size_t raw_len = strlen(raw);
    size_t start = raw_len > 0U && raw[0] == '"' ? 1U : 0U;
    size_t end = raw_len > start && raw[raw_len - 1U] == '"' ? raw_len - 1U : raw_len;
    char *bytes = cinder_arena_alloc(arena, end - start + 1U, _Alignof(char));
    size_t out = 0U;
    for (size_t i = start; i < end; ++i) {
        if (raw[i] == '\\' && i + 1U < end) {
            ++i;
            switch (raw[i]) { case 'n': bytes[out++] = '\n'; break; case 'r': bytes[out++] = '\r'; break; case 't': bytes[out++] = '\t'; break; case '0': bytes[out++] = '\0'; break; default: bytes[out++] = raw[i]; break; }
        } else bytes[out++] = raw[i];
    }
    bytes[out] = '\0';
    *length = out;
    return bytes;
}

static void lower_global_decl(CinderIRModule *module, CinderDecl *decl, CinderDiagnostics *diags) {
    CinderIRGlobal global;
    memset(&global, 0, sizeof(global));
    global.name = cinder_strndup(decl->name, strlen(decl->name));
    global.type = decl->type;
    global.read_only = false;
    global.global = !decl->is_static;
    global.is_extern = decl->is_extern;
    global.loc = decl->loc;
    if (decl->initializer != NULL && (decl->initializer->kind == EX_INT || decl->initializer->kind == EX_CHAR)) { global.integer = decl->initializer->as.integer; global.has_initializer = true; }
    else if (decl->initializer != NULL && decl->initializer->kind == EX_STRING) {
        global.bytes = decode_string_literal(&module->arena, decl->initializer->as.string, &global.byte_count);
        global.has_initializer = true;
        global.read_only = true;
        if (global.type->kind == TYPE_ARRAY && global.type->array_len == 0U) { global.type->array_len = global.byte_count + 1U; global.type->size = global.type->array_len * global.type->base->size; global.type->complete = true; }
    } else if (decl->initializer != NULL) cinder_diag(diags, CINDER_ERROR, decl->loc, "global initializer for '%s' is not a supported constant", decl->name);
    cinder_vec_push((CinderVec *)&module->globals, &global);
}

int cinder_lower_ir(CinderIRModule *module, CinderAst *ast, CinderDiagnostics *diags) {
    for (size_t i = 0U; i < ast->declarations.len; ++i) {
        CinderDecl *decl = ast->declarations.data[i];
        if (decl->kind == DECL_VAR) { lower_global_decl(module, decl, diags); continue; }
        if (decl->kind != DECL_FUNCTION || !decl->is_definition) continue;
        CinderIRFunction function; memset(&function, 0, sizeof(function)); function.name = cinder_strndup(decl->name, strlen(decl->name)); function.type = decl->type; function.ast = ast; function.params.data = NULL; function.params.len = 0U; function.params.cap = 0U; function.blocks.data = NULL; function.blocks.len = 0U; function.blocks.cap = 0U; function.value_count = 0U; function.local_count = 0U; function.float_param_count = 0U; function.types = module->types; function.global = !decl->is_static;
        for (size_t p = 0U; p < decl->params.len; ++p) { CinderDecl *param = decl->params.data[p]; cinder_vec_push((CinderVec *)&function.params, &param); }
        CinderIRBlock entry = make_block(&function, "entry"); cinder_vec_push((CinderVec *)&function.blocks, &entry);
        LowerContext context; context.function = &function; context.diags = diags; context.current = 0U; context.locals.data = NULL; context.locals.len = 0U; context.locals.cap = 0U; context.break_blocks.data = NULL; context.break_blocks.len = 0U; context.break_blocks.cap = 0U; context.continue_blocks.data = NULL; context.continue_blocks.len = 0U; context.continue_blocks.cap = 0U; context.va_index = 0U;
        size_t integer_param_count = 0U;
        for (size_t p = 0U; p < decl->params.len; ++p) { CinderDecl *param = decl->params.data[p]; LocalSlot local = {param->name, new_local(&context, param->type), param->type}; cinder_vec_push((CinderVec *)&context.locals, &local); bool is_float = param->type != NULL && (param->type->kind == TYPE_FLOAT || param->type->kind == TYPE_DOUBLE); CinderIRInst *arg = add_inst_ptr(&function, 0U, is_float ? IR_FARG : IR_ARG, param->loc); arg->dst = new_value(&function); arg->slot = (int)p; arg->operator_code = is_float ? (int)function.float_param_count++ : (int)integer_param_count++; arg->type = param->type; CinderValueId argument_value = arg->dst; CinderIRInst *store = add_inst_ptr(&function, 0U, IR_LOCAL_STORE, param->loc); store->left = argument_value; store->type = param->type; store->slot = local.slot; }
        lower_stmt(&context, decl->body);
        if (block_at(&function, context.current)->terminator.kind == TERM_UNREACHABLE) {
            CinderValueId returned = CINDER_INVALID_VALUE;
            if (function.type->return_type->kind != TYPE_VOID) {
                CinderIRInst *value = add_inst_ptr(&function, context.current, strcmp(function.name, "main") == 0 ? IR_CONST : IR_UNDEF, decl->loc);
                value->type = function.type->return_type; value->dst = new_value(&function); returned = value->dst;
            }
            CinderIRBlock *block = block_at(&function, context.current); block->terminator.kind = TERM_RETURN; block->terminator.value = returned;
        }
        free(context.locals.data); free(context.break_blocks.data); free(context.continue_blocks.data);
        cinder_vec_push((CinderVec *)&module->functions, &function);
    }
    return diags->errors == 0U ? 0 : 1;
}

const char *cinder_ir_op_name(CinderIROp op) {
    static const char *names[] = {"nop","const","fconst","global.load","global.store","local.load","local.store","arg","farg","va_arg","copy","add","sub","mul","fadd","fsub","fmul","fdiv","fneg","fcmp.eq","fcmp.ne","fcmp.lt","fcmp.le","fcmp.gt","fcmp.ge","div.s","div.u","mod.s","mod.u","neg","not","and","or","xor","shl","shr.s","shr.u","cmp.eq","cmp.ne","cmp.lt.s","cmp.le.s","cmp.gt.s","cmp.ge.s","cmp.lt.u","cmp.le.u","cmp.gt.u","cmp.ge.u","call","phi","convert","undef"};
    return op < CINDER_ARRAY_LEN(names) ? names[op] : "unknown";
}

void cinder_dump_ir(const CinderIRModule *module, FILE *out) {
    for (size_t g = 0U; g < module->globals.len; ++g) fprintf(out, "global %s : %s%s\n", module->globals.data[g].name, cinder_type_name(module->globals.data[g].type), module->globals.data[g].bytes != NULL ? " string" : "");
    for (size_t f = 0U; f < module->functions.len; ++f) {
        const CinderIRFunction *function = &module->functions.data[f]; fprintf(out, "function %s() -> %s {\n", function->name, cinder_type_name(function->type->return_type));
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            const CinderIRBlock *block = &function->blocks.data[b]; fprintf(out, "  block %u %s:\n", block->id, block->name);
            for (size_t i = 0U; i < block->instructions.len; ++i) { const CinderIRInst *inst = &block->instructions.data[i]; fprintf(out, "    "); if (inst->dst != CINDER_INVALID_VALUE) fprintf(out, "%%v%u = ", inst->dst); fprintf(out, "%s", cinder_ir_op_name(inst->op)); if (inst->op == IR_CONST) fprintf(out, " %" PRId64, inst->integer); else if (inst->op == IR_GLOBAL_LOAD || inst->op == IR_GLOBAL_STORE) fprintf(out, " %s", inst->callee); else if (inst->op == IR_LOCAL_LOAD || inst->op == IR_LOCAL_STORE || inst->op == IR_ARG || inst->op == IR_FARG) fprintf(out, " slot=%d", inst->slot); else if (inst->op == IR_PHI) { fputs(" <", out); for (size_t a = 0U; a < inst->args.len; ++a) fprintf(out, "block %u:%%v%u%s", inst->phi_blocks.data[a], inst->args.data[a], a + 1U == inst->args.len ? "" : ", "); fputc('>', out); } else if (inst->op == IR_CALL) { fprintf(out, " %s(", inst->callee); for (size_t a = 0U; a < inst->args.len; ++a) fprintf(out, "%%v%u%s", inst->args.data[a], a + 1U == inst->args.len ? "" : ", "); fputc(')', out); } else if (inst->left != CINDER_INVALID_VALUE) { fprintf(out, " %%v%u", inst->left); if (inst->right != CINDER_INVALID_VALUE) fprintf(out, ", %%v%u", inst->right); } fputc('\n', out); }
            switch (block->terminator.kind) { case TERM_RETURN: fprintf(out, "    return"); if (block->terminator.value != CINDER_INVALID_VALUE) fprintf(out, " %%v%u", block->terminator.value); fputc('\n', out); break; case TERM_JUMP: fprintf(out, "    jump block %u\n", block->terminator.target); break; case TERM_BRANCH: fprintf(out, "    branch %%v%u, block %u, block %u\n", block->terminator.condition, block->terminator.yes, block->terminator.no); break; default: fputs("    unreachable\n", out); break; }
        }
        fputs("}\n", out);
    }
}
