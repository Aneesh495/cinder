#include "cinder.h"

#include <limits.h>
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
static void emit_mov_rax_from_reg(CinderMachineObject *object, unsigned reg) {
    if (reg < 8U) { emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | (reg << 3U))); }
    else { emit8(object, 0x4CU); emit8(object, 0x89U); emit8(object, (uint8_t)(0xC0U | ((reg - 8U) << 3U))); }
}
static void emit_epilogue(CinderMachineObject *object, CinderUnwind *unwind, const CinderMIRFrame *frame) {
    emit8(object, frame->epilogue[0]); cinder_unwind_leave(unwind, object);
    emit8(object, frame->epilogue[1]); cinder_unwind_epilogue_end(unwind, object);
}
static int value_offset(const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) { (void)function; return -(int)(allocation->local_bytes + ((size_t)value + 1U) * 8U); }
static int local_offset(const CinderAllocation *allocation, int slot) { return allocation->local_offsets.data[slot]; }

static void debug_point(CinderMachineObject *object, size_t symbol, size_t block, size_t instruction, int scope) {
    if (!object->debug_enabled && !object->track_source) return;
    if (object->debug_points.len != 0U && object->debug_points.data[object->debug_points.len - 1U].symbol == symbol)
        object->debug_points.data[object->debug_points.len - 1U].end = object->text.len;
    CinderDebugPoint point = {symbol, block, instruction, object->text.len, object->text.len, scope};
    cinder_vec_push((CinderVec *)&object->debug_points, &point);
}
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


static void load_value_alloc(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    const CinderLocation *location = location_for(allocation, value);
    if (location != NULL && location->kind == LOC_REGISTER) emit_mov_reg_reg(object, 0U, register_code(location->reg));
    else emit_mov_rax_mem(object, location != NULL ? location->stack_offset : value_offset(function, allocation, value));
}

