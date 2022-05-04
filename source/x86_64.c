#include "cinder.h"

#include <stdlib.h>
#include <string.h>

#define R_X86_64_PLT32 4
#define R_X86_64_PC32 2

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
    if (reg < 8U) { emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | reg)); }
    else { emit8(object, 0x49U); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | (reg - 8U))); }
}
static void emit_mov_rax_from_reg(CinderMachineObject *object, unsigned reg) {
    if (reg < 8U) { emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | (reg << 3U))); }
    else { emit8(object, 0x4CU); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | ((reg - 8U) << 3U))); }
}
static void emit_prologue(CinderMachineObject *object, size_t frame) { emit8(object, 0x55U); emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, 0xE5U); if (frame != 0U) { emit8(object, 0x48U); emit8(object, 0x81U); emit8(object, 0xECU); emit32(object, (uint32_t)frame); } }
static void emit_epilogue(CinderMachineObject *object) { emit8(object, 0xC9U); emit8(object, 0xC3U); }
static int value_offset(const CinderIRFunction *function, CinderValueId value) { return -(int)((function->local_count + (size_t)value + 1U) * 8U); }
static int local_offset(int slot) { return -(slot + 1) * 8; }
static unsigned register_code(CinderRegister reg) {
    switch (reg) { case REG_RAX: return 0U; case REG_RCX: return 1U; case REG_RDX: return 2U; case REG_RSI: return 6U; case REG_RDI: return 7U; case REG_R8: return 8U; case REG_R9: return 9U; case REG_R10: return 10U; case REG_R11: return 11U; case REG_R12: return 12U; case REG_R13: return 13U; case REG_R14: return 14U; case REG_R15: return 15U; default: return 0U; }
}

static void emit_mov_reg_reg(CinderMachineObject *object, unsigned destination, unsigned source) {
    uint8_t rex = 0x48U;
    if (source >= 8U) rex |= 0x04U;
    if (destination >= 8U) rex |= 0x01U;
    emit8(object, rex); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | ((source & 7U) << 3U) | (destination & 7U)));
}

static const CinderLocation *location_for(const CinderAllocation *allocation, CinderValueId value) {
    for (size_t i = 0U; i < allocation->intervals.len; ++i) if (allocation->intervals.data[i].value == value) return &allocation->intervals.data[i].location;
    return NULL;
}

static void normalize_integer(CinderMachineObject *object, const CinderType *type) {
    if (type == NULL || type->kind == TYPE_POINTER || type->size == 8U) return;
    if (type->kind == TYPE_BOOL) {
        emit8(object, 0x48U); emit8(object, 0x85U); emit8(object, 0xC0U);
        emit8(object, 0x0FU); emit8(object, 0x95U); emit8(object, 0xC0U);
        emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U);
    } else if (type->size == 1U || type->size == 2U) {
        if (!type->is_unsigned) emit8(object, 0x48U);
        emit8(object, 0x0FU);
        emit8(object, type->size == 1U ? (type->is_unsigned ? 0xB6U : 0xBEU) : (type->is_unsigned ? 0xB7U : 0xBFU));
        emit8(object, 0xC0U);
    } else if (type->size == 4U) {
        if (type->is_unsigned) { emit8(object, 0x89U); emit8(object, 0xC0U); }
        else { emit8(object, 0x48U); emit8(object, 0x63U); emit8(object, 0xC0U); }
    }
}

static void load_value_alloc(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) emit_mov_reg_reg(object, 0U, register_code(location->reg));
    else emit_mov_rax_mem(object, location != NULL ? location->stack_offset : value_offset(function, value));
}

static void store_value_alloc(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    normalize_integer(object, cinder_ir_value_type(function, value));
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) emit_mov_reg_reg(object, register_code(location->reg), 0U);
    else emit_mov_mem_rax(object, location != NULL ? location->stack_offset : value_offset(function, value));
}

static void load_value_to_r10(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) emit_mov_reg_reg(object, 10U, register_code(location->reg));
    else emit_mov_r10_mem(object, location != NULL ? location->stack_offset : value_offset(function, value));
}

static void load_value_to_rcx(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) emit_mov_reg_reg(object, 1U, register_code(location->reg));
    else emit_mov_rcx_mem(object, location != NULL ? location->stack_offset : value_offset(function, value));
}

static void emit_movsd_xmm_xmm(CinderMachineObject *object, unsigned destination, unsigned source) { emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x10U); emit8(object, (uint8_t)(0xC0U | ((destination & 7U) << 3U) | (source & 7U))); }

