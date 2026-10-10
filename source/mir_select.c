#include "cinder.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define GPR(n) (UINT64_C(1) << (n))
#define SSE(n) (UINT64_C(1) << (16U + (n)))
#define FLAGS (UINT64_C(1) << 32U)

typedef struct { CinderIROp source; unsigned char bytes[10]; unsigned length; } IntegerForm;
static const IntegerForm integer_forms[] = {
    {IR_ADD,{0x4c,0x01,0xd0},3}, {IR_SUB,{0x4c,0x29,0xd0},3}, {IR_MUL,{0x49,0x0f,0xaf,0xc2},4},
    {IR_DIV_S,{0x48,0x99,0x49,0xf7,0xfa},5}, {IR_MOD_S,{0x48,0x99,0x49,0xf7,0xfa,0x48,0x89,0xd0},8},
    {IR_DIV_U,{0x31,0xd2,0x49,0xf7,0xf2},5}, {IR_MOD_U,{0x31,0xd2,0x49,0xf7,0xf2,0x48,0x89,0xd0},8},
    {IR_BIT_AND,{0x4c,0x21,0xd0},3}, {IR_BIT_OR,{0x4c,0x09,0xd0},3}, {IR_BIT_XOR,{0x4c,0x31,0xd0},3},
    {IR_SHL,{0x48,0xd3,0xe0},3}, {IR_SHR_S,{0x48,0xd3,0xf8},3}, {IR_SHR_U,{0x48,0xd3,0xe8},3},
    {IR_CMP_EQ,{0x4c,0x39,0xd0,0x0f,0x94,0xc0,0x48,0x0f,0xb6,0xc0},10},
    {IR_CMP_NE,{0x4c,0x39,0xd0,0x0f,0x95,0xc0,0x48,0x0f,0xb6,0xc0},10},
    {IR_CMP_LT_S,{0x4c,0x39,0xd0,0x0f,0x9c,0xc0,0x48,0x0f,0xb6,0xc0},10},
    {IR_CMP_LE_S,{0x4c,0x39,0xd0,0x0f,0x9e,0xc0,0x48,0x0f,0xb6,0xc0},10},
    {IR_CMP_GT_S,{0x4c,0x39,0xd0,0x0f,0x9f,0xc0,0x48,0x0f,0xb6,0xc0},10},
    {IR_CMP_GE_S,{0x4c,0x39,0xd0,0x0f,0x9d,0xc0,0x48,0x0f,0xb6,0xc0},10},
    {IR_CMP_LT_U,{0x4c,0x39,0xd0,0x0f,0x92,0xc0,0x48,0x0f,0xb6,0xc0},10},
    {IR_CMP_LE_U,{0x4c,0x39,0xd0,0x0f,0x96,0xc0,0x48,0x0f,0xb6,0xc0},10},
    {IR_CMP_GT_U,{0x4c,0x39,0xd0,0x0f,0x97,0xc0,0x48,0x0f,0xb6,0xc0},10},
    {IR_CMP_GE_U,{0x4c,0x39,0xd0,0x0f,0x93,0xc0,0x48,0x0f,0xb6,0xc0},10},
};

void cinder_selected_mir_init(CinderMIRFunction *machine) { memset(machine, 0, sizeof(*machine)); }
static void destroy_call_plan(CinderMIRCallPlan *plan) {
    if (plan == NULL) return;
    free(plan->arguments); free(plan->argument_types); free(plan->staging); free(plan);
}
void cinder_selected_mir_destroy(CinderMIRFunction *machine) {
    for (size_t b = 0U; b < machine->blocks.len; ++b) {
        for (size_t i = 0U; i < machine->blocks.data[b].instructions.len; ++i) {
            CinderIRInst *inst = &machine->blocks.data[b].instructions.data[i].operands;
            free(inst->callee); free(inst->args.data); free(inst->arg_floats.data); free(inst->phi_blocks.data);
            destroy_call_plan(machine->blocks.data[b].instructions.data[i].call);
            free(machine->blocks.data[b].instructions.data[i].variadic_layout);
        }
        free(machine->blocks.data[b].instructions.data);
    }
    free(machine->blocks.data); free(machine->values); destroy_call_plan(machine->signature); cinder_selected_mir_init(machine);
}

