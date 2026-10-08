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
static int value_offset(const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) { (void)function; return -(int)(allocation->local_bytes + ((size_t)value + 1U) * 8U); }
static int local_offset(const CinderAllocation *allocation, int slot) { return allocation->local_offsets.data[slot]; }
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

static void normalize_incoming_scalar(CinderMachineObject *object, const CinderType *type) {
    /* The ABI specifies only the low byte of a _Bool argument or result.
     * A C conversion, in contrast, must inspect the entire scalar value. */
    if (type != NULL && type->kind == TYPE_BOOL) {
        emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U);
    }
}

static void load_value_alloc(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) emit_mov_reg_reg(object, 0U, register_code(location->reg));
    else emit_mov_rax_mem(object, location != NULL ? location->stack_offset : value_offset(function, allocation, value));
}

static void store_value_alloc(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    normalize_integer(object, cinder_ir_value_type(function, value));
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) emit_mov_reg_reg(object, register_code(location->reg), 0U);
    else emit_mov_mem_rax(object, location != NULL ? location->stack_offset : value_offset(function, allocation, value));
}

static void load_value_to_r10(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) emit_mov_reg_reg(object, 10U, register_code(location->reg));
    else emit_mov_r10_mem(object, location != NULL ? location->stack_offset : value_offset(function, allocation, value));
}

static void load_value_to_rcx(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) emit_mov_reg_reg(object, 1U, register_code(location->reg));
    else emit_mov_rcx_mem(object, location != NULL ? location->stack_offset : value_offset(function, allocation, value));
}

static void emit_movsd_xmm_xmm(CinderMachineObject *object, unsigned destination, unsigned source) { emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x10U); emit8(object, (uint8_t)(0xC0U | ((destination & 7U) << 3U) | (source & 7U))); }

static void store_float_value(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) {
        emit_movsd_xmm_xmm(object, (unsigned)(location->reg - REG_XMM2) + 2U, 0U);
        return;
    }
    int offset = location != NULL ? location->stack_offset : value_offset(function, allocation, value);
    emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x11U); emit8(object, 0x85U); emit32(object, (uint32_t)offset);
}

static void load_float_value(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value, unsigned xmm) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) {
        emit_movsd_xmm_xmm(object, xmm, (unsigned)(location->reg - REG_XMM2) + 2U);
        return;
    }
    int offset = location != NULL ? location->stack_offset : value_offset(function, allocation, value);
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
    emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x10U); emit8(object, 0x05U); size_t fix_offset = object->text.len; emit32(object, 0U); CinderFixup fix = {fix_offset, cinder_strndup(name, (size_t)written), R_X86_64_PC32, -4, 0U}; cinder_vec_push((CinderVec *)&object->fixups, &fix);
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

static void stack_store_rax(CinderMachineObject *object, size_t offset) {
    emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, 0x84U); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
}

static void stack_load_rax(CinderMachineObject *object, size_t offset) {
    emit8(object, 0x48U); emit8(object, 0x8BU); emit8(object, 0x84U); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
}

static bool aggregate_type(const CinderType *type) {
    return type->kind == TYPE_STRUCT || type->kind == TYPE_UNION;
}

static void stack_adjust(CinderMachineObject *object, size_t size, bool allocate) {
    if (size == 0U) return;
    emit8(object, 0x48U); emit8(object, 0x81U); emit8(object, allocate ? 0xECU : 0xC4U); emit32(object, (uint32_t)size);
}

static void address_rbp(CinderMachineObject *object, unsigned reg, int offset) {
    emit8(object, reg < 8U ? 0x48U : 0x4CU); emit8(object, 0x8DU);
    emit8(object, (uint8_t)(0x85U | ((reg & 7U) << 3U))); emit32(object, (uint32_t)offset);
}

static void address_rsp(CinderMachineObject *object, unsigned reg, size_t offset) {
    emit8(object, reg < 8U ? 0x48U : 0x4CU); emit8(object, 0x8DU);
    emit8(object, (uint8_t)(0x84U | ((reg & 7U) << 3U))); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
}

static void xmm_rbp(CinderMachineObject *object, unsigned xmm, int offset, bool store) {
    emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, store ? 0x11U : 0x10U);
    emit8(object, (uint8_t)(0x85U | (xmm << 3U))); emit32(object, (uint32_t)offset);
}

static void xmm_rsp(CinderMachineObject *object, unsigned xmm, size_t offset, bool store) {
    emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, store ? 0x11U : 0x10U);
    emit8(object, (uint8_t)(0x84U | (xmm << 3U))); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
}

static void copy_bytes(CinderMachineObject *object, size_t size) {
    emit8(object, 0x48U); emit8(object, 0xB9U); emit64(object, size);
    emit8(object, 0xF3U); emit8(object, 0xA4U);
}

/* Pack only bytes belonging to the object. A three-byte aggregate at a page
 * boundary must not cause an eight-byte load into the next page. */
static void pack_eightbyte(CinderMachineObject *object, size_t offset, size_t size) {
    emit8(object, 0x31U); emit8(object, 0xC0U);
    size_t count = size - offset < 8U ? size - offset : 8U;
    for (size_t byte = 0U; byte < count; ++byte) {
        emit8(object, 0x45U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0x9AU); emit32(object, (uint32_t)(offset + byte));
        if (byte != 0U) { emit8(object, 0x49U); emit8(object, 0xC1U); emit8(object, 0xE3U); emit8(object, (uint8_t)(byte * 8U)); }
        emit8(object, 0x4CU); emit8(object, 0x09U); emit8(object, 0xD8U);
    }
}