static void store_float_value(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) {
        emit_movsd_xmm_xmm(object, (unsigned)(location->reg - REG_XMM2) + 2U, 0U);
        return;
    }
    int offset = location != NULL ? location->stack_offset : value_offset(function, value);
    emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x11U); emit8(object, 0x85U); emit32(object, (uint32_t)offset);
}

static void load_float_value(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value, unsigned xmm) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) {
        emit_movsd_xmm_xmm(object, xmm, (unsigned)(location->reg - REG_XMM2) + 2U);
        return;
    }
    int offset = location != NULL ? location->stack_offset : value_offset(function, value);
    emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x10U); emit8(object, (uint8_t)(0x85U | ((xmm & 7U) << 3U))); emit32(object, (uint32_t)offset);
}

static void float32_round(CinderMachineObject *object, unsigned xmm) {
    uint8_t operands = (uint8_t)(0xC0U | ((xmm & 7U) << 3U) | (xmm & 7U));
    emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, operands);
    emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, operands);
}

static void float_from_integer(CinderMachineObject *object, bool single) {
    emit8(object, single ? 0xF3U : 0xF2U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0x2AU); emit8(object, 0xC0U);
}

static void integer_from_double(CinderMachineObject *object) {
    emit8(object, 0xF2U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0x2CU); emit8(object, 0xC0U);
}

static void emit_conversion(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst) {
    bool from_fp = cinder_ir_floating(inst->source_type);
    bool to_fp = cinder_ir_floating(inst->type);
    if (from_fp) load_float_value(object, function, allocation, inst->left, 0U);
    else load_value_alloc(object, function, allocation, inst->left);
    if (!from_fp && to_fp) {
        bool single = inst->type->kind == TYPE_FLOAT;
        if (inst->source_type->is_unsigned && inst->source_type->size == 8U) {
            emit8(object, 0x48U); emit8(object, 0x85U); emit8(object, 0xC0U);
            emit8(object, 0x0FU); emit8(object, 0x89U); size_t normal = object->text.len; emit32(object, 0U);
            emit_mov_reg_reg(object, 10U, 0U);
            emit8(object, 0x49U); emit8(object, 0x83U); emit8(object, 0xE2U); emit8(object, 1U);
            emit8(object, 0x48U); emit8(object, 0xD1U); emit8(object, 0xE8U);
            emit8(object, 0x4CU); emit8(object, 0x09U); emit8(object, 0xD0U);
            float_from_integer(object, single);
            emit8(object, single ? 0xF3U : 0xF2U); emit8(object, 0x0FU); emit8(object, 0x58U); emit8(object, 0xC0U);
            emit8(object, 0xE9U); size_t done = object->text.len; emit32(object, 0U);
            cinder_bytes_patch32(&object->text, normal, (uint32_t)(object->text.len - normal - 4U));
            float_from_integer(object, single);
            cinder_bytes_patch32(&object->text, done, (uint32_t)(object->text.len - done - 4U));
        } else float_from_integer(object, single);
        if (single) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
    } else if (from_fp && !to_fp) {
        if (inst->type->kind == TYPE_BOOL) {
            emit8(object, 0x66U); emit8(object, 0x0FU); emit8(object, 0x57U); emit8(object, 0xC9U);
            emit8(object, 0x66U); emit8(object, 0x0FU); emit8(object, 0x2EU); emit8(object, 0xC1U);
            emit8(object, 0x0FU); emit8(object, 0x95U); emit8(object, 0xC0U);
            emit8(object, 0x0FU); emit8(object, 0x9AU); emit8(object, 0xC2U);
            emit8(object, 0x08U); emit8(object, 0xD0U);
            emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U);
        } else if (inst->type->is_unsigned && inst->type->size == 8U) {
            emit_mov_rax_imm(object, INT64_C(0x43E0000000000000));
            emit8(object, 0x66U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0x6EU); emit8(object, 0xC8U);
            emit8(object, 0x66U); emit8(object, 0x0FU); emit8(object, 0x2EU); emit8(object, 0xC1U);
            emit8(object, 0x0FU); emit8(object, 0x82U); size_t small = object->text.len; emit32(object, 0U);
            emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5CU); emit8(object, 0xC1U);
            integer_from_double(object);
            emit_mov_reg_reg(object, 10U, 0U); emit_mov_rax_imm(object, INT64_MIN);
            emit8(object, 0x4CU); emit8(object, 0x31U); emit8(object, 0xD0U);
            emit8(object, 0xE9U); size_t done = object->text.len; emit32(object, 0U);
            cinder_bytes_patch32(&object->text, small, (uint32_t)(object->text.len - small - 4U));
            integer_from_double(object);
            cinder_bytes_patch32(&object->text, done, (uint32_t)(object->text.len - done - 4U));
        } else integer_from_double(object);
    }
    if (to_fp) {
        if (inst->type->kind == TYPE_FLOAT) float32_round(object, 0U);
        store_float_value(object, function, allocation, inst->dst);
    } else store_value_alloc(object, function, allocation, inst->dst);
}
static void store_float_constant(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value, double floating) {
    size_t offset = object->rodata.len;
    while ((object->rodata.len & 7U) != 0U) cinder_bytes_put8(&object->rodata, 0U);
    offset = object->rodata.len;
    uint64_t bits = 0U; memcpy(&bits, &floating, sizeof(bits)); cinder_bytes_put64(&object->rodata, bits);
    char name[64]; int written = snprintf(name, sizeof(name), ".LCF%u", object->literal_counter++);
    if (written <= 0 || (size_t)written >= sizeof(name)) return;
    CinderDataSymbol symbol; symbol.name = cinder_strndup(name, (size_t)written); symbol.section_kind = 2U; symbol.offset = offset; symbol.size = 8U; symbol.global = false; cinder_vec_push((CinderVec *)&object->data_symbols, &symbol);
    emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x10U); emit8(object, 0x05U); size_t fix_offset = object->text.len; emit32(object, 0U); CinderFixup fix = {fix_offset, cinder_strndup(name, (size_t)written), R_X86_64_PC32, -4}; cinder_vec_push((CinderVec *)&object->fixups, &fix);
    store_float_value(object, function, allocation, value);
}

