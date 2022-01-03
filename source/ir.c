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

static CinderIRInst add_inst(CinderIRFunction *function, CinderBlockId block_id, CinderIROp op, CinderLoc loc) {
    CinderIRInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.op = op; inst.dst = CINDER_INVALID_VALUE; inst.left = CINDER_INVALID_VALUE; inst.right = CINDER_INVALID_VALUE; inst.slot = -1; inst.loc = loc;
    inst.args.data = NULL; inst.args.len = 0U; inst.args.cap = 0U;
    CinderIRBlock *block = block_at(function, block_id);
    cinder_vec_push((CinderVec *)&block->instructions, &inst);
    return inst;
}

static CinderIRInst *add_inst_ptr(CinderIRFunction *function, CinderBlockId block_id, CinderIROp op, CinderLoc loc) {
    CinderIRBlock *block = block_at(function, block_id);
    CinderIRInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.op = op; inst.dst = CINDER_INVALID_VALUE; inst.left = CINDER_INVALID_VALUE; inst.right = CINDER_INVALID_VALUE; inst.slot = -1; inst.loc = loc;
    inst.args.data = NULL; inst.args.len = 0U; inst.args.cap = 0U;
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
} LowerContext;

static int find_local(const LowerContext *context, const char *name) {
    for (size_t i = context->locals.len; i > 0U; --i) if (strcmp(context->locals.data[i - 1U].name, name) == 0) return context->locals.data[i - 1U].slot;
    return -1;
}

static CinderValueId lower_expr(LowerContext *context, CinderExpr *expr);

static CinderValueId lower_call(LowerContext *context, CinderExpr *expr) {
    CINDER_VEC_TYPE(CinderValueId) lowered = {NULL, 0U, 0U};
    for (size_t i = 0U; i < expr->as.call.args.len; ++i) {
        CinderValueId arg = lower_expr(context, expr->as.call.args.data[i]);
        cinder_vec_push((CinderVec *)&lowered, &arg);
    }
    CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CALL, expr->loc);
    inst->dst = new_value(context->function);
    if (expr->as.call.callee->kind == EX_NAME) inst->callee = cinder_strndup(expr->as.call.callee->as.name, strlen(expr->as.call.callee->as.name));
    else inst->callee = cinder_strndup("<indirect>", 10U);
    for (size_t i = 0U; i < lowered.len; ++i) cinder_vec_push((CinderVec *)&inst->args, &lowered.data[i]);
    free(lowered.data);
    return inst->dst;
}