static int emit_call(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst, CinderDiagnostics *diags) {
    CinderABIValue result;
    if (!cinder_abi_classify(inst->callee_type->return_type, &result)) { cinder_diag(diags, CINDER_FATAL, inst->loc, "unsupported ABI return layout"); return 1; }
    CinderABIState state = {result.memory ? 1U : 0U, 0U, 0U};
    CinderABIArgument *arguments = cinder_alloc((inst->args.len == 0U ? 1U : inst->args.len) * sizeof(*arguments));
    size_t *staging = cinder_alloc((inst->args.len == 0U ? 1U : inst->args.len) * sizeof(*staging));
    for (size_t a = 0U; a < inst->args.len; ++a) {
        const CinderType *type = inst->source_type != NULL ? inst->source_type->params.data[a].type : cinder_ir_value_type(function, inst->args.data[a]);
        if (!cinder_abi_place(type, &state, &arguments[a])) { cinder_diag(diags, CINDER_FATAL, inst->loc, "unsupported ABI argument layout"); free(staging); free(arguments); return 1; }
    }
    size_t frame = state.stack;
    for (size_t a = 0U; a < inst->args.len; ++a) {
        if (arguments[a].stack_offset != SIZE_MAX) staging[a] = arguments[a].stack_offset;
        else {
            size_t bytes = (arguments[a].value.size + 7U) & ~(size_t)7U;
            if (bytes > 64U * 1024U * 1024U - frame) { cinder_diag(diags, CINDER_FATAL, inst->loc, "ABI argument staging exceeds frame limit"); free(staging); free(arguments); return 1; }
            staging[a] = frame; frame += bytes;
        }
    }
    frame = (frame + 15U) & ~(size_t)15U;
    stack_adjust(object, frame, true);
    /* Stage every argument before assigning registers, including arguments
     * currently held in XMM2-XMM7. Register exhaustion rolls back an entire
     * aggregate, leaving both register banks available to later arguments. */
    for (size_t a = 0U; a < inst->args.len; ++a) {
        const CinderType *type = inst->source_type != NULL ? inst->source_type->params.data[a].type : cinder_ir_value_type(function, inst->args.data[a]);
        if (aggregate_type(type)) {
            if (arguments[a].stack_offset != SIZE_MAX) {
                load_value_alloc(object, function, allocation, inst->args.data[a]); emit_mov_reg_reg(object, 6U, 0U);
                address_rsp(object, 7U, staging[a]); copy_bytes(object, type->size);
            } else {
                load_value_to_r10(object, function, allocation, inst->args.data[a]);
                for (unsigned p = 0U; p < arguments[a].value.count; ++p) {
                    pack_eightbyte(object, p * 8U, type->size); stack_store_rax(object, staging[a] + p * 8U);
                }
            }
        } else if (cinder_ir_floating(type)) {
            load_float_value(object, function, allocation, inst->args.data[a], 0U);
            if (type->kind == TYPE_FLOAT) { emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
            xmm_rsp(object, 0U, staging[a], true);
        } else { load_value_alloc(object, function, allocation, inst->args.data[a]); stack_store_rax(object, staging[a]); }
    }
    if (result.memory) address_rbp(object, 7U, local_offset(allocation, inst->slot));
    for (size_t a = 0U; a < inst->args.len; ++a) {
        if (arguments[a].stack_offset != SIZE_MAX) continue;
        for (unsigned p = 0U; p < arguments[a].value.count; ++p) {
            if (arguments[a].value.classes[p] == ABI_INTEGER) { stack_load_rax(object, staging[a] + p * 8U); emit_mov_reg_from_rax(object, abi_register(arguments[a].registers[p])); }
            else if (arguments[a].value.classes[p] == ABI_SSE) xmm_rsp(object, arguments[a].registers[p], staging[a] + p * 8U, false);
        }
    }
    if (inst->callee == NULL) { load_value_alloc(object, function, allocation, inst->left); emit_mov_reg_reg(object, 11U, 0U); }
    emit8(object, 0xB0U); emit8(object, (uint8_t)state.sse);
    if (inst->callee == NULL) { emit8(object, 0x41U); emit8(object, 0xFFU); emit8(object, 0xD3U); }
    else {
        emit8(object, 0xE8U); size_t fix_offset = object->text.len; emit32(object, 0U);
        CinderFixup fix = {fix_offset, cinder_strndup(inst->callee, strlen(inst->callee)), R_X86_64_PLT32, -4, 0U};
        cinder_vec_push((CinderVec *)&object->fixups, &fix);
    }
    if (inst->noreturn_call) { emit8(object, 0x0FU); emit8(object, 0x0BU); }
    stack_adjust(object, frame, false);
    if (inst->dst != CINDER_INVALID_VALUE) {
        if (aggregate_type(inst->callee_type->return_type)) {
            if (!result.memory) {
                unsigned integer = 0U, floating = 0U;
                for (unsigned p = 0U; p < result.count; ++p) {
                    int offset = local_offset(allocation, inst->slot) + (int)(p * 8U);
                    if (result.classes[p] == ABI_INTEGER) { if (integer++ != 0U) emit_mov_reg_reg(object, 0U, 2U); emit_mov_mem_rax(object, offset); }
                    else if (result.classes[p] == ABI_SSE) xmm_rbp(object, floating++, offset, true);
                }
            }
            address_rbp(object, 0U, local_offset(allocation, inst->slot));
            store_value_alloc(object, function, allocation, inst->dst);
        } else if (inst->floating_result) {
            if (inst->type->kind == TYPE_FLOAT) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
            store_float_value(object, function, allocation, inst->dst);
        }
        else {
            normalize_incoming_scalar(object, inst->type);
            store_value_alloc(object, function, allocation, inst->dst);
        }
    }
    free(staging); free(arguments);
    return 0;
}

static void emit_aggregate_argument(CinderMachineObject *object, const CinderAllocation *allocation, const CinderIRInst *inst, const CinderABIArgument *argument, size_t incoming_base) {
    if (argument->stack_offset != SIZE_MAX) {
        address_rbp(object, 6U, (int)(16U + argument->stack_offset));
        address_rbp(object, 7U, local_offset(allocation, inst->slot)); copy_bytes(object, inst->type->size);
    } else {
        for (unsigned p = 0U; p < argument->value.count; ++p) {
            unsigned reg = argument->registers[p];
            if (argument->value.classes[p] == ABI_INTEGER) emit_mov_rax_mem(object, -(int)((incoming_base + reg + 1U) * 8U));
            else if (argument->value.classes[p] == ABI_SSE) emit_mov_rax_mem(object, -(int)((incoming_base + reg + 7U) * 8U));
            else continue;
            emit_mov_mem_rax(object, local_offset(allocation, inst->slot) + (int)(p * 8U));
        }
    }
}

static void emit_aggregate_return(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst, const CinderABIValue *result, size_t incoming_base) {
    if (result->memory) {
        emit_mov_rax_mem(object, -(int)((incoming_base + 1U) * 8U)); emit_mov_reg_reg(object, 7U, 0U);
        load_value_alloc(object, function, allocation, inst->left); emit_mov_reg_reg(object, 6U, 0U);
        copy_bytes(object, inst->type->size); emit_mov_rax_mem(object, -(int)((incoming_base + 1U) * 8U));
    } else {
        load_value_to_r10(object, function, allocation, inst->left); stack_adjust(object, 16U, true);
        for (unsigned p = 0U; p < result->count; ++p) { pack_eightbyte(object, p * 8U, inst->type->size); stack_store_rax(object, p * 8U); }
        unsigned floating = 0U, integer = 0U;
        for (unsigned p = 0U; p < result->count; ++p) if (result->classes[p] == ABI_SSE) xmm_rsp(object, floating++, p * 8U, false);
        for (unsigned p = 0U; p < result->count; ++p) if (result->classes[p] == ABI_INTEGER) ++integer;
        for (unsigned p = result->count; p-- > 0U;) if (result->classes[p] == ABI_INTEGER) { stack_load_rax(object, p * 8U); if (--integer != 0U) emit_mov_reg_reg(object, 2U, 0U); }
        stack_adjust(object, 16U, false);
    }
}

static void r10_store_rax(CinderMachineObject *object, unsigned offset) {
    emit8(object, 0x49U); emit8(object, 0x89U); emit8(object, 0x82U); emit32(object, offset);
}

static void emit_va_start(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst, const CinderABIState *named) {
    load_value_to_r10(object, function, allocation, inst->left);
    emit8(object, 0x41U); emit8(object, 0xC7U); emit8(object, 0x82U); emit32(object, 0U); emit32(object, named->gpr * 8U);
    emit8(object, 0x41U); emit8(object, 0xC7U); emit8(object, 0x82U); emit32(object, 4U); emit32(object, 48U + named->sse * 16U);
    address_rbp(object, 0U, (int)(16U + named->stack)); r10_store_rax(object, 8U);
    address_rbp(object, 0U, -(int)allocation->frame_size); r10_store_rax(object, 16U);
}

static void patch_here(CinderMachineObject *object, size_t offset) {
    cinder_bytes_patch32(&object->text, offset, (uint32_t)(object->text.len - offset - 4U));
}

static int emit_va_arg(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst, CinderDiagnostics *diags) {
    CinderABIValue value;
    if (!cinder_abi_classify(inst->source_type, &value)) { cinder_diag(diags, CINDER_FATAL, inst->loc, "unsupported va_arg layout"); return 1; }
    bool aggregate = aggregate_type(inst->source_type);
    unsigned gpr = 0U, sse = 0U;
    for (unsigned p = 0U; p < value.count; ++p) {
        if (value.classes[p] == ABI_INTEGER) ++gpr;
        else if (value.classes[p] == ABI_SSE) ++sse;
    }
    load_value_to_r10(object, function, allocation, inst->left);
    size_t gp_stack = SIZE_MAX, fp_stack = SIZE_MAX, done = SIZE_MAX;
    if (!value.memory) {
        if (gpr != 0U) {
            emit8(object, 0x41U); emit8(object, 0x8BU); emit8(object, 0x8AU); emit32(object, 0U);
            emit8(object, 0x81U); emit8(object, 0xF9U); emit32(object, 48U - gpr * 8U);
            emit8(object, 0x0FU); emit8(object, 0x87U); gp_stack = object->text.len; emit32(object, 0U);
        }
        if (sse != 0U) {
            emit8(object, 0x41U); emit8(object, 0x8BU); emit8(object, 0x92U); emit32(object, 4U);
            emit8(object, 0x81U); emit8(object, 0xFAU); emit32(object, 176U - sse * 16U);
            emit8(object, 0x0FU); emit8(object, 0x87U); fp_stack = object->text.len; emit32(object, 0U);
        }
        emit8(object, 0x4DU); emit8(object, 0x8BU); emit8(object, 0x9AU); emit32(object, 16U);
        if (aggregate) address_rbp(object, 7U, local_offset(allocation, inst->slot));
        for (unsigned p = 0U; p < value.count; ++p) {
            if (value.classes[p] != ABI_INTEGER && value.classes[p] != ABI_SSE) continue;
            emit8(object, 0x49U); emit8(object, 0x8BU); emit8(object, 0x04U); emit8(object, value.classes[p] == ABI_INTEGER ? 0x0BU : 0x13U);
            emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, value.classes[p] == ABI_INTEGER ? 0xC1U : 0xC2U); emit8(object, value.classes[p] == ABI_INTEGER ? 8U : 16U);
            if (aggregate) { emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, 0x87U); emit32(object, p * 8U); }
        }
        if (gpr != 0U) { emit8(object, 0x41U); emit8(object, 0x89U); emit8(object, 0x8AU); emit32(object, 0U); }
        if (sse != 0U) { emit8(object, 0x41U); emit8(object, 0x89U); emit8(object, 0x92U); emit32(object, 4U); }
        emit8(object, 0xE9U); done = object->text.len; emit32(object, 0U);
    }
    if (gp_stack != SIZE_MAX) patch_here(object, gp_stack);
    if (fp_stack != SIZE_MAX) patch_here(object, fp_stack);
    emit8(object, 0x49U); emit8(object, 0x8BU); emit8(object, 0xB2U); emit32(object, 8U);
    if (value.align > 8U) {
        emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xC6U); emit8(object, 15U);
        emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xE6U); emit8(object, 0xF0U);
    }
    if (aggregate) {
        address_rbp(object, 7U, local_offset(allocation, inst->slot)); copy_bytes(object, value.size);
        emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xC6U); emit8(object, 7U);
        emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xE6U); emit8(object, 0xF8U);
    } else {
        emit8(object, 0x48U); emit8(object, 0x8BU); emit8(object, 0x06U);
        emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xC6U); emit8(object, 8U);
    }
    emit8(object, 0x49U); emit8(object, 0x89U); emit8(object, 0xB2U); emit32(object, 8U);
    if (done != SIZE_MAX) patch_here(object, done);
    if (aggregate) { address_rbp(object, 0U, local_offset(allocation, inst->slot)); store_value_alloc(object, function, allocation, inst->dst); }
    else if (cinder_ir_floating(inst->type)) {
        emit8(object, 0x66U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0x6EU); emit8(object, 0xC0U);
        if (inst->type->kind == TYPE_FLOAT) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
        store_float_value(object, function, allocation, inst->dst);
    } else { normalize_incoming_scalar(object, inst->type); store_value_alloc(object, function, allocation, inst->dst); }
    return 0;
}