static void emit_float_binary(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst) {
    load_float_value(object, function, allocation, inst->left, 0U);
    load_float_value(object, function, allocation, inst->right, 1U);
    bool single = inst->type->kind == TYPE_FLOAT;
    if (single) {
        emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U);
        emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC9U);
    }
    uint8_t opcode = inst->op == IR_FADD ? 0x58U : inst->op == IR_FSUB ? 0x5CU : inst->op == IR_FMUL ? 0x59U : 0x5EU;
    emit8(object, single ? 0xF3U : 0xF2U); emit8(object, 0x0FU); emit8(object, opcode); emit8(object, 0xC1U);
    if (single) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
    store_float_value(object, function, allocation, inst->dst);
}

static void emit_float_compare(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst) {
    load_float_value(object, function, allocation, inst->left, 0U); load_float_value(object, function, allocation, inst->right, 1U); emit8(object, 0x66U); emit8(object, 0x0FU); emit8(object, 0x2EU); emit8(object, 0xC1U);
    uint8_t condition = 0x94U; switch (inst->op) { case IR_FCMP_EQ: condition = 0x94U; break; case IR_FCMP_NE: condition = 0x95U; break; case IR_FCMP_LT: condition = 0x92U; break; case IR_FCMP_LE: condition = 0x96U; break; case IR_FCMP_GT: condition = 0x97U; break; case IR_FCMP_GE: condition = 0x93U; break; default: break; }
    emit8(object, 0x0FU); emit8(object, condition); emit8(object, 0xC0U);
    if (inst->op == IR_FCMP_EQ || inst->op == IR_FCMP_LT || inst->op == IR_FCMP_LE || inst->op == IR_FCMP_NE) {
        emit8(object, 0x0FU); emit8(object, inst->op == IR_FCMP_NE ? 0x9AU : 0x9BU); emit8(object, 0xC2U);
        emit8(object, inst->op == IR_FCMP_NE ? 0x08U : 0x20U); emit8(object, 0xD0U);
    }
    emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); store_value_alloc(object, function, allocation, inst->dst);
}

static void emit_arg_from_stack(CinderMachineObject *object, unsigned index) { emit8(object, 0x48U); emit8(object, 0x8BU); emit8(object, 0x85U); emit32(object, (uint32_t)(16U + (index - 6U) * 8U)); }