static CinderMIRCallPlan *call_plan(const CinderType *returned, const CinderType *const *types, size_t count, bool outgoing, CinderDiagnostics *diags, CinderLoc loc) {
    if (count > 1000000U) { cinder_diag(diags, CINDER_FATAL, loc, "machine ABI argument extent exceeds the profile"); return NULL; }
    CinderMIRCallPlan *plan = cinder_alloc(sizeof(*plan)); memset(plan, 0, sizeof(*plan));
    if (!cinder_abi_classify(returned, &plan->result)) { cinder_diag(diags, CINDER_FATAL, loc, "unsupported selected ABI return layout"); destroy_call_plan(plan); return NULL; }
    size_t capacity = count == 0U ? 1U : count;
    plan->arguments = cinder_alloc(capacity * sizeof(*plan->arguments)); plan->argument_types = cinder_alloc(capacity * sizeof(*plan->argument_types));
    plan->staging = cinder_alloc(capacity * sizeof(*plan->staging)); plan->argument_count = count;
    plan->state = (CinderABIState){plan->result.memory ? 1U : 0U, 0U, 0U};
    for (size_t a = 0U; a < count; ++a) {
        plan->argument_types[a] = types[a]; plan->staging[a] = 0U;
        if (!cinder_abi_place(types[a], &plan->state, &plan->arguments[a])) { cinder_diag(diags, CINDER_FATAL, loc, "unsupported selected ABI argument layout"); destroy_call_plan(plan); return NULL; }
    }
    if (outgoing) {
        plan->frame_size = plan->state.stack;
        for (size_t a = 0U; a < count; ++a) {
            if (plan->arguments[a].stack_offset != SIZE_MAX) plan->staging[a] = plan->arguments[a].stack_offset;
            else {
                size_t bytes = (plan->arguments[a].value.size + 7U) & ~(size_t)7U;
                if (plan->frame_size > 64U * 1024U * 1024U || bytes > 64U * 1024U * 1024U - plan->frame_size) { cinder_diag(diags, CINDER_FATAL, loc, "selected argument staging exceeds the frame limit"); destroy_call_plan(plan); return NULL; }
                plan->staging[a] = plan->frame_size; plan->frame_size += bytes;
            }
        }
        if (plan->frame_size > 64U * 1024U * 1024U - 15U) { cinder_diag(diags, CINDER_FATAL, loc, "selected call alignment exceeds the frame limit"); destroy_call_plan(plan); return NULL; }
        plan->frame_size = (plan->frame_size + 15U) & ~(size_t)15U;
    }
    return plan;
}

static bool plan_matches(const CinderMIRCallPlan *plan, const CinderMIRCallPlan *expected) {
    if (plan == NULL || expected == NULL || plan->argument_count != expected->argument_count || plan->frame_size != expected->frame_size || plan->state.gpr != expected->state.gpr || plan->state.sse != expected->state.sse || plan->state.stack != expected->state.stack || plan->result.memory != expected->result.memory || plan->result.size != expected->result.size || plan->result.align != expected->result.align || plan->result.count != expected->result.count) return false;
    if (plan->argument_count != 0U && (plan->arguments == NULL || plan->argument_types == NULL || plan->staging == NULL)) return false;
    for (unsigned p = 0U; p < 2U; ++p) if (plan->result.classes[p] != expected->result.classes[p]) return false;
    for (size_t a = 0U; a < plan->argument_count; ++a) {
        if (plan->argument_types[a] != expected->argument_types[a] || plan->staging[a] != expected->staging[a]) return false;
        const CinderABIArgument *x = &plan->arguments[a], *y = &expected->arguments[a];
        if (x->stack_offset != y->stack_offset || x->value.size != y->value.size || x->value.align != y->value.align || x->value.count != y->value.count || x->value.memory != y->value.memory) return false;
        for (unsigned p = 0U; p < 2U; ++p) if (x->registers[p] != y->registers[p] || x->value.classes[p] != y->value.classes[p]) return false;
    }
    return true;
}