static void store_value_alloc(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, CinderValueId value) {
    const CinderMIRValue *selected = &allocation->machine.values[value];
    for (unsigned n = 0U; n < selected->normalization_size; ++n) emit8(object, selected->normalization[n]);
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

static void emit_conversion(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRInst *selected) {
    const CinderIRInst *inst = &selected->operands;
    CinderMIRConversion conversion = selected->conversion;
    bool from_fp = conversion == MIR_CONVERT_FLOAT_PRECISION || conversion == MIR_CONVERT_FLOAT_SIGNED || conversion == MIR_CONVERT_FLOAT_UNSIGNED || conversion == MIR_CONVERT_FLOAT_BOOL;
    bool to_fp = conversion == MIR_CONVERT_FLOAT_PRECISION || conversion == MIR_CONVERT_SIGNED_FLOAT || conversion == MIR_CONVERT_UNSIGNED_FLOAT;
    if (from_fp) load_float_value(object, function, allocation, inst->left, 0U);
    else load_value_alloc(object, function, allocation, inst->left);
    if (!from_fp && to_fp) {
        bool single = selected->single_precision;
        if (conversion == MIR_CONVERT_UNSIGNED_FLOAT) {
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
        if (conversion == MIR_CONVERT_FLOAT_BOOL) {
            emit8(object, 0x66U); emit8(object, 0x0FU); emit8(object, 0x57U); emit8(object, 0xC9U);
            emit8(object, 0x66U); emit8(object, 0x0FU); emit8(object, 0x2EU); emit8(object, 0xC1U);
            emit8(object, 0x0FU); emit8(object, 0x95U); emit8(object, 0xC0U);
            emit8(object, 0x0FU); emit8(object, 0x9AU); emit8(object, 0xC2U);
            emit8(object, 0x08U); emit8(object, 0xD0U);
            emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U);
        } else if (conversion == MIR_CONVERT_FLOAT_UNSIGNED) {
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
        if (selected->single_precision) float32_round(object, 0U);
        store_float_value(object, function, allocation, inst->dst);
    } else store_value_alloc(object, function, allocation, inst->dst);
}
static void store_float_constant(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRInst *selected) {
    size_t offset = object->rodata.len;
    while ((object->rodata.len & 7U) != 0U) cinder_bytes_put8(&object->rodata, 0U);
    offset = object->rodata.len;
    cinder_bytes_put64(&object->rodata, selected->material.bits);
    char name[64]; int written = snprintf(name, sizeof(name), ".LCF%u", object->literal_counter++);
    if (written <= 0 || (size_t)written >= sizeof(name)) return;
    CinderDataSymbol symbol; symbol.name = cinder_strndup(name, (size_t)written); symbol.section_kind = 2U; symbol.offset = offset; symbol.size = 8U; symbol.global = false; cinder_vec_push((CinderVec *)&object->data_symbols, &symbol);
    cinder_bytes_append(&object->text, selected->encoding, selected->encoding_size); size_t fix_offset = object->text.len; emit32(object, 0U); CinderFixup fix = {fix_offset, cinder_strndup(name, (size_t)written), selected->material.relocation, selected->material.addend, 0U}; cinder_vec_push((CinderVec *)&object->fixups, &fix);
    store_float_value(object, function, allocation, selected->operands.dst);
}

static void emit_material(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRInst *selected) {
    const CinderMIRMaterial *material = &selected->material; const CinderIRInst *operands = &selected->operands;
    if (material->kind == MIR_MATERIAL_FLOAT) { store_float_constant(object, function, allocation, selected); return; }
    if (material->kind == MIR_MATERIAL_COPY_INTEGER || material->kind == MIR_MATERIAL_COPY_FLOAT || material->kind == MIR_MATERIAL_NEGATE_FLOAT) {
        if (material->bank == MIR_BANK_SSE) load_float_value(object, function, allocation, operands->left, 0U);
        else load_value_alloc(object, function, allocation, operands->left);
    }
    cinder_bytes_append(&object->text, selected->encoding, selected->encoding_size);
    if (material->bank == MIR_BANK_SSE) store_float_value(object, function, allocation, operands->dst);
    else store_value_alloc(object, function, allocation, operands->dst);
}

static void emit_bulk(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRInst *selected) {
    load_value_alloc(object, function, allocation, selected->operands.left); emit_mov_reg_reg(object, 7U, 0U);
    if (selected->bulk.kind == MIR_BULK_COPY) { load_value_alloc(object, function, allocation, selected->operands.right); emit_mov_reg_reg(object, 6U, 0U); }
    cinder_bytes_append(&object->text, selected->encoding, selected->encoding_size);
}

static void emit_float_binary(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRInst *selected) {
    const CinderIRInst *inst = &selected->operands;
    load_float_value(object, function, allocation, inst->left, 0U);
    load_float_value(object, function, allocation, inst->right, 1U);
    bool single = selected->single_precision;
    if (single) {
        emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U);
        emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC9U);
    }
    uint8_t opcode = selected->float_opcode;
    emit8(object, single ? 0xF3U : 0xF2U); emit8(object, 0x0FU); emit8(object, opcode); emit8(object, 0xC1U);
    if (single) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
    store_float_value(object, function, allocation, inst->dst);
}

static void emit_float_compare(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRInst *selected) {
    const CinderIRInst *inst = &selected->operands;
    load_float_value(object, function, allocation, inst->left, 0U); load_float_value(object, function, allocation, inst->right, 1U); emit8(object, 0x66U); emit8(object, 0x0FU); emit8(object, 0x2EU); emit8(object, 0xC1U);
    uint8_t condition = selected->condition_opcode;
    emit8(object, 0x0FU); emit8(object, condition); emit8(object, 0xC0U);
    if (selected->parity_opcode != 0U) {
        emit8(object, 0x0FU); emit8(object, selected->parity_opcode); emit8(object, 0xC2U);
        emit8(object, selected->parity_combine); emit8(object, 0xD0U);
    }
    emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0xC0U); store_value_alloc(object, function, allocation, inst->dst);
}