static CinderValueId lower_expr(LowerContext *context, CinderExpr *expr) {
    if (expr == NULL) return CINDER_INVALID_VALUE;
    if (expr->kind == EX_INT || expr->kind == EX_CHAR) {
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CONST, expr->loc);
        inst->dst = new_value(context->function); inst->integer = expr->as.integer; return inst->dst;
    }
    if (expr->kind == EX_NAME) {
        int slot = find_local(context, expr->as.name);
        if (slot >= 0) {
            CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_LOCAL_LOAD, expr->loc);
            inst->dst = new_value(context->function); inst->slot = slot; return inst->dst;
        }
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CONST, expr->loc);
        inst->dst = new_value(context->function); inst->integer = 0; return inst->dst;
    }
    if (expr->kind == EX_CALL) return lower_call(context, expr);
    if (expr->kind == EX_ASSIGN) {
        CinderValueId value = lower_expr(context, expr->as.assign.value);
        if (expr->as.assign.target->kind == EX_NAME) {
            int slot = find_local(context, expr->as.assign.target->as.name);
            if (slot >= 0) {
                CinderIRInst *store = add_inst_ptr(context->function, context->current, IR_LOCAL_STORE, expr->loc);
                store->left = value; store->slot = slot;
            }
        }
        return value;
    }
    if (expr->kind == EX_UNARY) {
        int op = expr->as.unary.op;
        if (op == TOK_PLUSPLUS || op == TOK_MINUSMINUS) {
            CinderValueId old = lower_expr(context, expr->as.unary.value);
            CinderIRInst *one = add_inst_ptr(context->function, context->current, IR_CONST, expr->loc); one->dst = new_value(context->function); one->integer = 1;
            CinderIRInst *add = add_inst_ptr(context->function, context->current, op == TOK_PLUSPLUS ? IR_ADD : IR_SUB, expr->loc); add->dst = new_value(context->function); add->left = old; add->right = one->dst; add->loc = expr->loc;
            if (expr->as.unary.value->kind == EX_NAME) {
                int slot = find_local(context, expr->as.unary.value->as.name);
                if (slot >= 0) { CinderIRInst *store = add_inst_ptr(context->function, context->current, IR_LOCAL_STORE, expr->loc); store->left = add->dst; store->slot = slot; }
            }
            return add->dst;
        }
        CinderValueId value = lower_expr(context, expr->as.unary.value);
        CinderIROp ir_op = op == '-' ? IR_NEG : (op == '~' ? IR_BIT_NOT : IR_COPY);
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, ir_op, expr->loc); inst->dst = new_value(context->function); inst->left = value; return inst->dst;
    }
    if (expr->kind == EX_BINARY) {
        CinderValueId left = lower_expr(context, expr->as.binary.left); CinderValueId right = lower_expr(context, expr->as.binary.right);
        CinderIROp op = IR_ADD;
        switch (expr->as.binary.op) {
            case '+': op = IR_ADD; break; case '-': op = IR_SUB; break; case '*': op = IR_MUL; break; case '/': op = IR_DIV_S; break; case '%': op = IR_MOD_S; break;
            case '&': op = IR_BIT_AND; break; case '|': op = IR_BIT_OR; break; case '^': op = IR_BIT_XOR; break; case TOK_SHL: op = IR_SHL; break; case TOK_SHR: op = IR_SHR_S; break;
            case TOK_EQEQ: op = IR_CMP_EQ; break; case TOK_NEQ: op = IR_CMP_NE; break; case '<': op = IR_CMP_LT_S; break; case TOK_LE: op = IR_CMP_LE_S; break; case '>': op = IR_CMP_GT_S; break; case TOK_GE: op = IR_CMP_GE_S; break;
            case TOK_ANDAND: op = IR_BIT_AND; break; case TOK_OROR: op = IR_BIT_OR; break; default: break;
        }
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, op, expr->loc); inst->dst = new_value(context->function); inst->left = left; inst->right = right; return inst->dst;
    }
    if (expr->kind == EX_SIZEOF) {
        CinderIRInst *inst = add_inst_ptr(context->function, context->current, IR_CONST, expr->loc); inst->dst = new_value(context->function); inst->integer = expr->as.unary.value->type == NULL ? 0 : (int64_t)expr->as.unary.value->type->size; return inst->dst;
    }
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
            int slot = (int)context->function->local_count++;
            LocalSlot local = {stmt->as.decl->name, slot, stmt->as.decl->type}; cinder_vec_push((CinderVec *)&context->locals, &local);
            if (stmt->as.decl->initializer != NULL) { CinderValueId value = lower_expr(context, stmt->as.decl->initializer); CinderIRInst *store = add_inst_ptr(context->function, context->current, IR_LOCAL_STORE, stmt->loc); store->left = value; store->slot = slot; }
            break;
        }
        case ST_RETURN: {
            CinderIRBlock *block = block_at(context->function, context->current);
            block->terminator.kind = TERM_RETURN; block->terminator.value = stmt->as.ret.value == NULL ? CINDER_INVALID_VALUE : lower_expr(context, stmt->as.ret.value); block->terminator.loc = stmt->loc; break;
        }
        case ST_BLOCK:
            for (size_t i = 0U; i < stmt->as.block.items.len; ++i) {
                lower_stmt(context, stmt->as.block.items.data[i]);
                if (block_at(context->function, context->current)->terminator.kind != TERM_UNREACHABLE) break;
            }
            break;
        case ST_IF: {
            CinderValueId condition = lower_expr(context, stmt->as.if_stmt.condition);
            CinderBlockId then_id = (CinderBlockId)context->function->blocks.len; CinderIRBlock then_block = make_block(context->function, "then"); cinder_vec_push((CinderVec *)&context->function->blocks, &then_block);
            CinderBlockId else_id = (CinderBlockId)context->function->blocks.len; CinderIRBlock else_block = make_block(context->function, "else"); cinder_vec_push((CinderVec *)&context->function->blocks, &else_block);
            CinderBlockId merge_id = (CinderBlockId)context->function->blocks.len; CinderIRBlock merge_block = make_block(context->function, "merge"); cinder_vec_push((CinderVec *)&context->function->blocks, &merge_block);
            CinderIRBlock *current = block_at(context->function, context->current); current->terminator.kind = TERM_BRANCH; current->terminator.condition = condition; current->terminator.yes = then_id; current->terminator.no = else_id; current->terminator.loc = stmt->loc; set_successor(context->function, context->current, then_id); set_successor(context->function, context->current, else_id);
            context->current = then_id; lower_stmt(context, stmt->as.if_stmt.then_branch); ensure_block_jump(context, merge_id, stmt->loc);
            context->current = else_id; lower_stmt(context, stmt->as.if_stmt.else_branch); ensure_block_jump(context, merge_id, stmt->loc);
            context->current = merge_id; break;
        }
        case ST_WHILE: {
            CinderBlockId cond_id = (CinderBlockId)context->function->blocks.len; CinderIRBlock cond = make_block(context->function, "loop.cond"); cinder_vec_push((CinderVec *)&context->function->blocks, &cond);
            CinderBlockId body_id = (CinderBlockId)context->function->blocks.len; CinderIRBlock body = make_block(context->function, "loop.body"); cinder_vec_push((CinderVec *)&context->function->blocks, &body);
            CinderBlockId after_id = (CinderBlockId)context->function->blocks.len; CinderIRBlock after = make_block(context->function, "loop.after"); cinder_vec_push((CinderVec *)&context->function->blocks, &after);
            ensure_block_jump(context, cond_id, stmt->loc); context->current = cond_id; CinderValueId condition = lower_expr(context, stmt->as.loop.condition); CinderIRBlock *condition_block = block_at(context->function, cond_id); condition_block->terminator.kind = TERM_BRANCH; condition_block->terminator.condition = condition; condition_block->terminator.yes = body_id; condition_block->terminator.no = after_id; condition_block->terminator.loc = stmt->loc; set_successor(context->function, cond_id, body_id); set_successor(context->function, cond_id, after_id);
            cinder_vec_push((CinderVec *)&context->break_blocks, &after_id); cinder_vec_push((CinderVec *)&context->continue_blocks, &cond_id); context->current = body_id; lower_stmt(context, stmt->as.loop.body); ensure_block_jump(context, cond_id, stmt->loc); context->break_blocks.len--; context->continue_blocks.len--; context->current = after_id; break;
        }
        case ST_FOR: {
            lower_stmt(context, stmt->as.for_stmt.init);
            CinderStmt fake; memset(&fake, 0, sizeof(fake)); fake.kind = ST_WHILE; fake.loc = stmt->loc; fake.as.loop.condition = stmt->as.for_stmt.condition; fake.as.loop.body = stmt->as.for_stmt.body;
            lower_stmt(context, &fake);
            (void)stmt->as.for_stmt.step;
            break;
        }
        case ST_BREAK: if (context->break_blocks.len > 0U) { CinderIRBlock *block = block_at(context->function, context->current); block->terminator.kind = TERM_JUMP; block->terminator.target = context->break_blocks.data[context->break_blocks.len - 1U]; set_successor(context->function, context->current, block->terminator.target); } break;
        case ST_CONTINUE: if (context->continue_blocks.len > 0U) { CinderIRBlock *block = block_at(context->function, context->current); block->terminator.kind = TERM_JUMP; block->terminator.target = context->continue_blocks.data[context->continue_blocks.len - 1U]; set_successor(context->function, context->current, block->terminator.target); } break;
        case ST_DO: break;
    }
}