static CinderMIRCallPlan *function_plan(const CinderIRFunction *source, CinderDiagnostics *diags) {
    size_t count = source->params.len; const CinderType **types = cinder_alloc((count == 0U ? 1U : count) * sizeof(*types));
    for (size_t p = 0U; p < count; ++p) types[p] = source->params.data[p]->type;
    CinderMIRCallPlan *plan = call_plan(source->type->return_type, types, count, false, diags, (CinderLoc){0}); free(types); return plan;
}

static CinderMIRCallPlan *instruction_plan(const CinderMIRFunction *machine, const CinderIRInst *inst, CinderDiagnostics *diags) {
    if (inst->callee_type == NULL || inst->callee_type->kind != TYPE_FUNCTION || (inst->source_type != NULL && (inst->source_type->kind != TYPE_FUNCTION || inst->source_type->params.len != inst->args.len))) {
        cinder_diag(diags, CINDER_FATAL, inst->loc, "machine call requires verified formal and actual signatures"); return NULL;
    }
    size_t count = inst->args.len; const CinderType **types = cinder_alloc((count == 0U ? 1U : count) * sizeof(*types));
    for (size_t a = 0U; a < count; ++a) {
        if ((size_t)inst->args.data[a] >= machine->value_count) { cinder_diag(diags, CINDER_FATAL, inst->loc, "machine call argument lies outside its definition table"); free(types); return NULL; }
        types[a] = inst->source_type != NULL ? inst->source_type->params.data[a].type : machine->values[inst->args.data[a]].type;
    }
    CinderMIRCallPlan *plan = call_plan(inst->callee_type->return_type, types, count, true, diags, inst->loc); free(types); return plan;
}

static void select_normalization(CinderMIRValue *value) {
    const CinderType *type = value->type;
    value->incoming_bool = type != NULL && type->kind == TYPE_BOOL;
    if (type == NULL || type->kind == TYPE_POINTER || type->size == 8U) return;
    unsigned char *bytes = value->normalization;
    if (type->kind == TYPE_BOOL) {
        static const unsigned char boolean[] = {0x48,0x85,0xc0,0x0f,0x95,0xc0,0x0f,0xb6,0xc0};
        memcpy(bytes, boolean, sizeof(boolean)); value->normalization_size = (unsigned char)sizeof(boolean);
    } else if (type->size == 1U || type->size == 2U) {
        if (!type->is_unsigned) bytes[value->normalization_size++] = 0x48U;
        bytes[value->normalization_size++] = 0x0fU;
        bytes[value->normalization_size++] = type->size == 1U ? (type->is_unsigned ? 0xb6U : 0xbeU) : (type->is_unsigned ? 0xb7U : 0xbfU);
        bytes[value->normalization_size++] = 0xc0U;
    } else if (type->size == 4U) {
        if (type->is_unsigned) { bytes[0] = 0x89U; bytes[1] = 0xc0U; value->normalization_size = 2U; }
        else { bytes[0] = 0x48U; bytes[1] = 0x63U; bytes[2] = 0xc0U; value->normalization_size = 3U; }
    }
}

static bool select_access(const CinderType *type, CinderMIRAccess *access) {
    if (type == NULL || (type->size != 1U && type->size != 2U && type->size != 4U && type->size != 8U)) return false;
    access->floating = cinder_ir_floating(type); access->single = type->kind == TYPE_FLOAT;
    for (unsigned extended = 0U; extended < 2U; ++extended) {
        unsigned char *load = extended ? access->load_extended : access->load;
        unsigned char *store = extended ? access->store_extended : access->store;
        unsigned char nl = 0U, ns = 0U;
        if (access->floating) {
            load[nl++] = store[ns++] = access->single ? 0xf3U : 0xf2U;
            if (extended) { load[nl++] = 0x41U; store[ns++] = 0x41U; }
            load[nl++] = 0x0fU; load[nl++] = 0x10U;
            store[ns++] = 0x0fU; store[ns++] = 0x11U;
        } else {
            if (type->size == 1U || type->size == 2U) {
                load[nl++] = extended ? 0x49U : 0x48U; load[nl++] = 0x0fU;
                load[nl++] = type->size == 1U ? (type->is_unsigned ? 0xb6U : 0xbeU) : (type->is_unsigned ? 0xb7U : 0xbfU);
            } else if (type->size == 4U && !type->is_unsigned) {
                load[nl++] = extended ? 0x49U : 0x48U; load[nl++] = 0x63U;
            } else {
                if (type->size == 8U) load[nl++] = extended ? 0x49U : 0x48U;
                else if (extended) load[nl++] = 0x41U;
                load[nl++] = 0x8bU;
            }
            if (type->size == 2U) store[ns++] = 0x66U;
            if (type->size == 8U) store[ns++] = extended ? 0x49U : 0x48U;
            else if (extended) store[ns++] = 0x41U;
            store[ns++] = type->size == 1U ? 0x88U : 0x89U;
        }
        if (extended) { access->load_extended_size = nl; access->store_extended_size = ns; }
        else { access->load_size = nl; access->store_size = ns; }
    }
    return true;
}

