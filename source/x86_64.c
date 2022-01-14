#include "cinder.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define R_X86_64_PLT32 4

typedef struct {
    size_t offset;
    CinderBlockId target;
} BranchFixup;

static void emit8(CinderMachineObject *object, uint8_t value) { cinder_bytes_put8(&object->text, value); }
static void emit32(CinderMachineObject *object, uint32_t value) { cinder_bytes_put32(&object->text, value); }
static void emit64(CinderMachineObject *object, uint64_t value) { cinder_bytes_put64(&object->text, value); }

static void emit_mov_rax_imm(CinderMachineObject *object, int64_t value) { emit8(object, 0x48U); emit8(object, 0xB8U); emit64(object, (uint64_t)value); }
static void emit_mov_rax_mem(CinderMachineObject *object, int offset) { emit8(object, 0x48U); emit8(object, 0x8BU); emit8(object, 0x85U); emit32(object, (uint32_t)offset); }
static void emit_mov_r10_mem(CinderMachineObject *object, int offset) { emit8(object, 0x4CU); emit8(object, 0x8BU); emit8(object, 0x95U); emit32(object, (uint32_t)offset); }
static void emit_mov_rcx_mem(CinderMachineObject *object, int offset) { emit8(object, 0x48U); emit8(object, 0x8BU); emit8(object, 0x8DU); emit32(object, (uint32_t)offset); }
static void emit_mov_mem_rax(CinderMachineObject *object, int offset) { emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, 0x85U); emit32(object, (uint32_t)offset); }
static void emit_mov_reg_from_rax(CinderMachineObject *object, unsigned reg) {
    if (reg < 8U) { emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | (reg << 3U))); }
    else { emit8(object, 0x49U); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | ((reg - 8U) << 3U))); }
}
static void emit_mov_rax_from_reg(CinderMachineObject *object, unsigned reg) {
    if (reg < 8U) { emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | (reg << 3U))); }
    else { emit8(object, 0x4CU); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | ((reg - 8U) << 3U))); }
}
static void emit_prologue(CinderMachineObject *object, size_t frame) { emit8(object, 0x55U); emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, 0xE5U); if (frame != 0U) { emit8(object, 0x48U); emit8(object, 0x81U); emit8(object, 0xECU); emit32(object, (uint32_t)frame); } }
static void emit_epilogue(CinderMachineObject *object) { emit8(object, 0xC9U); emit8(object, 0xC3U); }
static int value_offset(const CinderIRFunction *function, CinderValueId value) { return -(int)((function->local_count + (size_t)value + 1U) * 8U); }
static int local_offset(int slot) { return -(slot + 1) * 8; }
static void load_value(CinderMachineObject *object, const CinderIRFunction *function, CinderValueId value) { emit_mov_rax_mem(object, value_offset(function, value)); }
static void store_value(CinderMachineObject *object, const CinderIRFunction *function, CinderValueId value) { emit_mov_mem_rax(object, value_offset(function, value)); }

static void emit_arg_from_stack(CinderMachineObject *object, unsigned index) { emit8(object, 0x48U); emit8(object, 0x8BU); emit8(object, 0x85U); emit32(object, (uint32_t)(16U + (index - 6U) * 8U)); }