void cinder_ir_init(CinderIRModule *module, CinderTypeContext *types) {
    module->functions.data = NULL; module->functions.len = 0U; module->functions.cap = 0U; module->types = types; cinder_arena_init(&module->arena, 32768U);
}

static void destroy_inst(CinderIRInst *inst) { free(inst->callee); free(inst->args.data); }
static void destroy_function(CinderIRFunction *function) {
    free(function->name); free(function->params.data);
    for (size_t b = 0U; b < function->blocks.len; ++b) { CinderIRBlock *block = &function->blocks.data[b]; free(block->name); for (size_t i = 0U; i < block->instructions.len; ++i) destroy_inst(&block->instructions.data[i]); free(block->instructions.data); free(block->predecessors.data); free(block->successors.data); }
    free(function->blocks.data);
}

void cinder_ir_destroy(CinderIRModule *module) { for (size_t i = 0U; i < module->functions.len; ++i) destroy_function(&module->functions.data[i]); free(module->functions.data); cinder_arena_destroy(&module->arena); }

int cinder_lower_ir(CinderIRModule *module, CinderAst *ast, CinderDiagnostics *diags) {
    for (size_t i = 0U; i < ast->declarations.len; ++i) {
        CinderDecl *decl = ast->declarations.data[i]; if (decl->kind != DECL_FUNCTION || !decl->is_definition) continue;
        CinderIRFunction function; memset(&function, 0, sizeof(function)); function.name = cinder_strndup(decl->name, strlen(decl->name)); function.type = decl->type; function.ast = ast; function.params.data = NULL; function.params.len = 0U; function.params.cap = 0U; function.blocks.data = NULL; function.blocks.len = 0U; function.blocks.cap = 0U; function.value_count = 0U; function.local_count = 0U;
        for (size_t p = 0U; p < decl->params.len; ++p) { CinderDecl *param = decl->params.data[p]; cinder_vec_push((CinderVec *)&function.params, &param); }
        CinderIRBlock entry = make_block(&function, "entry"); cinder_vec_push((CinderVec *)&function.blocks, &entry);
        LowerContext context; context.function = &function; context.diags = diags; context.current = 0U; context.locals.data = NULL; context.locals.len = 0U; context.locals.cap = 0U; context.break_blocks.data = NULL; context.break_blocks.len = 0U; context.break_blocks.cap = 0U; context.continue_blocks.data = NULL; context.continue_blocks.len = 0U; context.continue_blocks.cap = 0U;
        for (size_t p = 0U; p < decl->params.len; ++p) { CinderDecl *param = decl->params.data[p]; LocalSlot local = {param->name, (int)function.local_count++, param->type}; cinder_vec_push((CinderVec *)&context.locals, &local); CinderIRInst *arg = add_inst_ptr(&function, 0U, IR_ARG, param->loc); arg->dst = new_value(&function); arg->slot = (int)p; CinderIRInst *store = add_inst_ptr(&function, 0U, IR_LOCAL_STORE, param->loc); store->left = arg->dst; store->slot = local.slot; }
        lower_stmt(&context, decl->body);
        if (block_at(&function, context.current)->terminator.kind == TERM_UNREACHABLE) { CinderIRBlock *block = block_at(&function, context.current); block->terminator.kind = TERM_RETURN; block->terminator.value = CINDER_INVALID_VALUE; }
        free(context.locals.data); free(context.break_blocks.data); free(context.continue_blocks.data);
        cinder_vec_push((CinderVec *)&module->functions, &function);
    }
    return diags->errors == 0U ? 0 : 1;
}