static void symbol_displacement(CinderMachineObject *object, const char *name) {
    size_t offset = object->text.len; emit32(object, 0U);
    CinderFixup fix = {offset, cinder_strndup(name, strlen(name)), R_X86_64_PC32, -4, 0U};
    cinder_vec_push((CinderVec *)&object->fixups, &fix);
}

/* Object storage uses its declared width. Register values use 64-bit integer
 * bits or canonical binary64, including values whose source type is float. */
static void object_load(CinderMachineObject *object, const CinderType *type, uint8_t operand, bool extended, int displacement) {
    size_t size = type->size;
    if (cinder_ir_floating(type)) {
        emit8(object, size == 4U ? 0xF3U : 0xF2U);
        if (extended) emit8(object, 0x41U);
        emit8(object, 0x0FU); emit8(object, 0x10U);
    } else if (size == 1U || size == 2U) {
        emit8(object, extended ? 0x49U : 0x48U); emit8(object, 0x0FU);
        emit8(object, type->is_unsigned ? (size == 1U ? 0xB6U : 0xB7U) : (size == 1U ? 0xBEU : 0xBFU));
    } else if (size == 4U && !type->is_unsigned) {
        emit8(object, extended ? 0x49U : 0x48U); emit8(object, 0x63U);
    } else {
        if (size == 8U) emit8(object, extended ? 0x49U : 0x48U);
        else if (extended) emit8(object, 0x41U);
        emit8(object, 0x8BU);
    }
    emit8(object, operand); emit32(object, (uint32_t)displacement);
    if (type->kind == TYPE_FLOAT) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
}

