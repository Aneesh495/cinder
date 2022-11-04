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
void cinder_selected_mir_destroy(CinderMIRFunction *machine) {
    for (size_t b = 0U; b < machine->blocks.len; ++b) {
        for (size_t i = 0U; i < machine->blocks.data[b].instructions.len; ++i) {
            CinderIRInst *inst = &machine->blocks.data[b].instructions.data[i].operands;
            free(inst->callee); free(inst->args.data); free(inst->arg_floats.data); free(inst->phi_blocks.data);
        }
        free(machine->blocks.data[b].instructions.data);
    }
    free(machine->blocks.data); free(machine->values); cinder_selected_mir_init(machine);
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
            }
            cinder_vec_push((CinderVec *)&block.instructions, &instruction);
        }
        cinder_vec_push((CinderVec *)&machine->blocks, &block);
    }
    return cinder_verify_selected_mir(machine, diags);
}

int cinder_verify_selected_mir(const CinderMIRFunction *machine, CinderDiagnostics *diags) {
    if (machine->source == NULL || machine->value_count != machine->source->value_count || machine->blocks.len != machine->source->blocks.len) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "selected machine function has stale dimensions"); return 1; }
    for (size_t b = 0U; b < machine->blocks.len; ++b) {
        const CinderMIRBlock *block = &machine->blocks.data[b];
        if (block->instructions.len != machine->source->blocks.data[b].instructions.len) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "selected machine block has stale instructions"); continue; }
        const CinderTerminator *term = &block->terminator, *original_term = &machine->source->blocks.data[b].terminator;
        if (term->kind != original_term->kind || term->value != original_term->value || term->condition != original_term->condition || term->target != original_term->target || term->yes != original_term->yes || term->no != original_term->no)
            cinder_diag(diags, CINDER_FATAL, term->loc, "selected machine terminator disagrees with its CFG edge");
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderMIRInst *inst = &block->instructions.data[i];
            const CinderIRInst *original = &machine->source->blocks.data[b].instructions.data[i];
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
            if (inst->operands.dst != CINDER_INVALID_VALUE && (size_t)inst->operands.dst < machine->value_count) {
                const CinderMIRValue *value = &machine->values[inst->operands.dst];
                if (value->type != original->type || value->bank != (cinder_ir_floating(original->type) ? MIR_BANK_SSE : MIR_BANK_GPR)) cinder_diag(diags, CINDER_FATAL, inst->operands.loc, "selected machine value has an incorrect register bank");
            }
        }
    }
    return diags->errors != 0U;
}

void cinder_dump_selected_mir(const CinderMIRFunction *machine, FILE *out) {
    fprintf(out, "selected-mir %s target=x86_64-sysv values=%zu\n", machine->source->name, machine->value_count);
    for (size_t b = 0U; b < machine->blocks.len; ++b) {
        fprintf(out, "machine-block %zu\n", b);
        for (size_t i = 0U; i < machine->blocks.data[b].instructions.len; ++i) {
            const CinderMIRInst *inst = &machine->blocks.data[b].instructions.data[i];
            fprintf(out, "  machine-kind=%u origin=%s dst=%u fixed=%09" PRIx64 " clobbers=%09" PRIx64 " bytes=", (unsigned)inst->kind, cinder_ir_op_name(inst->operands.op), inst->operands.dst, inst->fixed_uses, inst->clobbers);
            for (unsigned n = 0U; n < inst->encoding_size; ++n) fprintf(out, "%02x", inst->encoding[n]);
            if (inst->kind == MIR_FLOAT_ALU) fprintf(out, "%02x0f%02xc1", inst->single_precision ? 0xf3U : 0xf2U, inst->float_opcode);
            if (inst->operands.dst != CINDER_INVALID_VALUE && (size_t)inst->operands.dst < machine->value_count) fprintf(out, " bank=%s", machine->values[inst->operands.dst].bank == MIR_BANK_SSE ? "sse" : "gpr");
            fputc('\n', out);
        }
    }
}