static void emit_binary(CinderMachineObject *object, const CinderIRFunction *function, const CinderIRInst *inst) {
    load_value(object, function, inst->left);
    emit_mov_r10_mem(object, value_offset(function, inst->right));
    switch (inst->op) {
        case IR_ADD: emit8(object, 0x4CU); emit8(object, 0x01U); emit8(object, 0xD0U); break;
        case IR_SUB: emit8(object, 0x4CU); emit8(object, 0x29U); emit8(object, 0xD0U); break;
        case IR_MUL: emit8(object, 0x49U); emit8(object, 0x0FU); emit8(object, 0xAFU); emit8(object, 0xC2U); break;
        case IR_DIV_S: emit8(object, 0x48U); emit8(object, 0x99U); emit8(object, 0x49U); emit8(object, 0xF7U); emit8(object, 0xFAU); break;
        case IR_MOD_S: emit8(object, 0x48U); emit8(object, 0x99U); emit8(object, 0x49U); emit8(object, 0xF7U); emit8(object, 0xF2U); break;
        case IR_BIT_AND: emit8(object, 0x4CU); emit8(object, 0x21U); emit8(object, 0xD0U); break;
        case IR_BIT_OR: emit8(object, 0x4CU); emit8(object, 0x09U); emit8(object, 0xD0U); break;
        case IR_BIT_XOR: emit8(object, 0x4CU); emit8(object, 0x31U); emit8(object, 0xD0U); break;
        case IR_SHL: emit_mov_rcx_mem(object, value_offset(function, inst->right)); emit8(object, 0x48U); emit8(object, 0xD3U); emit8(object, 0xE0U); break;
        case IR_SHR_S: emit_mov_rcx_mem(object, value_offset(function, inst->right)); emit8(object, 0x48U); emit8(object, 0xD3U); emit8(object, 0xF8U); break;
        case IR_SHR_U: emit_mov_rcx_mem(object, value_offset(function, inst->right)); emit8(object, 0x48U); emit8(object, 0xD3U); emit8(object, 0xE8U); break;
        case IR_CMP_EQ: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, 0x94U); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        case IR_CMP_NE: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, 0x95U); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        case IR_CMP_LT_S: case IR_CMP_LT_U: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, inst->op == IR_CMP_LT_U ? 0x92U : 0x9CU); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        case IR_CMP_LE_S: case IR_CMP_LE_U: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, inst->op == IR_CMP_LE_U ? 0x96U : 0x9EU); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        case IR_CMP_GT_S: case IR_CMP_GT_U: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, inst->op == IR_CMP_GT_U ? 0x97U : 0x9FU); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        case IR_CMP_GE_S: case IR_CMP_GE_U: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, inst->op == IR_CMP_GE_U ? 0x93U : 0x9DU); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        default: break;
    }
    store_value(object, function, inst->dst);
}

static unsigned abi_register(unsigned index) { static const unsigned regs[] = {7U, 6U, 2U, 1U, 8U, 9U}; return index < CINDER_ARRAY_LEN(regs) ? regs[index] : 0U; }

void cinder_machine_init(CinderMachineObject *object) { object->text.data = NULL; object->text.len = 0U; object->text.cap = 0U; object->fixups.data = NULL; object->fixups.len = 0U; object->fixups.cap = 0U; object->defined_symbols.data = NULL; object->defined_symbols.len = 0U; object->defined_symbols.cap = 0U; object->symbol_offsets.data = NULL; object->symbol_offsets.len = 0U; object->symbol_offsets.cap = 0U; object->frame_size = 0U; }

void cinder_machine_destroy(CinderMachineObject *object) { free(object->text.data); for (size_t i = 0U; i < object->fixups.len; ++i) free(object->fixups.data[i].symbol); free(object->fixups.data); for (size_t i = 0U; i < object->defined_symbols.len; ++i) free(object->defined_symbols.data[i]); free(object->defined_symbols.data); free(object->symbol_offsets.data); }

static void asm_line(FILE *out, const char *format, ...) {
    if (out == NULL) return;
    va_list args; va_start(args, format); vfprintf(out, format, args); va_end(args); fputc('\n', out);
}