static void emit_binary(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst) {
    load_value_alloc(object, function, allocation, inst->left);
    load_value_to_r10(object, function, allocation, inst->right);
    switch (inst->op) {
        case IR_ADD: emit8(object, 0x4CU); emit8(object, 0x01U); emit8(object, 0xD0U); break;
        case IR_SUB: emit8(object, 0x4CU); emit8(object, 0x29U); emit8(object, 0xD0U); break;
        case IR_MUL: emit8(object, 0x49U); emit8(object, 0x0FU); emit8(object, 0xAFU); emit8(object, 0xC2U); break;
        case IR_DIV_S: emit8(object, 0x48U); emit8(object, 0x99U); emit8(object, 0x49U); emit8(object, 0xF7U); emit8(object, 0xFAU); break;
        case IR_MOD_S: emit8(object, 0x48U); emit8(object, 0x99U); emit8(object, 0x49U); emit8(object, 0xF7U); emit8(object, 0xFAU); emit_mov_reg_reg(object, 0U, 2U); break;
        case IR_DIV_U: case IR_MOD_U:
            emit8(object, 0x31U); emit8(object, 0xD2U); emit8(object, 0x49U); emit8(object, 0xF7U); emit8(object, 0xF2U);
            if (inst->op == IR_MOD_U) emit_mov_reg_reg(object, 0U, 2U);
            break;
        case IR_BIT_AND: emit8(object, 0x4CU); emit8(object, 0x21U); emit8(object, 0xD0U); break;
        case IR_BIT_OR: emit8(object, 0x4CU); emit8(object, 0x09U); emit8(object, 0xD0U); break;
        case IR_BIT_XOR: emit8(object, 0x4CU); emit8(object, 0x31U); emit8(object, 0xD0U); break;
        case IR_SHL: load_value_to_rcx(object, function, allocation, inst->right); emit8(object, 0x48U); emit8(object, 0xD3U); emit8(object, 0xE0U); break;
        case IR_SHR_S: load_value_to_rcx(object, function, allocation, inst->right); emit8(object, 0x48U); emit8(object, 0xD3U); emit8(object, 0xF8U); break;
        case IR_SHR_U: load_value_to_rcx(object, function, allocation, inst->right); emit8(object, 0x48U); emit8(object, 0xD3U); emit8(object, 0xE8U); break;
        case IR_CMP_EQ: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, 0x94U); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        case IR_CMP_NE: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, 0x95U); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        case IR_CMP_LT_S: case IR_CMP_LT_U: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, inst->op == IR_CMP_LT_U ? 0x92U : 0x9CU); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        case IR_CMP_LE_S: case IR_CMP_LE_U: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, inst->op == IR_CMP_LE_U ? 0x96U : 0x9EU); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        case IR_CMP_GT_S: case IR_CMP_GT_U: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, inst->op == IR_CMP_GT_U ? 0x97U : 0x9FU); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        case IR_CMP_GE_S: case IR_CMP_GE_U: emit8(object, 0x4CU); emit8(object, 0x39U); emit8(object, 0xD0U); emit8(object, 0x0FU); emit8(object, inst->op == IR_CMP_GE_U ? 0x93U : 0x9DU); emit8(object, 0xC0U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); break;
        default: break;
    }
    store_value_alloc(object, function, allocation, inst->dst);
}

static unsigned abi_register(unsigned index) { static const unsigned regs[] = {7U, 6U, 2U, 1U, 8U, 9U}; return index < CINDER_ARRAY_LEN(regs) ? regs[index] : 0U; }

static size_t argument_stack_offset(const CinderIRFunction *function, size_t parameter) {
    size_t integer = 0U, floating = 0U, stacked = 0U;
    for (size_t p = 0U; p < parameter && p < function->params.len; ++p) {
        if (cinder_ir_floating(function->params.data[p]->type)) { if (floating++ >= 8U) ++stacked; }
        else if (integer++ >= 6U) ++stacked;
    }
    return 16U + stacked * 8U;
}

static void stack_store_rax(CinderMachineObject *object, size_t offset) {
    emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, 0x84U); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
}

static void stack_load_rax(CinderMachineObject *object, size_t offset) {
    emit8(object, 0x48U); emit8(object, 0x8BU); emit8(object, 0x84U); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
}