static void select_operation(CinderMIRInst *machine) {
    CinderIROp op = machine->operands.op;
    machine->single_precision = machine->operands.type != NULL && machine->operands.type->kind == TYPE_FLOAT;
    for (size_t n = 0U; n < CINDER_ARRAY_LEN(integer_forms); ++n) {
        const IntegerForm *form = &integer_forms[n];
        if (form->source != op) continue;
        machine->kind = MIR_INTEGER_ALU; machine->encoding_size = form->length;
        memcpy(machine->encoding, form->bytes, form->length);
        machine->fixed_uses = GPR(0)|GPR(10); machine->clobbers = GPR(0)|GPR(10)|FLAGS;
        if (op == IR_DIV_S || op == IR_DIV_U || op == IR_MOD_S || op == IR_MOD_U) machine->clobbers |= GPR(2);
        if (op == IR_SHL || op == IR_SHR_S || op == IR_SHR_U) {
            machine->shift_count = true; machine->fixed_uses |= GPR(1); machine->clobbers |= GPR(1);
        }
        return;
    }
    if (op >= IR_FADD && op <= IR_FDIV) {
        machine->kind = MIR_FLOAT_ALU;
        machine->float_opcode = op == IR_FADD ? 0x58U : op == IR_FSUB ? 0x5cU : op == IR_FMUL ? 0x59U : 0x5eU;
        machine->fixed_uses = SSE(0)|SSE(1); machine->clobbers = SSE(0)|SSE(1); return;
    }
    if (op >= IR_FCMP_EQ && op <= IR_FCMP_GE) {
        machine->kind = MIR_FLOAT_COMPARE;
        machine->condition_opcode = op == IR_FCMP_EQ ? 0x94U : op == IR_FCMP_NE ? 0x95U : op == IR_FCMP_LT ? 0x92U : op == IR_FCMP_LE ? 0x96U : op == IR_FCMP_GT ? 0x97U : 0x93U;
        if (op == IR_FCMP_EQ || op == IR_FCMP_NE || op == IR_FCMP_LT || op == IR_FCMP_LE) {
            machine->parity_opcode = op == IR_FCMP_NE ? 0x9aU : 0x9bU;
            machine->parity_combine = op == IR_FCMP_NE ? 0x08U : 0x20U;
        }
        machine->fixed_uses = SSE(0)|SSE(1); machine->clobbers = SSE(0)|SSE(1)|GPR(0)|GPR(2)|FLAGS; return;
    }
    if (op == IR_NEG || op == IR_BIT_NOT) {
        machine->kind = MIR_INTEGER_UNARY; machine->encoding_size = 3U;
        machine->encoding[0] = 0x48U; machine->encoding[1] = 0xf7U; machine->encoding[2] = op == IR_NEG ? 0xd8U : 0xd0U;
        machine->fixed_uses = GPR(0); machine->clobbers = GPR(0)|FLAGS; return;
    }
    if (op == IR_LOCAL_LOAD || op == IR_LOCAL_STORE || op == IR_LOCAL_INIT || op == IR_GLOBAL_LOAD || op == IR_GLOBAL_STORE || op == IR_MEMORY_LOAD || op == IR_MEMORY_STORE || op == IR_MEMORY_INIT) {
        machine->kind = MIR_MEMORY; (void)select_access(machine->operands.type, &machine->access);
    }
    if (op == IR_BIT_LOAD || op == IR_BIT_STORE || op == IR_BIT_INIT || op == IR_BIT_CONVERT) {
        machine->kind = MIR_BITFIELD;
        CinderType byte; memset(&byte, 0, sizeof(byte)); byte.kind = TYPE_CHAR; byte.size = 1U; byte.is_unsigned = true;
        (void)select_access(&byte, &machine->access);
        unsigned width = (unsigned)machine->operands.integer, offset = (unsigned)machine->operands.operator_code;
        if (width != 0U && width <= 32U && offset < 32U && offset + width <= 32U) {
            machine->bitfield.mask = (UINT64_C(1) << width) - 1U;
            machine->bitfield.begin = offset / 8U; machine->bitfield.end = (offset + width + 7U) / 8U;
            machine->bitfield.shift = offset % 8U;
            machine->bitfield.positioned_mask = machine->bitfield.mask << machine->bitfield.shift;
            machine->bitfield.signed_shift = machine->operands.type->kind != TYPE_BOOL && !machine->operands.type->is_unsigned ? 64U - width : 0U;
        }
    }
    if (op == IR_CONVERT) {
        machine->kind = MIR_CONVERSION;
        const CinderType *from = machine->operands.source_type, *to = machine->operands.type;
        bool from_fp = cinder_ir_floating(from), to_fp = cinder_ir_floating(to);
        if (from_fp && to_fp) machine->conversion = MIR_CONVERT_FLOAT_PRECISION;
        else if (from_fp && to != NULL) machine->conversion = to->kind == TYPE_BOOL ? MIR_CONVERT_FLOAT_BOOL : to->is_unsigned && to->size == 8U ? MIR_CONVERT_FLOAT_UNSIGNED : MIR_CONVERT_FLOAT_SIGNED;
        else if (to_fp && from != NULL) machine->conversion = from->is_unsigned && from->size == 8U ? MIR_CONVERT_UNSIGNED_FLOAT : MIR_CONVERT_SIGNED_FLOAT;
    }
    if (op == IR_CALL) machine->clobbers = GPR(0)|GPR(1)|GPR(2)|GPR(6)|GPR(7)|GPR(8)|GPR(9)|GPR(10)|GPR(11)|UINT64_C(0xffff0000)|FLAGS;
    else if (op != IR_NOP && op != IR_PHI && op != IR_LOCAL_BEGIN && op != IR_LOCAL_END && op != IR_LOCAL_RESET && op != IR_LOCAL_FREEZE && op != IR_VA_END)
        /* Conservative scratch set for target pseudos expanded after allocation. */
        machine->clobbers = GPR(0)|GPR(1)|GPR(2)|GPR(6)|GPR(7)|GPR(10)|GPR(11)|SSE(0)|SSE(1)|FLAGS;
}