static void emit_binary(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRInst *selected) {
    const CinderIRInst *inst = &selected->operands;
    load_value_alloc(object, function, allocation, inst->left);
    load_value_to_r10(object, function, allocation, inst->right);
    if (selected->shift_count) load_value_to_rcx(object, function, allocation, inst->right);
    for (unsigned n = 0U; n < selected->encoding_size; ++n) emit8(object, selected->encoding[n]);
    store_value_alloc(object, function, allocation, inst->dst);
}

static unsigned abi_register(unsigned index) { static const unsigned regs[] = {7U, 6U, 2U, 1U, 8U, 9U}; return index < CINDER_ARRAY_LEN(regs) ? regs[index] : 0U; }

static void emit_protocol(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRProgram *program) {
    const CinderMIRFrame *frame = &allocation->frame;
    for (size_t s = 0U; s < program->steps.len; ++s) {
        const CinderMIRProgramStep *step = &program->steps.data[s];
        if (step->kind == MIR_STEP_LOAD_GPR) {
            if (step->reg == 10U) load_value_to_r10(object, function, allocation, step->value);
            else load_value_alloc(object, function, allocation, step->value);
        } else if (step->kind == MIR_STEP_LOAD_SSE) load_float_value(object, function, allocation, step->value, step->reg);
        else if (step->kind == MIR_STEP_STORE_GPR) store_value_alloc(object, function, allocation, step->value);
        else if (step->kind == MIR_STEP_STORE_SSE) store_float_value(object, function, allocation, step->value);
        else {
            size_t start = object->text.len; cinder_bytes_append(&object->text, step->bytes.data, step->bytes.len);
            for (size_t p = 0U; p < step->patches.len; ++p) {
                const CinderMIRFramePatch *patch = &step->patches.data[p]; int64_t offset;
                if (patch->kind == MIR_PATCH_LOCAL) offset = local_offset(allocation, patch->slot);
                else if (patch->kind == MIR_PATCH_INCOMING_GPR) offset = frame->incoming_gpr[patch->slot];
                else if (patch->kind == MIR_PATCH_INCOMING_SSE) offset = frame->incoming_sse[patch->slot];
                else offset = frame->variadic_area;
                cinder_bytes_patch32(&object->text, start + patch->offset, (uint32_t)(offset + patch->addend));
            }
            for (size_t f = 0U; f < step->fixups.len; ++f) {
                CinderFixup fix = step->fixups.data[f]; fix.offset += start; fix.symbol = cinder_strndup(fix.symbol, strlen(fix.symbol));
                cinder_vec_push((CinderVec *)&object->fixups, &fix);
            }
        }
    }
}

static void emit_address(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRInst *selected) {
    const CinderMIRAddress *address = &selected->address; const CinderIRInst *operands = &selected->operands;
    if (address->kind == MIR_ADDRESS_OFFSET) load_value_alloc(object, function, allocation, operands->right);
    else if (address->kind == MIR_ADDRESS_MEMBER || address->kind == MIR_ADDRESS_DIFFERENCE) load_value_alloc(object, function, allocation, operands->left);
    if (address->kind == MIR_ADDRESS_DIFFERENCE) load_value_to_r10(object, function, allocation, operands->right);
    cinder_bytes_append(&object->text, address->before, address->before_size);
    if (address->kind == MIR_ADDRESS_FRAME) emit32(object, (uint32_t)local_offset(allocation, address->frame_slot));
    else if (address->kind == MIR_ADDRESS_SYMBOL) {
        CinderFixup fix = {object->text.len, cinder_strndup(operands->callee, strlen(operands->callee)), address->relocation, address->addend, 0U};
        cinder_vec_push((CinderVec *)&object->fixups, &fix); emit32(object, 0U);
    } else if (address->kind == MIR_ADDRESS_OFFSET) load_value_to_r10(object, function, allocation, operands->left);
    cinder_bytes_append(&object->text, address->after, address->after_size);
    store_value_alloc(object, function, allocation, operands->dst);
}