static void emit_call(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst) {
    size_t integer = 0U, floating = 0U, stacked = 0U;
    for (size_t a = 0U; a < inst->args.len; ++a) {
        bool fp = a < inst->arg_floats.len && inst->arg_floats.data[a];
        if (fp) { if (floating++ >= 8U) ++stacked; }
        else if (integer++ >= 6U) ++stacked;
    }
    size_t frame = ((stacked + inst->args.len) * 8U + 15U) & ~(size_t)15U;
    if (frame != 0U) { emit8(object, 0x48U); emit8(object, 0x81U); emit8(object, 0xECU); emit32(object, (uint32_t)frame); }
    /* Snapshot all arguments before assigning ABI registers. In particular,
     * XMM2-XMM7 may contain an argument needed after another ABI move. */
    for (size_t a = 0U; a < inst->args.len; ++a) {
        size_t offset = (stacked + a) * 8U;
        bool fp = a < inst->arg_floats.len && inst->arg_floats.data[a];
        if (fp) {
            load_float_value(object, function, allocation, inst->args.data[a], 0U);
            emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x11U); emit8(object, 0x84U); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
        } else { load_value_alloc(object, function, allocation, inst->args.data[a]); stack_store_rax(object, offset); }
    }
    integer = 0U; floating = 0U; size_t stack_index = 0U;
    for (size_t a = 0U; a < inst->args.len; ++a) {
        size_t offset = (stacked + a) * 8U;
        bool fp = a < inst->arg_floats.len && inst->arg_floats.data[a];
        bool overflow = fp ? floating >= 8U : integer >= 6U;
        if (overflow) {
            CinderType *argument_type = cinder_ir_value_type(function, inst->args.data[a]);
            if (fp && argument_type->kind == TYPE_FLOAT) {
                emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x10U); emit8(object, 0x84U); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
                emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U);
                emit8(object, 0x66U); emit8(object, 0x0FU); emit8(object, 0x7EU); emit8(object, 0xC0U);
            } else stack_load_rax(object, offset);
            stack_store_rax(object, stack_index++ * 8U);
        }
        else if (fp) {
            emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x10U);
            emit8(object, (uint8_t)(0x84U | ((unsigned)floating << 3U))); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
            CinderType *argument_type = cinder_ir_value_type(function, inst->args.data[a]);
            if (argument_type->kind == TYPE_FLOAT) { emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, (uint8_t)(0xC0U | ((unsigned)floating << 3U) | (unsigned)floating)); }
        } else { stack_load_rax(object, offset); emit_mov_reg_from_rax(object, abi_register((unsigned)integer)); }
        if (fp) ++floating; else ++integer;
    }
    emit8(object, 0xB0U); emit8(object, (uint8_t)(floating < 8U ? floating : 8U));
    emit8(object, 0xE8U); size_t fix_offset = object->text.len; emit32(object, 0U);
    CinderFixup fix = {fix_offset, cinder_strndup(inst->callee, strlen(inst->callee)), R_X86_64_PLT32, -4};
    cinder_vec_push((CinderVec *)&object->fixups, &fix);
    if (frame != 0U) { emit8(object, 0x48U); emit8(object, 0x81U); emit8(object, 0xC4U); emit32(object, (uint32_t)frame); }
    if (inst->dst != CINDER_INVALID_VALUE) {
        if (inst->floating_result) {
            if (inst->type->kind == TYPE_FLOAT) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
            store_float_value(object, function, allocation, inst->dst);
        }
        else store_value_alloc(object, function, allocation, inst->dst);
    }
}

static void symbol_displacement(CinderMachineObject *object, const char *name) {
    size_t offset = object->text.len; emit32(object, 0U);
    CinderFixup fix = {offset, cinder_strndup(name, strlen(name)), R_X86_64_PC32, -4};
    cinder_vec_push((CinderVec *)&object->fixups, &fix);
}

static void emit_global_load(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst) {
    size_t size = inst->type == NULL ? 8U : inst->type->size;
    if (size == 1U || size == 2U) {
        emit8(object, 0x48U); emit8(object, 0x0FU);
        emit8(object, inst->type->is_unsigned ? (size == 1U ? 0xB6U : 0xB7U) : (size == 1U ? 0xBEU : 0xBFU));
        emit8(object, 0x05U);
    } else if (size == 4U) {
        if (!inst->type->is_unsigned) { emit8(object, 0x48U); emit8(object, 0x63U); }
        else emit8(object, 0x8BU);
        emit8(object, 0x05U);
    } else { emit8(object, 0x48U); emit8(object, 0x8BU); emit8(object, 0x05U); }
    symbol_displacement(object, inst->callee);
    store_value_alloc(object, function, allocation, inst->dst);
}

static void emit_global_store(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst) {
    size_t size = inst->type == NULL ? 8U : inst->type->size;
    load_value_alloc(object, function, allocation, inst->left);
    if (size == 8U) emit8(object, 0x48U);
    if (size == 2U) emit8(object, 0x66U);
    emit8(object, size == 1U ? 0x88U : 0x89U); emit8(object, 0x05U);
    symbol_displacement(object, inst->callee);
}