static CinderIRInst copy_operands(const CinderIRInst *source) {
    CinderIRInst result = *source;
    result.callee = source->callee == NULL ? NULL : cinder_strndup(source->callee, strlen(source->callee));
    result.args.data = NULL; result.args.len = result.args.cap = 0U;
    result.arg_floats.data = NULL; result.arg_floats.len = result.arg_floats.cap = 0U;
    result.phi_blocks.data = NULL; result.phi_blocks.len = result.phi_blocks.cap = 0U;
    for (size_t a = 0U; a < source->args.len; ++a) cinder_vec_push((CinderVec *)&result.args, &source->args.data[a]);
    for (size_t a = 0U; a < source->arg_floats.len; ++a) cinder_vec_push((CinderVec *)&result.arg_floats, &source->arg_floats.data[a]);
    for (size_t a = 0U; a < source->phi_blocks.len; ++a) cinder_vec_push((CinderVec *)&result.phi_blocks, &source->phi_blocks.data[a]);
    return result;
}

int cinder_select_mir(const CinderIRFunction *source, CinderMIRFunction *machine, CinderDiagnostics *diags) {
    cinder_selected_mir_destroy(machine); machine->source = source;
    if (source == NULL || source->value_count > 1000000U || source->blocks.len == 0U) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "invalid selected machine function extent"); return 1; }
    machine->value_count = source->value_count;
    machine->values = cinder_alloc((machine->value_count == 0U ? 1U : machine->value_count) * sizeof(*machine->values));
    memset(machine->values, 0, machine->value_count * sizeof(*machine->values));
    for (size_t b = 0U; b < source->blocks.len; ++b) {
        CinderMIRBlock block = {{NULL,0U,0U}, source->blocks.data[b].terminator};
        for (size_t i = 0U; i < source->blocks.data[b].instructions.len; ++i) {
            const CinderIRInst *original = &source->blocks.data[b].instructions.data[i];
            CinderMIRInst instruction; memset(&instruction, 0, sizeof(instruction));
            instruction.operands = copy_operands(original); select_operation(&instruction);
            if (original->dst != CINDER_INVALID_VALUE && (size_t)original->dst < machine->value_count) {
                machine->values[original->dst].type = original->type;
                machine->values[original->dst].bank = cinder_ir_floating(original->type) ? MIR_BANK_SSE : MIR_BANK_GPR;
                select_normalization(&machine->values[original->dst]);
            }
            cinder_vec_push((CinderVec *)&block.instructions, &instruction);
        }
        cinder_vec_push((CinderVec *)&machine->blocks, &block);
    }
    machine->signature = function_plan(source, diags); if (machine->signature == NULL) return 1;
    for (size_t b = 0U; b < machine->blocks.len; ++b)
        for (size_t i = 0U; i < machine->blocks.data[b].instructions.len; ++i) {
            CinderMIRInst *selected = &machine->blocks.data[b].instructions.data[i];
            if (selected->operands.op == IR_VA_ARG) {
                selected->variadic_layout = cinder_alloc(sizeof(*selected->variadic_layout));
                if (!cinder_abi_classify(selected->operands.source_type, selected->variadic_layout)) { cinder_diag(diags, CINDER_FATAL, selected->operands.loc, "unsupported selected va_arg layout"); return 1; }
            }
            if (selected->operands.op != IR_CALL) continue;
            selected->call = instruction_plan(machine, &selected->operands, diags); if (selected->call == NULL) return 1;
        }
    return cinder_verify_selected_mir(machine, diags);
}