static void object_store(CinderMachineObject *object, const CinderType *type, uint8_t operand, bool extended, int displacement) {
    size_t size = type->size;
    if (cinder_ir_floating(type)) {
        if (size == 4U) { emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
        emit8(object, size == 4U ? 0xF3U : 0xF2U);
        if (extended) emit8(object, 0x41U);
        emit8(object, 0x0FU); emit8(object, 0x11U);
    } else {
        if (size == 2U) emit8(object, 0x66U);
        if (size == 8U) emit8(object, extended ? 0x49U : 0x48U);
        else if (extended) emit8(object, 0x41U);
        emit8(object, size == 1U ? 0x88U : 0x89U);
    }
    emit8(object, operand); emit32(object, (uint32_t)displacement);
}

static void bitfield_normalize(CinderMachineObject *object, const CinderType *type, unsigned width) {
    emit8(object, 0x48U); emit8(object, 0xB9U); emit64(object, (UINT64_C(1) << width) - 1U);
    emit8(object, 0x48U); emit8(object, 0x21U); emit8(object, 0xC8U);
    if (type->kind != TYPE_BOOL && !type->is_unsigned) {
        emit8(object, 0x48U); emit8(object, 0xC1U); emit8(object, 0xE0U); emit8(object, (uint8_t)(64U - width));
        emit8(object, 0x48U); emit8(object, 0xC1U); emit8(object, 0xF8U); emit8(object, (uint8_t)(64U - width));
    }
}

/* Byte accesses stay inside the bitfield's occupied bytes. In particular, a
 * uint container may also contain distinct ordinary char members. */
static void bitfield_load(CinderMachineObject *object, const CinderIRInst *inst) {
    CinderType byte = {0}; byte.kind = TYPE_CHAR; byte.size = 1U; byte.is_unsigned = true;
    unsigned begin = (unsigned)inst->operator_code / 8U;
    unsigned end = ((unsigned)inst->operator_code + (unsigned)inst->integer + 7U) / 8U;
    emit8(object, 0x45U); emit8(object, 0x31U); emit8(object, 0xDBU);
    for (unsigned i = begin; i < end; ++i) {
        object_load(object, &byte, 0x82U, true, (int)i);
        if (i != begin) { emit8(object, 0x48U); emit8(object, 0xC1U); emit8(object, 0xE0U); emit8(object, (uint8_t)((i - begin) * 8U)); }
        emit8(object, 0x49U); emit8(object, 0x09U); emit8(object, 0xC3U);
    }
    emit_mov_reg_reg(object, 0U, 11U);
    if ((unsigned)inst->operator_code % 8U != 0U) { emit8(object, 0x48U); emit8(object, 0xC1U); emit8(object, 0xE8U); emit8(object, (uint8_t)((unsigned)inst->operator_code % 8U)); }
    bitfield_normalize(object, inst->type, (unsigned)inst->integer);
}

static void bitfield_store(CinderMachineObject *object, const CinderIRInst *inst) {
    CinderType byte = {0}; byte.kind = TYPE_CHAR; byte.size = 1U; byte.is_unsigned = true;
    unsigned offset = (unsigned)inst->operator_code;
    unsigned begin = offset / 8U, end = (offset + (unsigned)inst->integer + 7U) / 8U;
    uint64_t mask = ((UINT64_C(1) << (unsigned)inst->integer) - 1U) << (offset % 8U);
    emit8(object, 0x48U); emit8(object, 0xB9U); emit64(object, (UINT64_C(1) << (unsigned)inst->integer) - 1U);
    emit8(object, 0x48U); emit8(object, 0x21U); emit8(object, 0xC8U);
    emit_mov_reg_reg(object, 11U, 0U);
    if (offset % 8U != 0U) { emit8(object, 0x49U); emit8(object, 0xC1U); emit8(object, 0xE3U); emit8(object, (uint8_t)(offset % 8U)); }
    for (unsigned i = begin; i < end; ++i) {
        unsigned shift = (i - begin) * 8U;
        uint8_t selected = (uint8_t)(mask >> shift);
        emit_mov_reg_reg(object, 0U, 11U);
        if (shift != 0U) { emit8(object, 0x48U); emit8(object, 0xC1U); emit8(object, 0xE8U); emit8(object, (uint8_t)shift); }
        emit8(object, 0x48U); emit8(object, 0x25U); emit32(object, selected);
        emit_mov_reg_reg(object, 1U, 0U);
        object_load(object, &byte, 0x82U, true, (int)i);
        emit8(object, 0x48U); emit8(object, 0x25U); emit32(object, (uint8_t)~selected);
        emit8(object, 0x48U); emit8(object, 0x09U); emit8(object, 0xC8U);
        object_store(object, &byte, 0x82U, true, (int)i);
    }
}

static void emit_global_load(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderIRInst *inst) {
    size_t size = inst->type == NULL ? 8U : inst->type->size;
    if (cinder_ir_floating(inst->type)) {
        emit8(object, size == 4U ? 0xF3U : 0xF2U); emit8(object, 0x0FU); emit8(object, 0x10U); emit8(object, 0x05U);
        symbol_displacement(object, inst->callee);
        if (size == 4U) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
        store_float_value(object, function, allocation, inst->dst); return;
    }
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
    if (cinder_ir_floating(inst->type)) {
        load_float_value(object, function, allocation, inst->left, 0U);
        if (size == 4U) { emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
        emit8(object, size == 4U ? 0xF3U : 0xF2U); emit8(object, 0x0FU); emit8(object, 0x11U); emit8(object, 0x05U);
        symbol_displacement(object, inst->callee); return;
    }
    load_value_alloc(object, function, allocation, inst->left);
    if (size == 8U) emit8(object, 0x48U);
    if (size == 2U) emit8(object, 0x66U);
    emit8(object, size == 1U ? 0x88U : 0x89U); emit8(object, 0x05U);
    symbol_displacement(object, inst->callee);
}

static CinderValueId storage_key(const CinderLocation *location) {
    return location->kind == LOC_REGISTER ? (CinderValueId)location->reg : (CinderValueId)((size_t)REG_NONE + 1U + (size_t)(-(int64_t)location->stack_offset / 8));
}

static int emit_phi_transfers(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderBlockId from, CinderBlockId to, CinderDiagnostics *diags) {
    const CinderIRBlock *target = &function->blocks.data[to];
    size_t count = 0U;
    for (size_t i = 0U; i < target->instructions.len; ++i) if (target->instructions.data[i].op == IR_PHI) ++count;
    if (count == 0U) return 0;
    CinderValueId *sources = cinder_alloc(count * sizeof(*sources));
    CinderValueId *destinations = cinder_alloc(count * sizeof(*destinations));
    CinderValueId *source_values = cinder_alloc(count * sizeof(*source_values));
    CinderValueId *destination_values = cinder_alloc(count * sizeof(*destination_values));
    size_t index = 0U;
    for (size_t i = 0U; i < target->instructions.len; ++i) {
        const CinderIRInst *phi = &target->instructions.data[i];
        if (phi->op != IR_PHI) continue;
        size_t incoming = SIZE_MAX;
        for (size_t p = 0U; p < phi->phi_blocks.len; ++p) if (phi->phi_blocks.data[p] == from) incoming = p;
        if (incoming == SIZE_MAX) { cinder_diag(diags, CINDER_FATAL, phi->loc, "phi transfer has no incoming edge"); break; }
        const CinderLocation *source = location_for(allocation, phi->args.data[incoming]);
        const CinderLocation *destination = location_for(allocation, phi->dst);
        if (source == NULL || destination == NULL) { cinder_diag(diags, CINDER_FATAL, phi->loc, "phi transfer has no physical location"); break; }
        sources[index] = storage_key(source); destinations[index] = storage_key(destination);
        source_values[index] = phi->args.data[incoming]; destination_values[index] = phi->dst; ++index;
    }
    CinderParallelCopyPlan plan; cinder_parallel_copy_init(&plan);
    if (diags->errors == 0U) (void)cinder_resolve_parallel_copies(sources, destinations, count, &plan, diags);
    bool temporary_float = false;
    for (size_t m = 0U; m < plan.moves.len && diags->errors == 0U; ++m) {
        CinderParallelCopy move = plan.moves.data[m];
        CinderValueId source = CINDER_INVALID_VALUE, destination = CINDER_INVALID_VALUE;
        for (size_t k = 0U; k < count; ++k) {
            if (sources[k] == move.source) source = source_values[k];
            if (destinations[k] == move.destination) destination = destination_values[k];
        }
        bool floating = source != CINDER_INVALID_VALUE ? cinder_ir_floating(cinder_ir_value_type(function, source)) : temporary_float;
        if (move.source == CINDER_INVALID_VALUE) {
            if (floating) emit_movsd_xmm_xmm(object, 0U, 1U);
            else emit_mov_reg_reg(object, 0U, 11U);
        } else if (source != CINDER_INVALID_VALUE) {
            if (floating) load_float_value(object, function, allocation, source, 0U);
            else load_value_alloc(object, function, allocation, source);
        } else { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "parallel-copy source is absent"); break; }
        if (move.destination == CINDER_INVALID_VALUE) {
            temporary_float = floating;
            if (floating) emit_movsd_xmm_xmm(object, 1U, 0U);
            else emit_mov_reg_reg(object, 11U, 0U);
        } else if (destination != CINDER_INVALID_VALUE) {
            if (floating) store_float_value(object, function, allocation, destination);
            else store_value_alloc(object, function, allocation, destination);
        } else { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "parallel-copy destination is absent"); break; }
    }
    cinder_parallel_copy_destroy(&plan);
    free(destination_values); free(source_values); free(destinations); free(sources);
    return diags->errors == 0U ? 0 : 1;
}