void cinder_machine_init(CinderMachineObject *object) { object->text.data = NULL; object->text.len = 0U; object->text.cap = 0U; object->data.data = NULL; object->data.len = 0U; object->data.cap = 0U; object->rodata.data = NULL; object->rodata.len = 0U; object->rodata.cap = 0U; object->bss_size = 0U; object->fixups.data = NULL; object->fixups.len = 0U; object->fixups.cap = 0U; object->defined_symbols.data = NULL; object->defined_symbols.len = 0U; object->defined_symbols.cap = 0U; object->symbol_offsets.data = NULL; object->symbol_offsets.len = 0U; object->symbol_offsets.cap = 0U; object->symbol_sizes.data = NULL; object->symbol_sizes.len = 0U; object->symbol_sizes.cap = 0U; object->symbol_globals.data = NULL; object->symbol_globals.len = 0U; object->symbol_globals.cap = 0U; object->data_symbols.data = NULL; object->data_symbols.len = 0U; object->data_symbols.cap = 0U; object->literal_counter = 0U; object->frame_size = 0U; }

void cinder_machine_destroy(CinderMachineObject *object) { free(object->text.data); free(object->data.data); free(object->rodata.data); for (size_t i = 0U; i < object->fixups.len; ++i) free(object->fixups.data[i].symbol); free(object->fixups.data); for (size_t i = 0U; i < object->defined_symbols.len; ++i) free(object->defined_symbols.data[i]); free(object->defined_symbols.data); free(object->symbol_offsets.data); free(object->symbol_sizes.data); free(object->symbol_globals.data); for (size_t i = 0U; i < object->data_symbols.len; ++i) free(object->data_symbols.data[i].name); free(object->data_symbols.data); }