int cinder_verify_selected_mir(const CinderMIRFunction *machine, CinderDiagnostics *diags) {
    if (machine->source == NULL || machine->value_count != machine->source->value_count || machine->blocks.len != machine->source->blocks.len) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "selected machine function has stale dimensions"); return 1; }
    CinderMIRCallPlan *signature = function_plan(machine->source, diags);
    if (!plan_matches(machine->signature, signature)) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "selected incoming ABI plan is inconsistent");
    destroy_call_plan(signature);
    for (size_t b = 0U; b < machine->blocks.len; ++b) {
        const CinderMIRBlock *block = &machine->blocks.data[b];
        if (block->instructions.len != machine->source->blocks.data[b].instructions.len) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "selected machine block has stale instructions"); continue; }
        const CinderTerminator *term = &block->terminator, *original_term = &machine->source->blocks.data[b].terminator;
        if (term->kind != original_term->kind || term->value != original_term->value || term->condition != original_term->condition || term->target != original_term->target || term->yes != original_term->yes || term->no != original_term->no)
            cinder_diag(diags, CINDER_FATAL, term->loc, "selected machine terminator disagrees with its CFG edge");
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderMIRInst *inst = &block->instructions.data[i];
            const CinderIRInst *original = &machine->source->blocks.data[b].instructions.data[i];
            if (original->op == IR_VA_ARG) {
                CinderABIValue expected_layout;
                if (!cinder_abi_classify(original->source_type, &expected_layout) || inst->variadic_layout == NULL || inst->variadic_layout->size != expected_layout.size || inst->variadic_layout->align != expected_layout.align || inst->variadic_layout->count != expected_layout.count || inst->variadic_layout->memory != expected_layout.memory || inst->variadic_layout->classes[0] != expected_layout.classes[0] || inst->variadic_layout->classes[1] != expected_layout.classes[1]) cinder_diag(diags, CINDER_FATAL, original->loc, "selected variadic ABI layout is inconsistent");
            } else if (inst->variadic_layout != NULL) cinder_diag(diags, CINDER_FATAL, original->loc, "non-variadic instruction owns a va_arg layout");
            if (original->op == IR_CALL) {
                CinderMIRCallPlan *call = instruction_plan(machine, original, diags);
                if (!plan_matches(inst->call, call)) cinder_diag(diags, CINDER_FATAL, original->loc, "selected outgoing ABI plan is inconsistent");
                destroy_call_plan(call);
            } else if (inst->call != NULL) cinder_diag(diags, CINDER_FATAL, original->loc, "non-call machine instruction owns an ABI plan");
            const CinderIRInst *operand = &inst->operands;
            bool same_callee = operand->callee == NULL || original->callee == NULL ? operand->callee == original->callee : strcmp(operand->callee, original->callee) == 0;
            bool same_args = operand->args.len == original->args.len && (operand->args.len == 0U || (operand->args.data != NULL && memcmp(operand->args.data, original->args.data, operand->args.len * sizeof(*operand->args.data)) == 0));
            bool same_classes = operand->arg_floats.len == original->arg_floats.len && (operand->arg_floats.len == 0U || (operand->arg_floats.data != NULL && memcmp(operand->arg_floats.data, original->arg_floats.data, operand->arg_floats.len * sizeof(*operand->arg_floats.data)) == 0));
            bool same_edges = operand->phi_blocks.len == original->phi_blocks.len && (operand->phi_blocks.len == 0U || (operand->phi_blocks.data != NULL && memcmp(operand->phi_blocks.data, original->phi_blocks.data, operand->phi_blocks.len * sizeof(*operand->phi_blocks.data)) == 0));
            if (operand->op != original->op || operand->dst != original->dst || operand->left != original->left || operand->right != original->right || operand->type != original->type || operand->source_type != original->source_type || operand->callee_type != original->callee_type || operand->slot != original->slot || operand->operator_code != original->operator_code || operand->integer != original->integer || memcmp(&operand->floating, &original->floating, sizeof(operand->floating)) != 0 || operand->noreturn_call != original->noreturn_call || operand->floating_result != original->floating_result || !same_callee || !same_args || !same_classes || !same_edges)
                cinder_diag(diags, CINDER_FATAL, inst->operands.loc, "selected machine operands disagree with source definitions");
            CinderMIRInst expected; memset(&expected, 0, sizeof(expected)); expected.operands = *original; select_operation(&expected);
            if (inst->kind != expected.kind || inst->encoding_size != expected.encoding_size || memcmp(inst->encoding, expected.encoding, sizeof(inst->encoding)) != 0 || inst->fixed_uses != expected.fixed_uses || inst->clobbers != expected.clobbers || inst->float_opcode != expected.float_opcode || inst->condition_opcode != expected.condition_opcode || inst->parity_opcode != expected.parity_opcode || inst->parity_combine != expected.parity_combine || inst->single_precision != expected.single_precision || inst->shift_count != expected.shift_count)
                cinder_diag(diags, CINDER_FATAL, inst->operands.loc, "invalid selected x86 instruction contract");
            if (inst->conversion != expected.conversion || memcmp(&inst->access, &expected.access, sizeof(inst->access)) != 0 || (inst->kind == MIR_MEMORY && inst->access.load_size == 0U))
                cinder_diag(diags, CINDER_FATAL, inst->operands.loc, "invalid selected memory or conversion plan");
            if (memcmp(&inst->bitfield, &expected.bitfield, sizeof(inst->bitfield)) != 0 || (inst->kind == MIR_BITFIELD && inst->bitfield.mask == 0U))
                cinder_diag(diags, CINDER_FATAL, inst->operands.loc, "invalid selected bitfield plan");
            if (inst->operands.dst != CINDER_INVALID_VALUE && (size_t)inst->operands.dst < machine->value_count) {
                const CinderMIRValue *value = &machine->values[inst->operands.dst];
                if (value->type != original->type || value->bank != (cinder_ir_floating(original->type) ? MIR_BANK_SSE : MIR_BANK_GPR)) cinder_diag(diags, CINDER_FATAL, inst->operands.loc, "selected machine value has an incorrect register bank");
                CinderMIRValue expected_value; memset(&expected_value, 0, sizeof(expected_value)); expected_value.type = original->type; select_normalization(&expected_value);
                if (value->normalization_size != expected_value.normalization_size || memcmp(value->normalization, expected_value.normalization, sizeof(value->normalization)) != 0 || value->incoming_bool != expected_value.incoming_bool)
                    cinder_diag(diags, CINDER_FATAL, inst->operands.loc, "selected scalar normalization is inconsistent");
            }
        }
    }
    return diags->errors != 0U;
}