static const char *op_name(CinderIROp op) {
    static const char *names[] = {"nop","const","local.load","local.store","arg","copy","add","sub","mul","div.s","div.u","mod.s","mod.u","neg","not","and","or","xor","shl","shr.s","shr.u","cmp.eq","cmp.ne","cmp.lt.s","cmp.le.s","cmp.gt.s","cmp.ge.s","cmp.lt.u","cmp.le.u","cmp.gt.u","cmp.ge.u","call","phi"};
    return op < CINDER_ARRAY_LEN(names) ? names[op] : "unknown";
}

void cinder_dump_ir(const CinderIRModule *module, FILE *out) {
    for (size_t f = 0U; f < module->functions.len; ++f) {
        const CinderIRFunction *function = &module->functions.data[f]; fprintf(out, "function %s() -> %s {\n", function->name, cinder_type_name(function->type->return_type));
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            const CinderIRBlock *block = &function->blocks.data[b]; fprintf(out, "  block %u %s:\n", block->id, block->name);
            for (size_t i = 0U; i < block->instructions.len; ++i) { const CinderIRInst *inst = &block->instructions.data[i]; fprintf(out, "    "); if (inst->dst != CINDER_INVALID_VALUE) fprintf(out, "%%v%u = ", inst->dst); fprintf(out, "%s", op_name(inst->op)); if (inst->op == IR_CONST) fprintf(out, " %" PRId64, inst->integer); else if (inst->op == IR_LOCAL_LOAD || inst->op == IR_LOCAL_STORE || inst->op == IR_ARG) fprintf(out, " slot=%d", inst->slot); else if (inst->op == IR_CALL) { fprintf(out, " %s(", inst->callee); for (size_t a = 0U; a < inst->args.len; ++a) fprintf(out, "%%v%u%s", inst->args.data[a], a + 1U == inst->args.len ? "" : ", "); fputc(')', out); } else if (inst->left != CINDER_INVALID_VALUE) { fprintf(out, " %%v%u", inst->left); if (inst->right != CINDER_INVALID_VALUE) fprintf(out, ", %%v%u", inst->right); } fputc('\n', out); }
            switch (block->terminator.kind) { case TERM_RETURN: fprintf(out, "    return"); if (block->terminator.value != CINDER_INVALID_VALUE) fprintf(out, " %%v%u", block->terminator.value); fputc('\n', out); break; case TERM_JUMP: fprintf(out, "    jump block %u\n", block->terminator.target); break; case TERM_BRANCH: fprintf(out, "    branch %%v%u, block %u, block %u\n", block->terminator.condition, block->terminator.yes, block->terminator.no); break; default: fputs("    unreachable\n", out); break; }
        }
        fputs("}\n", out);
    }
}