int cinder_lower_x86(const CinderIRFunction *function, CinderAllocation *allocation, CinderMachineObject *object, bool assembly, FILE *asm_out, CinderDiagnostics *diags) {
    (void)diags; (void)assembly; (void)asm_out;
    size_t frame = allocation->frame_size;
    size_t start = object->text.len;
    char *name = cinder_strndup(function->name, strlen(function->name));
    cinder_vec_push((CinderVec *)&object->defined_symbols, &name);
    cinder_vec_push((CinderVec *)&object->symbol_offsets, &start);
    cinder_vec_push((CinderVec *)&object->symbol_globals, &function->global);
    emit_prologue(object, frame);
    unsigned saved = 0U;
    for (unsigned bit = 0U; bit < 4U; ++bit) {
        if ((allocation->saved_gpr_mask & (1U << bit)) == 0U) continue;
        emit_mov_rax_from_reg(object, bit + 12U);
        emit_mov_mem_rax(object, -(int)((function->local_count + allocation->spill_slots + ++saved) * 8U));
    }
    size_t incoming_base = function->local_count + allocation->spill_slots + saved;
    for (unsigned a = 0U; a < 6U; ++a) {
        emit_mov_rax_from_reg(object, abi_register(a));
        emit_mov_mem_rax(object, -(int)((incoming_base + a + 1U) * 8U));
    }
    for (unsigned a = 0U; a < 8U; ++a) {
        emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x11U);
        emit8(object, (uint8_t)(0x85U | (a << 3U)));
        emit32(object, (uint32_t)(-(int)((incoming_base + 7U + a) * 8U)));
    }
    CINDER_VEC_TYPE(BranchFixup) branches = {NULL, 0U, 0U};
    size_t *labels = cinder_alloc((function->blocks.len == 0U ? 1U : function->blocks.len) * sizeof(*labels));
    for (size_t i = 0U; i < function->blocks.len; ++i) labels[i] = SIZE_MAX;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        const CinderIRBlock *block = &function->blocks.data[b]; labels[b] = object->text.len;
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderIRInst *inst = &block->instructions.data[i];
            switch (inst->op) {
                case IR_NOP: break;
                case IR_CONST: emit_mov_rax_imm(object, inst->integer); store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_FCONST: store_float_constant(object, function, allocation, inst->dst, inst->floating); break;
                case IR_GLOBAL_LOAD: emit_global_load(object, function, allocation, inst); break;
                case IR_GLOBAL_STORE: emit_global_store(object, function, allocation, inst); break;
                case IR_ARG:
                    if (inst->operator_code >= 0 && inst->operator_code < 6) emit_mov_rax_mem(object, -(int)((incoming_base + (size_t)inst->operator_code + 1U) * 8U)); else if (inst->operator_code >= 6) emit_mov_rax_mem(object, (int)argument_stack_offset(function, (size_t)inst->slot));
                    store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_FARG:
                    emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x10U); emit8(object, 0x85U);
                    emit32(object, inst->operator_code < 8 ? (uint32_t)(-(int)((incoming_base + 7U + (size_t)inst->operator_code) * 8U)) : (uint32_t)argument_stack_offset(function, (size_t)inst->slot));
                    if (inst->type->kind == TYPE_FLOAT) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
                    store_float_value(object, function, allocation, inst->dst); break;
                case IR_VA_ARG:
                    if (inst->slot >= 0 && inst->slot < 6) emit_mov_rax_mem(object, -(int)((incoming_base + (size_t)inst->slot + 1U) * 8U)); else if (inst->slot >= 6) emit_arg_from_stack(object, (unsigned)inst->slot);
                    store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_LOCAL_LOAD: case IR_PHI:
                    if (cinder_ir_floating(inst->type)) {
                        emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x10U); emit8(object, 0x85U); emit32(object, (uint32_t)local_offset(inst->slot));
                        store_float_value(object, function, allocation, inst->dst);
                    } else { emit_mov_rax_mem(object, local_offset(inst->slot)); store_value_alloc(object, function, allocation, inst->dst); }
                    break;
                case IR_LOCAL_STORE:
                    if (cinder_ir_floating(function->local_types.data[inst->slot])) {
                        load_float_value(object, function, allocation, inst->left, 0U);
                        emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x11U); emit8(object, 0x85U); emit32(object, (uint32_t)local_offset(inst->slot));
                    } else { load_value_alloc(object, function, allocation, inst->left); emit_mov_mem_rax(object, local_offset(inst->slot)); }
                    break;
                case IR_CONVERT: emit_conversion(object, function, allocation, inst); break;
                case IR_COPY:
                    if (cinder_ir_floating(inst->type)) { load_float_value(object, function, allocation, inst->left, 0U); store_float_value(object, function, allocation, inst->dst); }
                    else { load_value_alloc(object, function, allocation, inst->left); store_value_alloc(object, function, allocation, inst->dst); }
                    break;
                case IR_FNEG:
                    load_float_value(object, function, allocation, inst->left, 0U);
                    emit_mov_rax_imm(object, INT64_MIN);
                    emit8(object, 0x66U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0x6EU); emit8(object, 0xC8U);
                    emit8(object, 0x66U); emit8(object, 0x0FU); emit8(object, 0x57U); emit8(object, 0xC1U);
                    store_float_value(object, function, allocation, inst->dst); break;
                case IR_FADD: case IR_FSUB: case IR_FMUL: case IR_FDIV: emit_float_binary(object, function, allocation, inst); break;
                case IR_FCMP_EQ: case IR_FCMP_NE: case IR_FCMP_LT: case IR_FCMP_LE: case IR_FCMP_GT: case IR_FCMP_GE: emit_float_compare(object, function, allocation, inst); break;
                case IR_NEG: load_value_alloc(object, function, allocation, inst->left); emit8(object, 0x48U); emit8(object, 0xF7U); emit8(object, 0xD8U); store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_BIT_NOT: load_value_alloc(object, function, allocation, inst->left); emit8(object, 0x48U); emit8(object, 0xF7U); emit8(object, 0xD0U); store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_CALL: emit_call(object, function, allocation, inst); break;
                default: emit_binary(object, function, allocation, inst); break;
            }
        }
        switch (block->terminator.kind) {
            case TERM_RETURN:
                if (block->terminator.value != CINDER_INVALID_VALUE) { if (function->type->return_type->kind == TYPE_FLOAT || function->type->return_type->kind == TYPE_DOUBLE) load_float_value(object, function, allocation, block->terminator.value, 0U); else load_value_alloc(object, function, allocation, block->terminator.value); }
                if (function->type->return_type->kind == TYPE_FLOAT) { emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
                saved = 0U;
                for (unsigned bit = 0U; bit < 4U; ++bit) {
                    if ((allocation->saved_gpr_mask & (1U << bit)) == 0U) continue;
                    emit_mov_r10_mem(object, -(int)((function->local_count + allocation->spill_slots + ++saved) * 8U));
                    emit_mov_reg_reg(object, bit + 12U, 10U);
                }
                emit_epilogue(object); break;
            case TERM_JUMP: emit8(object, 0xE9U); { size_t offset = object->text.len; emit32(object, 0U); BranchFixup branch = {offset, block->terminator.target}; cinder_vec_push((CinderVec *)&branches, &branch); } break;
            case TERM_BRANCH:
                load_value_alloc(object, function, allocation, block->terminator.condition); emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xF8U); emit8(object, 0U); emit8(object, 0x0FU); emit8(object, 0x85U); { size_t offset = object->text.len; emit32(object, 0U); BranchFixup yes = {offset, block->terminator.yes}; cinder_vec_push((CinderVec *)&branches, &yes); } emit8(object, 0xE9U); { size_t offset = object->text.len; emit32(object, 0U); BranchFixup no = {offset, block->terminator.no}; cinder_vec_push((CinderVec *)&branches, &no); } break;
            case TERM_UNREACHABLE: emit8(object, 0x0FU); emit8(object, 0x0BU); break;
        }
    }
    for (size_t i = 0U; i < branches.len; ++i) { BranchFixup *branch = &branches.data[i]; if (branch->target >= function->blocks.len || labels[branch->target] == SIZE_MAX) continue; int64_t displacement = (int64_t)labels[branch->target] - (int64_t)(branch->offset + 4U); cinder_bytes_patch32(&object->text, branch->offset, (uint32_t)(int32_t)displacement); }
    free(labels); free(branches.data);
    size_t function_size = object->text.len - start;
    cinder_vec_push((CinderVec *)&object->symbol_sizes, &function_size);
    return 0;
}