/* Object storage uses its declared width. Register values use 64-bit integer
 * bits or canonical binary64, including values whose source type is float. */
static void access_prefix(CinderMachineObject *object, const CinderMIRAccess *access, bool extended, bool store) {
    const unsigned char *bytes = store ? (extended ? access->store_extended : access->store) : (extended ? access->load_extended : access->load);
    unsigned size = store ? (extended ? access->store_extended_size : access->store_size) : (extended ? access->load_extended_size : access->load_size);
    for (unsigned n = 0U; n < size; ++n) emit8(object, bytes[n]);
}

static void object_load(CinderMachineObject *object, const CinderMIRAccess *access, uint8_t operand, bool extended, int displacement) {
    access_prefix(object, access, extended, false);
    emit8(object, operand); emit32(object, (uint32_t)displacement);
    if (access->single) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
}

static void object_store(CinderMachineObject *object, const CinderMIRAccess *access, uint8_t operand, bool extended, int displacement) {
    if (access->single) { emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
    access_prefix(object, access, extended, true);
    emit8(object, operand); emit32(object, (uint32_t)displacement);
}

static void bitfield_normalize(CinderMachineObject *object, const CinderMIRBitfield *plan) {
    emit8(object, 0x48U); emit8(object, 0xB9U); emit64(object, plan->mask);
    emit8(object, 0x48U); emit8(object, 0x21U); emit8(object, 0xC8U);
    if (plan->signed_shift != 0U) {
        emit8(object, 0x48U); emit8(object, 0xC1U); emit8(object, 0xE0U); emit8(object, (uint8_t)plan->signed_shift);
        emit8(object, 0x48U); emit8(object, 0xC1U); emit8(object, 0xF8U); emit8(object, (uint8_t)plan->signed_shift);
    }
}

/* Byte accesses stay inside the bitfield's occupied bytes. In particular, a
 * uint container may also contain distinct ordinary char members. */
static void bitfield_load(CinderMachineObject *object, const CinderMIRInst *selected) {
    const CinderMIRBitfield *plan = &selected->bitfield;
    unsigned begin = plan->begin, end = plan->end;
    emit8(object, 0x45U); emit8(object, 0x31U); emit8(object, 0xDBU);
    for (unsigned i = begin; i < end; ++i) {
        object_load(object, &selected->access, 0x82U, true, (int)i);
        if (i != begin) { emit8(object, 0x48U); emit8(object, 0xC1U); emit8(object, 0xE0U); emit8(object, (uint8_t)((i - begin) * 8U)); }
        emit8(object, 0x49U); emit8(object, 0x09U); emit8(object, 0xC3U);
    }
    emit_mov_reg_reg(object, 0U, 11U);
    if (plan->shift != 0U) { emit8(object, 0x48U); emit8(object, 0xC1U); emit8(object, 0xE8U); emit8(object, (uint8_t)plan->shift); }
    bitfield_normalize(object, plan);
}

static void bitfield_store(CinderMachineObject *object, const CinderMIRInst *selected) {
    const CinderMIRBitfield *plan = &selected->bitfield;
    unsigned begin = plan->begin, end = plan->end;
    uint64_t mask = plan->positioned_mask;
    emit8(object, 0x48U); emit8(object, 0xB9U); emit64(object, plan->mask);
    emit8(object, 0x48U); emit8(object, 0x21U); emit8(object, 0xC8U);
    emit_mov_reg_reg(object, 11U, 0U);
    if (plan->shift != 0U) { emit8(object, 0x49U); emit8(object, 0xC1U); emit8(object, 0xE3U); emit8(object, (uint8_t)plan->shift); }
    for (unsigned i = begin; i < end; ++i) {
        unsigned shift = (i - begin) * 8U;
        uint8_t byte_mask = (uint8_t)(mask >> shift);
        emit_mov_reg_reg(object, 0U, 11U);
        if (shift != 0U) { emit8(object, 0x48U); emit8(object, 0xC1U); emit8(object, 0xE8U); emit8(object, (uint8_t)shift); }
        emit8(object, 0x48U); emit8(object, 0x25U); emit32(object, byte_mask);
        emit_mov_reg_reg(object, 1U, 0U);
        object_load(object, &selected->access, 0x82U, true, (int)i);
        emit8(object, 0x48U); emit8(object, 0x25U); emit32(object, (uint8_t)~byte_mask);
        emit8(object, 0x48U); emit8(object, 0x09U); emit8(object, 0xC8U);
        object_store(object, &selected->access, 0x82U, true, (int)i);
    }
}

static void emit_storage(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRInst *selected) {
    const CinderMIRStorage *storage = &selected->storage; const CinderMIRAccess *access = &selected->access;
    if (storage->kind == MIR_STORAGE_INDIRECT) load_value_to_r10(object, function, allocation, storage->address);
    if (storage->store) {
        if (access->floating) load_float_value(object, function, allocation, storage->value, 0U);
        else load_value_alloc(object, function, allocation, storage->value);
    }
    int displacement = storage->kind == MIR_STORAGE_FRAME ? local_offset(allocation, storage->frame_slot) : 0;
    if (storage->kind == MIR_STORAGE_SYMBOL) {
        if (storage->store && access->single) { emit8(object, 0xf2U); emit8(object, 0x0fU); emit8(object, 0x5aU); emit8(object, 0xc0U); }
        access_prefix(object, access, storage->extended, storage->store); emit8(object, storage->modrm);
        const char *name = selected->operands.callee;
        CinderFixup fix = {object->text.len, cinder_strndup(name, strlen(name)), storage->relocation, storage->addend, 0U};
        cinder_vec_push((CinderVec *)&object->fixups, &fix); emit32(object, 0U);
        if (!storage->store && access->single) { emit8(object, 0xf3U); emit8(object, 0x0fU); emit8(object, 0x5aU); emit8(object, 0xc0U); }
    } else if (storage->store) object_store(object, access, storage->modrm, storage->extended, displacement);
    else object_load(object, access, storage->modrm, storage->extended, displacement);
    if (!storage->store) {
        if (access->floating) store_float_value(object, function, allocation, storage->value);
        else store_value_alloc(object, function, allocation, storage->value);
    }
}

static void emit_bitfield(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRInst *selected) {
    const CinderIRInst *operands = &selected->operands;
    if (selected->bitfield.kind == MIR_BITFIELD_NORMALIZE) {
        load_value_alloc(object, function, allocation, operands->left); bitfield_normalize(object, &selected->bitfield);
    } else {
        load_value_to_r10(object, function, allocation, operands->left);
        if (selected->bitfield.kind == MIR_BITFIELD_STORE) { load_value_alloc(object, function, allocation, operands->right); bitfield_store(object, selected); return; }
        bitfield_load(object, selected);
    }
    store_value_alloc(object, function, allocation, operands->dst);
}

static void emit_phi_transfers(CinderMachineObject *object, const CinderIRFunction *function, const CinderAllocation *allocation, const CinderMIRBlock *block) {
    for (size_t m = 0U; m < block->transfers.len; ++m) {
        const CinderMIREdgeMove *move = &block->transfers.data[m]; bool floating = move->bank == MIR_BANK_SSE;
        if (move->source == CINDER_INVALID_VALUE) {
            if (floating) emit_movsd_xmm_xmm(object, 0U, 1U);
            else emit_mov_reg_reg(object, 0U, 11U);
        } else if (floating) load_float_value(object, function, allocation, move->source, 0U);
        else load_value_alloc(object, function, allocation, move->source);
        if (move->destination == CINDER_INVALID_VALUE) {
            if (floating) emit_movsd_xmm_xmm(object, 1U, 0U);
            else emit_mov_reg_reg(object, 11U, 0U);
        } else if (floating) store_float_value(object, function, allocation, move->destination);
        else store_value_alloc(object, function, allocation, move->destination);
    }
}

void cinder_machine_init(CinderMachineObject *object) { memset(object, 0, sizeof(*object)); }

void cinder_machine_destroy(CinderMachineObject *object) { free(object->text.data); free(object->data.data); free(object->rodata.data); for (size_t i = 0U; i < object->fixups.len; ++i) free(object->fixups.data[i].symbol); free(object->fixups.data); for (size_t i = 0U; i < object->defined_symbols.len; ++i) free(object->defined_symbols.data[i]); free(object->defined_symbols.data); free(object->symbol_offsets.data); free(object->symbol_sizes.data); free(object->symbol_globals.data); for (size_t i = 0U; i < object->data_symbols.len; ++i) free(object->data_symbols.data[i].name); free(object->data_symbols.data); for (size_t i = 0U; i < object->debug_sections.len; ++i) { free(object->debug_sections.data[i].name); free(object->debug_sections.data[i].bytes.data); } free(object->debug_sections.data); free(object->debug_rows.data); free(object->debug_functions.data); free(object->debug_parameters.data); free(object->debug_points.data); free(object->debug_local_homes.data); }

int cinder_lower_x86(const CinderIRFunction *function, CinderAllocation *allocation, CinderMachineObject *object, bool assembly, FILE *asm_out, CinderDiagnostics *diags) {
    (void)assembly; (void)asm_out;
    const CinderMIRFunction *machine = &allocation->machine;
    if (machine->source != function || cinder_verify_selected_mir(machine, diags) != 0 || cinder_verify_frame(allocation, diags) != 0 || cinder_verify_edge_copies(allocation, diags) != 0) return 1;
    const CinderMIRCallPlan *signature = machine->signature;
    if (signature == NULL || signature->argument_count != function->params.len) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "function has no selected incoming ABI plan"); return 1; }
    const CinderABIArgument *parameters = signature->arguments;
    const CinderMIRFrame *frame = &allocation->frame;
    size_t start = object->text.len;
    char *name = cinder_strndup(function->name, strlen(function->name));
    cinder_vec_push((CinderVec *)&object->defined_symbols, &name);
    cinder_vec_push((CinderVec *)&object->symbol_offsets, &start);
    cinder_vec_push((CinderVec *)&object->symbol_globals, &function->global);
    CinderDebugFunction debug = {object->defined_symbols.len - 1U, function->loc, start, object->debug_rows.len, object->debug_rows.len, object->debug_points.len, object->debug_points.len};
    if (object->debug_enabled) cinder_debug_row(object, function->loc);
    CinderUnwind unwind; cinder_unwind_begin(&unwind, object, function->name, start);
    cinder_bytes_append(&object->text, frame->prologue, frame->prologue_size);
    cinder_unwind_prologue(&unwind, object);
    for (unsigned n = 0U; n < frame->saved_count; ++n) {
        const CinderMIRPreserved *saved = &frame->saved[n];
        cinder_bytes_append(&object->text, saved->save, sizeof(saved->save));
        cinder_unwind_saved(&unwind, object, saved->reg, saved->offset);
    }
    size_t incoming_base = frame->incoming_base;
    for (unsigned a = 0U; a < 6U; ++a) {
        emit_mov_rax_from_reg(object, abi_register(a));
        emit_mov_mem_rax(object, frame->incoming_gpr[a]);
    }
    for (unsigned a = 0U; a < 8U; ++a) {
        emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x11U);
        emit8(object, (uint8_t)(0x85U | (a << 3U)));
        emit32(object, (uint32_t)frame->incoming_sse[a]);
    }
    if (frame->variadic_area != 0) {
        int area = frame->variadic_area;
        for (unsigned a = 0U; a < 6U; ++a) {
            emit_mov_rax_mem(object, -(int)((incoming_base + a + 1U) * 8U)); emit_mov_mem_rax(object, area + (int)(a * 8U));
        }
        for (unsigned a = 0U; a < 8U; ++a) {
            emit_mov_rax_mem(object, -(int)((incoming_base + a + 7U) * 8U)); emit_mov_mem_rax(object, area + (int)(48U + a * 16U));
            emit_mov_rax_imm(object, 0); emit_mov_mem_rax(object, area + (int)(56U + a * 16U));
        }
    }
    debug.prologue_end = object->text.len;
    if (object->debug_enabled) for (size_t p = 0U; p < function->params.len; ++p) {
        const CinderIRParameter *parameter = function->params.data[p];
        const CinderABIArgument *argument = &parameters[p];
        if (!parameter->unchanged || parameter->type->kind == TYPE_STRUCT || parameter->type->kind == TYPE_UNION ||
            argument->value.count != 1U) continue;
        CinderDebugParameter location = {debug.symbol, p, -1, 0};
        if (argument->stack_offset != SIZE_MAX) location.cfa_offset = (int)argument->stack_offset;
        else {
            unsigned reg = argument->registers[0];
            bool floating = argument->value.classes[0] == ABI_SSE;
            const unsigned dwarf_gpr[] = {5U, 4U, 1U, 2U, 8U, 9U};
            location.reg = floating ? (int)(17U + reg) : (int)dwarf_gpr[reg];
            location.cfa_offset = -(int)((incoming_base + reg + (floating ? 7U : 1U)) * 8U) - 16;
        }
        cinder_vec_push((CinderVec *)&object->debug_parameters, &location);
    }
    if (object->debug_enabled) for (size_t l = 0U; l < function->debug_locals.len; ++l) {
        const CinderIRLocal *local = &function->debug_locals.data[l]; const CinderType *type = local->type;
        if (type->size == 0U || type->size > 8U || type->kind == TYPE_ARRAY || type->kind == TYPE_STRUCT || type->kind == TYPE_UNION) continue;
        bool addressed = false;
        for (size_t b = 0U; b < function->blocks.len; ++b)
            for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
                const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
                if (inst->op == IR_LOCAL_ADDRESS && inst->slot == local->slot) addressed = true;
            }
        if (!addressed) {
            CinderDebugLocalHome home = {debug.symbol, l, local_offset(allocation, local->slot) - 16};
            cinder_vec_push((CinderVec *)&object->debug_local_homes, &home);
        }
    }
    if (object->debug_enabled) cinder_debug_row(object, function->loc);
    CINDER_VEC_TYPE(BranchFixup) branches = {NULL, 0U, 0U};
    size_t *labels = cinder_alloc((function->blocks.len == 0U ? 1U : function->blocks.len) * sizeof(*labels));
    for (size_t i = 0U; i < function->blocks.len; ++i) labels[i] = SIZE_MAX;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        const CinderMIRBlock *block = &machine->blocks.data[b]; labels[b] = object->text.len;
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderMIRInst *selected = &block->instructions.data[i];
            const CinderIRInst *inst = &selected->operands;
            debug_point(object, debug.symbol, b, i, inst->debug_scope);
            if (object->debug_enabled && inst->op != IR_NOP && inst->op != IR_PHI && inst->op != IR_LOCAL_BEGIN && inst->op != IR_LOCAL_END && inst->op != IR_LOCAL_RESET && inst->op != IR_LOCAL_FREEZE) cinder_debug_row(object, inst->loc);
            if (selected->kind == MIR_INTEGER_ALU) { emit_binary(object, function, allocation, selected); continue; }
            if (selected->kind == MIR_FLOAT_ALU) { emit_float_binary(object, function, allocation, selected); continue; }
            if (selected->kind == MIR_FLOAT_COMPARE) { emit_float_compare(object, function, allocation, selected); continue; }
            if (selected->kind == MIR_ADDRESS) { emit_address(object, function, allocation, selected); continue; }
            if (selected->kind == MIR_MARKER) continue;
            if (selected->kind == MIR_MATERIAL) { emit_material(object, function, allocation, selected); continue; }
            if (selected->kind == MIR_BULK) { emit_bulk(object, function, allocation, selected); continue; }
            if (selected->kind == MIR_INTEGER_UNARY) {
                load_value_alloc(object, function, allocation, inst->left);
                for (unsigned n = 0U; n < selected->encoding_size; ++n) emit8(object, selected->encoding[n]);
                store_value_alloc(object, function, allocation, inst->dst); continue;
            }
            if (selected->kind == MIR_MEMORY) { emit_storage(object, function, allocation, selected); continue; }
            if (selected->kind == MIR_BITFIELD) { emit_bitfield(object, function, allocation, selected); continue; }
            if (selected->kind == MIR_CONVERSION) { emit_conversion(object, function, allocation, selected); continue; }
            if (selected->kind == MIR_PROTOCOL) { emit_protocol(object, function, allocation, selected->program); continue; }
            cinder_diag(diags, CINDER_FATAL, inst->loc, "unselected machine instruction"); free(labels); free(branches.data); return 1;
        }
        debug_point(object, debug.symbol, b, SIZE_MAX, block->terminator.debug_scope);
        if (object->debug_enabled) cinder_debug_row(object, block->terminator.loc);
        const CinderMIRControl *control = &block->control;
        if (control->value != CINDER_INVALID_VALUE) {
            if (control->bank == MIR_BANK_SSE) load_float_value(object, function, allocation, control->value, 0U);
            else load_value_alloc(object, function, allocation, control->value);
        }
        cinder_bytes_append(&object->text, control->prefix, control->prefix_size);
        if (control->kind == MIR_CONTROL_RETURN) {
            cinder_unwind_epilogue_begin(&unwind, object);
            for (unsigned n = 0U; n < frame->saved_count; ++n) {
                const CinderMIRPreserved *saved = &frame->saved[n];
                cinder_bytes_append(&object->text, saved->restore, sizeof(saved->restore));
                cinder_unwind_restored(&unwind, object, saved->reg);
            }
            emit_epilogue(object, &unwind, frame);
        } else {
            if (control->kind == MIR_CONTROL_JUMP) emit_phi_transfers(object, function, allocation, block);
            for (unsigned edge = 0U; edge < control->edge_count; ++edge) {
                cinder_bytes_append(&object->text, control->branches[edge], control->branch_sizes[edge]);
                BranchFixup branch = {object->text.len, control->targets[edge]}; emit32(object, 0U);
                cinder_vec_push((CinderVec *)&branches, &branch);
            }
        }
    }
    for (size_t i = 0U; i < branches.len; ++i) {
        BranchFixup *branch = &branches.data[i];
        if ((size_t)branch->target >= function->blocks.len || labels[branch->target] == SIZE_MAX || branch->offset > (size_t)INT64_MAX - 4U || labels[branch->target] > INT64_MAX) {
            cinder_diag(diags, CINDER_FATAL, function->loc, "selected branch has no encodable target"); break;
        }
        int64_t displacement = (int64_t)labels[branch->target] - (int64_t)(branch->offset + 4U);
        if (displacement < INT32_MIN || displacement > INT32_MAX) { cinder_diag(diags, CINDER_FATAL, function->loc, "branch exceeds the signed rel32 target profile"); break; }
        cinder_bytes_patch32(&object->text, branch->offset, (uint32_t)(int32_t)displacement);
    }
    free(labels); free(branches.data);
    if (diags->errors != 0U) return 1;
    size_t function_size = object->text.len - start;
    cinder_vec_push((CinderVec *)&object->symbol_sizes, &function_size);
    if (object->debug_enabled || object->track_source) object->debug_points.data[object->debug_points.len - 1U].end = object->text.len;
    if (object->debug_enabled) {
        debug.end_point = object->debug_points.len; debug.end_row = object->debug_rows.len;
        cinder_vec_push((CinderVec *)&object->debug_functions, &debug);
    }
    return cinder_unwind_finish(&unwind, object, diags);
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