int cinder_lower_x86(const CinderIRFunction *function, CinderAllocation *allocation, CinderMachineObject *object, bool assembly, FILE *asm_out, CinderDiagnostics *diags) {
    (void)diags;
    size_t frame = (function->local_count + function->value_count) * 8U;
    frame = (frame + 15U) & ~((size_t)15U);
    allocation->frame_size = frame;
    size_t start = object->text.len;
    char *name = cinder_strndup(function->name, strlen(function->name));
    cinder_vec_push((CinderVec *)&object->defined_symbols, &name);
    cinder_vec_push((CinderVec *)&object->symbol_offsets, &start);
    if (assembly) asm_line(asm_out, ".text\n.globl %s\n.type %s,@function\n%s:", function->name, function->name, function->name);
    emit_prologue(object, frame);
    CINDER_VEC_TYPE(BranchFixup) branches = {NULL, 0U, 0U};
    size_t *labels = cinder_alloc((function->blocks.len == 0U ? 1U : function->blocks.len) * sizeof(*labels));
    for (size_t i = 0U; i < function->blocks.len; ++i) labels[i] = SIZE_MAX;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        const CinderIRBlock *block = &function->blocks.data[b]; labels[b] = object->text.len;
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderIRInst *inst = &block->instructions.data[i];
            switch (inst->op) {
                case IR_CONST: emit_mov_rax_imm(object, inst->integer); store_value(object, function, inst->dst); break;
                case IR_ARG:
                    if (inst->slot >= 0 && inst->slot < 6) emit_mov_rax_from_reg(object, abi_register((unsigned)inst->slot)); else if (inst->slot >= 6) emit_arg_from_stack(object, (unsigned)inst->slot);
                    store_value(object, function, inst->dst); break;
                case IR_LOCAL_LOAD: emit_mov_rax_mem(object, local_offset(inst->slot)); store_value(object, function, inst->dst); break;
                case IR_PHI: emit_mov_rax_mem(object, local_offset(inst->slot)); store_value(object, function, inst->dst); break;
                case IR_LOCAL_STORE: load_value(object, function, inst->left); emit_mov_mem_rax(object, local_offset(inst->slot)); break;
                case IR_COPY: load_value(object, function, inst->left); store_value(object, function, inst->dst); break;
                case IR_NEG: load_value(object, function, inst->left); emit8(object, 0x48U); emit8(object, 0xF7U); emit8(object, 0xD8U); store_value(object, function, inst->dst); break;
                case IR_BIT_NOT: load_value(object, function, inst->left); emit8(object, 0x48U); emit8(object, 0xF7U); emit8(object, 0xD0U); store_value(object, function, inst->dst); break;
                case IR_CALL:
                    for (size_t a = 0U; a < inst->args.len && a < 6U; ++a) { load_value(object, function, inst->args.data[a]); emit_mov_reg_from_rax(object, abi_register((unsigned)a)); }
                    emit8(object, 0xE8U); size_t fix_offset = object->text.len; emit32(object, 0U); CinderFixup fix = {fix_offset, cinder_strndup(inst->callee, strlen(inst->callee)), R_X86_64_PLT32, -4}; cinder_vec_push((CinderVec *)&object->fixups, &fix); store_value(object, function, inst->dst); break;
                default: emit_binary(object, function, inst); break;
            }
        }
        switch (block->terminator.kind) {
            case TERM_RETURN:
                if (block->terminator.value != CINDER_INVALID_VALUE) load_value(object, function, block->terminator.value);
                emit_epilogue(object); break;
            case TERM_JUMP: emit8(object, 0xE9U); { size_t offset = object->text.len; emit32(object, 0U); BranchFixup branch = {offset, block->terminator.target}; cinder_vec_push((CinderVec *)&branches, &branch); } break;
            case TERM_BRANCH:
                load_value(object, function, block->terminator.condition); emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xF8U); emit8(object, 0U); emit8(object, 0x0FU); emit8(object, 0x85U); { size_t offset = object->text.len; emit32(object, 0U); BranchFixup yes = {offset, block->terminator.yes}; cinder_vec_push((CinderVec *)&branches, &yes); } emit8(object, 0xE9U); { size_t offset = object->text.len; emit32(object, 0U); BranchFixup no = {offset, block->terminator.no}; cinder_vec_push((CinderVec *)&branches, &no); } break;
            case TERM_UNREACHABLE: emit_epilogue(object); break;
        }
    }
    for (size_t i = 0U; i < branches.len; ++i) { BranchFixup *branch = &branches.data[i]; if (branch->target >= function->blocks.len || labels[branch->target] == SIZE_MAX) continue; int64_t displacement = (int64_t)labels[branch->target] - (int64_t)(branch->offset + 4U); cinder_bytes_patch32(&object->text, branch->offset, (uint32_t)(int32_t)displacement); }
    free(labels); free(branches.data);
    if (assembly) {
        fputs("  .byte ", asm_out);
        for (size_t i = start; i < object->text.len; ++i) fprintf(asm_out, "0x%02x%s", object->text.data[i], i + 1U == object->text.len ? "" : ", ");
        fputc('\n', asm_out);
        asm_line(asm_out, ".size %s, .-%s", function->name, function->name);
    }
    return 0;
}