enum { CINDER_DATA_SECTION = 1U, CINDER_RODATA_SECTION = 2U, CINDER_BSS_SECTION = 3U };

static size_t data_align_up(size_t value, size_t align) {
    if (align <= 1U) return value;
    size_t mask = align - 1U;
    return value > SIZE_MAX - mask ? SIZE_MAX : (value + mask) & ~mask;
}

static void bytes_align(CinderBytes *bytes, size_t align) {
    size_t target = data_align_up(bytes->len, align);
    while (bytes->len < target) cinder_bytes_put8(bytes, 0U);
}

static void append_integer(CinderBytes *bytes, int64_t value, size_t size) {
    for (size_t byte = 0U; byte < size; ++byte)
        cinder_bytes_put8(bytes, (uint8_t)(byte < 8U ? (uint64_t)value >> (byte * 8U) : 0U));
}

int cinder_lower_globals(const CinderIRModule *module, CinderMachineObject *object, CinderDiagnostics *diags) {
    for (size_t i = 0U; i < module->globals.len; ++i) {
        const CinderIRGlobal *global = &module->globals.data[i];
        if (global->is_extern) continue;
        if (global->type == NULL || !global->type->complete) { cinder_diag(diags, CINDER_ERROR, global->loc, "cannot emit incomplete global '%s'", global->name); continue; }
        CinderDataSymbol symbol;
        symbol.name = cinder_strndup(global->name, strlen(global->name));
        symbol.global = global->global;
        if (global->bytes != NULL) {
            bytes_align(&object->rodata, global->type->align);
            symbol.section_kind = CINDER_RODATA_SECTION;
            symbol.offset = object->rodata.len;
            size_t total = global->type->size;
            for (size_t b = 0U; b < global->byte_count && b < total; ++b) cinder_bytes_put8(&object->rodata, (uint8_t)global->bytes[b]);
            while (object->rodata.len < symbol.offset + total) cinder_bytes_put8(&object->rodata, 0U);
            symbol.size = total;
        } else if (global->has_initializer) {
            bytes_align(&object->data, global->type->align);
            symbol.section_kind = CINDER_DATA_SECTION;
            symbol.offset = object->data.len;
            append_integer(&object->data, global->integer, global->type->size);
            while (object->data.len < symbol.offset + global->type->size) cinder_bytes_put8(&object->data, 0U);
            symbol.size = global->type->size;
        } else {
            object->bss_size = data_align_up(object->bss_size, global->type->align);
            symbol.section_kind = CINDER_BSS_SECTION;
            symbol.offset = object->bss_size;
            object->bss_size += global->type->size;
            symbol.size = global->type->size;
        }
        cinder_vec_push((CinderVec *)&object->data_symbols, &symbol);
    }
    return diags->errors == 0U ? 0 : 1;
}