void cinder_dump_selected_mir(const CinderMIRFunction *machine, FILE *out) {
    fprintf(out, "selected-mir %s target=x86_64-sysv values=%zu\n", machine->source->name, machine->value_count);
    fprintf(out, "incoming-abi arguments=%zu gpr=%u sse=%u stack=%zu sret=%s\n", machine->signature->argument_count, machine->signature->state.gpr, machine->signature->state.sse, machine->signature->state.stack, machine->signature->result.memory ? "yes" : "no");
    for (size_t b = 0U; b < machine->blocks.len; ++b) {
        fprintf(out, "machine-block %zu\n", b);
        for (size_t i = 0U; i < machine->blocks.data[b].instructions.len; ++i) {
            const CinderMIRInst *inst = &machine->blocks.data[b].instructions.data[i];
            fprintf(out, "  machine-kind=%u origin=%s dst=%u fixed=%09" PRIx64 " clobbers=%09" PRIx64 " bytes=", (unsigned)inst->kind, cinder_ir_op_name(inst->operands.op), inst->operands.dst, inst->fixed_uses, inst->clobbers);
            for (unsigned n = 0U; n < inst->encoding_size; ++n) fprintf(out, "%02x", inst->encoding[n]);
            if (inst->kind == MIR_FLOAT_ALU) fprintf(out, "%02x0f%02xc1", inst->single_precision ? 0xf3U : 0xf2U, inst->float_opcode);
            if (inst->operands.dst != CINDER_INVALID_VALUE && (size_t)inst->operands.dst < machine->value_count) fprintf(out, " bank=%s", machine->values[inst->operands.dst].bank == MIR_BANK_SSE ? "sse" : "gpr");
            if (inst->call != NULL) fprintf(out, " call-abi args=%zu gpr=%u sse=%u stack=%zu frame=%zu sret=%s", inst->call->argument_count, inst->call->state.gpr, inst->call->state.sse, inst->call->state.stack, inst->call->frame_size, inst->call->result.memory ? "yes" : "no");
            if (inst->variadic_layout != NULL) fprintf(out, " va-layout size=%zu align=%zu classes=%u memory=%s", inst->variadic_layout->size, inst->variadic_layout->align, inst->variadic_layout->count, inst->variadic_layout->memory ? "yes" : "no");
            if (inst->kind == MIR_MEMORY || inst->kind == MIR_BITFIELD) {
                fputs(" load-form=", out); for (unsigned n = 0U; n < inst->access.load_size; ++n) fprintf(out, "%02x", inst->access.load[n]);
                fputs(" store-form=", out); for (unsigned n = 0U; n < inst->access.store_size; ++n) fprintf(out, "%02x", inst->access.store[n]);
            }
            if (inst->kind == MIR_CONVERSION) fprintf(out, " conversion=%u precision=%s", (unsigned)inst->conversion, inst->single_precision ? "f32" : "f64");
            if (inst->kind == MIR_BITFIELD) fprintf(out, " occupied=%u:%u mask=%" PRIx64 " shift=%u sign-shift=%u", inst->bitfield.begin, inst->bitfield.end, inst->bitfield.mask, inst->bitfield.shift, inst->bitfield.signed_shift);
            fputc('\n', out);
        }
    }
}