void cinder_machine_init(CinderMachineObject *object) { object->text.data = NULL; object->text.len = 0U; object->text.cap = 0U; object->data.data = NULL; object->data.len = 0U; object->data.cap = 0U; object->rodata.data = NULL; object->rodata.len = 0U; object->rodata.cap = 0U; object->bss_size = 0U; object->fixups.data = NULL; object->fixups.len = 0U; object->fixups.cap = 0U; object->defined_symbols.data = NULL; object->defined_symbols.len = 0U; object->defined_symbols.cap = 0U; object->symbol_offsets.data = NULL; object->symbol_offsets.len = 0U; object->symbol_offsets.cap = 0U; object->symbol_sizes.data = NULL; object->symbol_sizes.len = 0U; object->symbol_sizes.cap = 0U; object->symbol_globals.data = NULL; object->symbol_globals.len = 0U; object->symbol_globals.cap = 0U; object->data_symbols.data = NULL; object->data_symbols.len = 0U; object->data_symbols.cap = 0U; object->literal_counter = 0U; object->frame_size = 0U; }

void cinder_machine_destroy(CinderMachineObject *object) { free(object->text.data); free(object->data.data); free(object->rodata.data); for (size_t i = 0U; i < object->fixups.len; ++i) free(object->fixups.data[i].symbol); free(object->fixups.data); for (size_t i = 0U; i < object->defined_symbols.len; ++i) free(object->defined_symbols.data[i]); free(object->defined_symbols.data); free(object->symbol_offsets.data); free(object->symbol_sizes.data); free(object->symbol_globals.data); for (size_t i = 0U; i < object->data_symbols.len; ++i) free(object->data_symbols.data[i].name); free(object->data_symbols.data); }

int cinder_lower_x86(const CinderIRFunction *function, CinderAllocation *allocation, CinderMachineObject *object, bool assembly, FILE *asm_out, CinderDiagnostics *diags) {
    (void)assembly; (void)asm_out;
    CinderABIValue result;
    if (!cinder_abi_classify(function->type->return_type, &result)) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "unsupported function ABI return layout"); return 1; }
    CinderABIState state = {result.memory ? 1U : 0U, 0U, 0U};
    CinderABIArgument *parameters = cinder_alloc((function->params.len == 0U ? 1U : function->params.len) * sizeof(*parameters));
    for (size_t p = 0U; p < function->params.len; ++p) {
        if (!cinder_abi_place(function->params.data[p]->type, &state, &parameters[p])) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "unsupported function ABI parameter layout"); free(parameters); return 1; }
    }
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
        emit_mov_mem_rax(object, -(int)((allocation->local_bytes / 8U + allocation->spill_slots + ++saved) * 8U));
    }
    size_t incoming_base = allocation->local_bytes / 8U + allocation->spill_slots + saved;
    for (unsigned a = 0U; a < 6U; ++a) {
        emit_mov_rax_from_reg(object, abi_register(a));
        emit_mov_mem_rax(object, -(int)((incoming_base + a + 1U) * 8U));
    }
    for (unsigned a = 0U; a < 8U; ++a) {
        emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x11U);
        emit8(object, (uint8_t)(0x85U | (a << 3U)));
        emit32(object, (uint32_t)(-(int)((incoming_base + 7U + a) * 8U)));
    }
    if (function->type->variadic) {
        int area = -(int)allocation->frame_size;
        for (unsigned a = 0U; a < 6U; ++a) {
            emit_mov_rax_mem(object, -(int)((incoming_base + a + 1U) * 8U)); emit_mov_mem_rax(object, area + (int)(a * 8U));
        }
        for (unsigned a = 0U; a < 8U; ++a) {
            emit_mov_rax_mem(object, -(int)((incoming_base + a + 7U) * 8U)); emit_mov_mem_rax(object, area + (int)(48U + a * 16U));
            emit_mov_rax_imm(object, 0); emit_mov_mem_rax(object, area + (int)(56U + a * 16U));
        }
    }
    CINDER_VEC_TYPE(BranchFixup) branches = {NULL, 0U, 0U};
    size_t *labels = cinder_alloc((function->blocks.len == 0U ? 1U : function->blocks.len) * sizeof(*labels));
    for (size_t i = 0U; i < function->blocks.len; ++i) labels[i] = SIZE_MAX;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        const CinderIRBlock *block = &function->blocks.data[b]; labels[b] = object->text.len;
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderIRInst *inst = &block->instructions.data[i];
            switch (inst->op) {
                case IR_NOP: case IR_LOCAL_BEGIN: case IR_LOCAL_END: case IR_LOCAL_RESET: case IR_LOCAL_FREEZE: break;
                case IR_CONST: emit_mov_rax_imm(object, inst->integer); store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_FCONST: store_float_constant(object, function, allocation, inst->dst, inst->floating); break;
                case IR_GLOBAL_LOAD: emit_global_load(object, function, allocation, inst); break;
                case IR_GLOBAL_STORE: emit_global_store(object, function, allocation, inst); break;
                case IR_ARG:
                    if (parameters[inst->slot].stack_offset == SIZE_MAX) emit_mov_rax_mem(object, -(int)((incoming_base + parameters[inst->slot].registers[0] + 1U) * 8U));
                    else emit_mov_rax_mem(object, (int)(16U + parameters[inst->slot].stack_offset));
                    normalize_incoming_scalar(object, inst->type);
                    store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_FARG:
                    xmm_rbp(object, 0U, parameters[inst->slot].stack_offset == SIZE_MAX ? -(int)((incoming_base + parameters[inst->slot].registers[0] + 7U) * 8U) : (int)(16U + parameters[inst->slot].stack_offset), false);
                    if (inst->type->kind == TYPE_FLOAT) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
                    store_float_value(object, function, allocation, inst->dst); break;
                case IR_AGG_ARG: emit_aggregate_argument(object, allocation, inst, &parameters[inst->operator_code], incoming_base); break;
                case IR_AGG_RETURN: emit_aggregate_return(object, function, allocation, inst, &result, incoming_base); break;
                case IR_VA_START: emit_va_start(object, function, allocation, inst, &state); break;
                case IR_VA_COPY:
                    load_value_alloc(object, function, allocation, inst->left); emit_mov_reg_reg(object, 7U, 0U);
                    load_value_alloc(object, function, allocation, inst->right); emit_mov_reg_reg(object, 6U, 0U);
                    copy_bytes(object, 24U); break;
                case IR_VA_END: break;
                case IR_VA_ARG:
                    if (emit_va_arg(object, function, allocation, inst, diags) != 0) { free(parameters); free(labels); free(branches.data); return 1; }
                    break;
                case IR_UNDEF:
                    if (cinder_ir_floating(inst->type)) {
                        emit8(object, 0x66U); emit8(object, 0x0FU); emit8(object, 0x57U); emit8(object, 0xC0U);
                        store_float_value(object, function, allocation, inst->dst);
                    } else { emit_mov_rax_imm(object, 0); store_value_alloc(object, function, allocation, inst->dst); }
                    break;
                case IR_PHI: break;
                case IR_LOCAL_ADDRESS:
                    emit8(object, 0x48U); emit8(object, 0x8DU); emit8(object, 0x85U); emit32(object, (uint32_t)local_offset(allocation, inst->slot));
                    store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_FUNCTION_ADDRESS: case IR_GLOBAL_ADDRESS:
                    emit8(object, 0x48U); emit8(object, 0x8DU); emit8(object, 0x05U); symbol_displacement(object, inst->callee);
                    store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_ZERO_INIT:
                    load_value_alloc(object, function, allocation, inst->left); emit_mov_reg_reg(object, 7U, 0U);
                    emit8(object, 0x31U); emit8(object, 0xC0U);
                    emit8(object, 0x48U); emit8(object, 0xB9U); emit64(object, (uint64_t)inst->integer);
                    emit8(object, 0xF3U); emit8(object, 0xAAU); break;
                case IR_OBJECT_COPY: case IR_OBJECT_INIT:
                    load_value_alloc(object, function, allocation, inst->left); emit_mov_reg_reg(object, 7U, 0U);
                    load_value_alloc(object, function, allocation, inst->right); emit_mov_reg_reg(object, 6U, 0U);
                    emit8(object, 0x48U); emit8(object, 0xB9U); emit64(object, inst->type->size);
                    emit8(object, 0xF3U); emit8(object, 0xA4U); break;
                case IR_BIT_LOAD:
                    load_value_to_r10(object, function, allocation, inst->left);
                    bitfield_load(object, inst);
                    store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_BIT_CONVERT:
                    load_value_alloc(object, function, allocation, inst->left);
                    bitfield_normalize(object, inst->type, (unsigned)inst->integer);
                    store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_BIT_STORE: case IR_BIT_INIT:
                    load_value_to_r10(object, function, allocation, inst->left);
                    load_value_alloc(object, function, allocation, inst->right);
                    bitfield_store(object, inst); break;
                case IR_MEMORY_LOAD:
                    load_value_to_r10(object, function, allocation, inst->left);
                    object_load(object, inst->type, 0x82U, true, 0);
                    if (cinder_ir_floating(inst->type)) store_float_value(object, function, allocation, inst->dst);
                    else store_value_alloc(object, function, allocation, inst->dst);
                    break;
                case IR_MEMORY_STORE: case IR_MEMORY_INIT:
                    load_value_to_r10(object, function, allocation, inst->left);
                    if (cinder_ir_floating(inst->type)) load_float_value(object, function, allocation, inst->right, 0U);
                    else load_value_alloc(object, function, allocation, inst->right);
                    object_store(object, inst->type, 0x82U, true, 0); break;
                case IR_POINTER_OFFSET:
                    load_value_alloc(object, function, allocation, inst->right);
                    emit8(object, 0x49U); emit8(object, 0xBAU); emit64(object, (uint64_t)inst->integer);
                    emit8(object, 0x49U); emit8(object, 0x0FU); emit8(object, 0xAFU); emit8(object, 0xC2U);
                    load_value_to_r10(object, function, allocation, inst->left);
                    if (inst->operator_code < 0) { emit8(object, 0x48U); emit8(object, 0xF7U); emit8(object, 0xD8U); }
                    emit8(object, 0x4CU); emit8(object, 0x01U); emit8(object, 0xD0U); store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_POINTER_DIFF:
                    load_value_alloc(object, function, allocation, inst->left); load_value_to_r10(object, function, allocation, inst->right);
                    emit8(object, 0x4CU); emit8(object, 0x29U); emit8(object, 0xD0U); emit8(object, 0x48U); emit8(object, 0x99U);
                    emit8(object, 0x49U); emit8(object, 0xBAU); emit64(object, (uint64_t)inst->integer); emit8(object, 0x49U); emit8(object, 0xF7U); emit8(object, 0xFAU);
                    store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_POINTER_MEMBER:
                    load_value_alloc(object, function, allocation, inst->left); emit8(object, 0x48U); emit8(object, 0x05U); emit32(object, (uint32_t)inst->integer);
                    store_value_alloc(object, function, allocation, inst->dst); break;
                case IR_LOCAL_LOAD:
                    object_load(object, inst->type, 0x85U, false, local_offset(allocation, inst->slot));
                    if (cinder_ir_floating(inst->type)) store_float_value(object, function, allocation, inst->dst);
                    else store_value_alloc(object, function, allocation, inst->dst);
                    break;
                case IR_LOCAL_STORE: case IR_LOCAL_INIT:
                    if (cinder_ir_floating(inst->type)) load_float_value(object, function, allocation, inst->left, 0U);
                    else load_value_alloc(object, function, allocation, inst->left);
                    object_store(object, inst->type, 0x85U, false, local_offset(allocation, inst->slot));
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
                case IR_CALL:
                    if (emit_call(object, function, allocation, inst, diags) != 0) { free(parameters); free(labels); free(branches.data); return 1; }
                    break;
                default: emit_binary(object, function, allocation, inst); break;
            }
        }
        switch (block->terminator.kind) {
            case TERM_RETURN:
                if (function->is_noreturn) { emit8(object, 0x0FU); emit8(object, 0x0BU); break; }
                if (block->terminator.value != CINDER_INVALID_VALUE) { if (function->type->return_type->kind == TYPE_FLOAT || function->type->return_type->kind == TYPE_DOUBLE) load_float_value(object, function, allocation, block->terminator.value, 0U); else load_value_alloc(object, function, allocation, block->terminator.value); }
                if (function->type->return_type->kind == TYPE_FLOAT) { emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
                saved = 0U;
                for (unsigned bit = 0U; bit < 4U; ++bit) {
                    if ((allocation->saved_gpr_mask & (1U << bit)) == 0U) continue;
                    emit_mov_r10_mem(object, -(int)((allocation->local_bytes / 8U + allocation->spill_slots + ++saved) * 8U));
                    emit_mov_reg_reg(object, bit + 12U, 10U);
                }
                emit_epilogue(object); break;
            case TERM_JUMP:
                if (emit_phi_transfers(object, function, allocation, (CinderBlockId)b, block->terminator.target, diags) != 0) { free(parameters); free(labels); free(branches.data); return 1; }
                emit8(object, 0xE9U); { size_t offset = object->text.len; emit32(object, 0U); BranchFixup branch = {offset, block->terminator.target}; cinder_vec_push((CinderVec *)&branches, &branch); } break;
            case TERM_BRANCH:
                load_value_alloc(object, function, allocation, block->terminator.condition); emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xF8U); emit8(object, 0U); emit8(object, 0x0FU); emit8(object, 0x85U); { size_t offset = object->text.len; emit32(object, 0U); BranchFixup yes = {offset, block->terminator.yes}; cinder_vec_push((CinderVec *)&branches, &yes); } emit8(object, 0xE9U); { size_t offset = object->text.len; emit32(object, 0U); BranchFixup no = {offset, block->terminator.no}; cinder_vec_push((CinderVec *)&branches, &no); } break;
            case TERM_UNREACHABLE: emit8(object, 0x0FU); emit8(object, 0x0BU); break;
        }
    }
    for (size_t i = 0U; i < branches.len; ++i) { BranchFixup *branch = &branches.data[i]; if (branch->target >= function->blocks.len || labels[branch->target] == SIZE_MAX) continue; int64_t displacement = (int64_t)labels[branch->target] - (int64_t)(branch->offset + 4U); cinder_bytes_patch32(&object->text, branch->offset, (uint32_t)(int32_t)displacement); }
    free(parameters); free(labels); free(branches.data);
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
            CinderBytes *bytes = global->read_only ? &object->rodata : &object->data;
            bytes_align(bytes, global->alignment == 0U ? global->type->align : global->alignment);
            symbol.section_kind = global->read_only ? CINDER_RODATA_SECTION : CINDER_DATA_SECTION;
            symbol.offset = bytes->len;
            size_t total = global->type->size;
            for (size_t b = 0U; b < global->byte_count && b < total; ++b) cinder_bytes_put8(bytes, (uint8_t)global->bytes[b]);
            while (bytes->len < symbol.offset + total) cinder_bytes_put8(bytes, 0U);
            symbol.size = total;
        } else if (global->has_initializer || global->read_only) {
            CinderBytes *bytes = global->read_only ? &object->rodata : &object->data;
            bytes_align(bytes, global->alignment == 0U ? global->type->align : global->alignment);
            symbol.section_kind = global->read_only ? CINDER_RODATA_SECTION : CINDER_DATA_SECTION;
            symbol.offset = bytes->len;
            if (global->type->kind == TYPE_FLOAT) { float value = (float)global->floating; uint32_t bits; memcpy(&bits, &value, sizeof(bits)); cinder_bytes_put32(bytes, bits); }
            else if (global->type->kind == TYPE_DOUBLE) { uint64_t bits; memcpy(&bits, &global->floating, sizeof(bits)); cinder_bytes_put64(bytes, bits); }
            else append_integer(bytes, global->integer, global->type->size);
            while (bytes->len < symbol.offset + global->type->size) cinder_bytes_put8(bytes, 0U);
            symbol.size = global->type->size;
        } else {
            object->bss_size = data_align_up(object->bss_size, global->alignment == 0U ? global->type->align : global->alignment);
            symbol.section_kind = CINDER_BSS_SECTION;
            symbol.offset = object->bss_size;
            object->bss_size += global->type->size;
            symbol.size = global->type->size;
        }
        cinder_vec_push((CinderVec *)&object->data_symbols, &symbol);
        for (size_t a = 0U; a < global->addresses.len; ++a) {
            const CinderIRAddress *address = &global->addresses.data[a];
            CinderFixup fix = {symbol.offset + address->offset, cinder_strndup(address->symbol, strlen(address->symbol)), 1, address->addend, symbol.section_kind};
            cinder_vec_push((CinderVec *)&object->fixups, &fix);
        }
    }
    return diags->errors == 0U ? 0 : 1;
}
