#include "cinder.h"
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define R_X86_64_PLT32 4

typedef struct {
    CinderMIRProgram *program;
    CinderBytes text;
    CINDER_VEC_TYPE(CinderMIRFramePatch) patches;
    CINDER_VEC_TYPE(CinderFixup) fixups;
} ProgramBuilder;

static void flush(ProgramBuilder *object) {
    if (object->text.len == 0U) return;
    CinderMIRProgramStep step; memset(&step, 0, sizeof(step));
    step.kind = MIR_STEP_BYTES; step.value = CINDER_INVALID_VALUE; step.bytes = object->text;
    step.patches.data = object->patches.data; step.patches.len = object->patches.len; step.patches.cap = object->patches.cap;
    step.fixups.data = object->fixups.data; step.fixups.len = object->fixups.len; step.fixups.cap = object->fixups.cap;
    cinder_vec_push((CinderVec *)&object->program->steps, &step);
    memset(&object->text, 0, sizeof(object->text)); memset(&object->patches, 0, sizeof(object->patches)); memset(&object->fixups, 0, sizeof(object->fixups));
}

static void transfer(ProgramBuilder *object, CinderMIRProgramStepKind kind, CinderValueId value, unsigned reg) {
    flush(object); CinderMIRProgramStep step; memset(&step, 0, sizeof(step)); step.kind = kind; step.value = value; step.reg = reg;
    cinder_vec_push((CinderVec *)&object->program->steps, &step);
}

static void emit8(ProgramBuilder *object, uint8_t value) { cinder_bytes_put8(&object->text, value); }
static void emit32(ProgramBuilder *object, uint32_t value) { cinder_bytes_put32(&object->text, value); }
static void emit64(ProgramBuilder *object, uint64_t value) { cinder_bytes_put64(&object->text, value); }
static void frame_patch(ProgramBuilder *object, CinderMIRFramePatchKind kind, int slot, int64_t addend) {
    CinderMIRFramePatch patch; memset(&patch, 0, sizeof(patch)); patch.kind = kind; patch.offset = object->text.len; patch.slot = slot; patch.addend = addend;
    cinder_vec_push((CinderVec *)&object->patches, &patch); emit32(object, 0U);
}
static void frame_address(ProgramBuilder *object, unsigned reg, int slot) {
    emit8(object, reg < 8U ? 0x48U : 0x4cU); emit8(object, 0x8dU); emit8(object, (uint8_t)(0x85U | ((reg & 7U) << 3U)));
    frame_patch(object, MIR_PATCH_LOCAL, slot, 0);
}
static void local_store(ProgramBuilder *object, int slot, unsigned part) {
    emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, 0x85U); frame_patch(object, MIR_PATCH_LOCAL, slot, (int64_t)part * 8);
}
static void local_xmm_store(ProgramBuilder *object, unsigned xmm, int slot, unsigned part) {
    emit8(object, 0xf2U); emit8(object, 0x0fU); emit8(object, 0x11U); emit8(object, (uint8_t)(0x85U | (xmm << 3U)));
    frame_patch(object, MIR_PATCH_LOCAL, slot, (int64_t)part * 8);
}
static void incoming_load(ProgramBuilder *object, unsigned reg, bool floating) {
    emit8(object, 0x48U); emit8(object, 0x8bU); emit8(object, 0x85U);
    frame_patch(object, floating ? MIR_PATCH_INCOMING_SSE : MIR_PATCH_INCOMING_GPR, (int)reg, 0);
}
static void load_value_alloc(ProgramBuilder *object, CinderValueId value) {
    transfer(object, MIR_STEP_LOAD_GPR, value, 0U);
}
static void load_value_to_r10(ProgramBuilder *object, CinderValueId value) {
    transfer(object, MIR_STEP_LOAD_GPR, value, 10U);
}
static void store_value_alloc(ProgramBuilder *object, CinderValueId value) {
    transfer(object, MIR_STEP_STORE_GPR, value, 0U);
}
static void load_float_value(ProgramBuilder *object, CinderValueId value, unsigned xmm) {
    transfer(object, MIR_STEP_LOAD_SSE, value, xmm);
}
static void store_float_value(ProgramBuilder *object, CinderValueId value) {
    transfer(object, MIR_STEP_STORE_SSE, value, 0U);
}
static void emit_mov_reg_reg(ProgramBuilder *object, unsigned destination, unsigned source) {
    unsigned rex = 0x48U | (source >= 8U ? 4U : 0U) | (destination >= 8U ? 1U : 0U);
    emit8(object, (uint8_t)rex); emit8(object, 0x89U); emit8(object, (uint8_t)(0xc0U | ((source & 7U) << 3U) | (destination & 7U)));
}
static void emit_mov_reg_from_rax(ProgramBuilder *object, unsigned reg) { emit_mov_reg_reg(object, reg, 0U); }
static void emit_mov_rax_mem(ProgramBuilder *object, int offset) { emit8(object, 0x48U); emit8(object, 0x8bU); emit8(object, 0x85U); emit32(object, (uint32_t)offset); }
static void normalize_incoming_scalar(ProgramBuilder *object, bool incoming_bool) {
    if (incoming_bool) { emit8(object, 0x0fU); emit8(object, 0xb6U); emit8(object, 0xc0U); }
}

static unsigned abi_register(unsigned index) { static const unsigned regs[] = {7U, 6U, 2U, 1U, 8U, 9U}; return index < CINDER_ARRAY_LEN(regs) ? regs[index] : 0U; }

static void stack_store_rax(ProgramBuilder *object, size_t offset) {
    emit8(object, 0x48U); emit8(object, 0x89U); emit8(object, 0x84U); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
}

static void stack_load_rax(ProgramBuilder *object, size_t offset) {
    emit8(object, 0x48U); emit8(object, 0x8BU); emit8(object, 0x84U); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
}

static bool aggregate_type(const CinderType *type) {
    return type->kind == TYPE_STRUCT || type->kind == TYPE_UNION;
}

static void stack_adjust(ProgramBuilder *object, size_t size, bool allocate) {
    if (size == 0U) return;
    emit8(object, 0x48U); emit8(object, 0x81U); emit8(object, allocate ? 0xECU : 0xC4U); emit32(object, (uint32_t)size);
}

static void address_rbp(ProgramBuilder *object, unsigned reg, int offset) {
    emit8(object, reg < 8U ? 0x48U : 0x4CU); emit8(object, 0x8DU);
    emit8(object, (uint8_t)(0x85U | ((reg & 7U) << 3U))); emit32(object, (uint32_t)offset);
}

static void address_rsp(ProgramBuilder *object, unsigned reg, size_t offset) {
    emit8(object, reg < 8U ? 0x48U : 0x4CU); emit8(object, 0x8DU);
    emit8(object, (uint8_t)(0x84U | ((reg & 7U) << 3U))); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
}

static void xmm_rsp(ProgramBuilder *object, unsigned xmm, size_t offset, bool store) {
    emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, store ? 0x11U : 0x10U);
    emit8(object, (uint8_t)(0x84U | (xmm << 3U))); emit8(object, 0x24U); emit32(object, (uint32_t)offset);
}

static void copy_bytes(ProgramBuilder *object, size_t size) {
    emit8(object, 0x48U); emit8(object, 0xB9U); emit64(object, size);
    emit8(object, 0xF3U); emit8(object, 0xA4U);
}

/* Pack only bytes belonging to the object. A three-byte aggregate at a page
 * boundary must not cause an eight-byte load into the next page. */
static void pack_eightbyte(ProgramBuilder *object, size_t offset, size_t size) {
    emit8(object, 0x31U); emit8(object, 0xC0U);
    size_t count = size - offset < 8U ? size - offset : 8U;
    for (size_t byte = 0U; byte < count; ++byte) {
        emit8(object, 0x45U); emit8(object, 0x0FU); emit8(object, 0xB6U); emit8(object, 0x9AU); emit32(object, (uint32_t)(offset + byte));
        if (byte != 0U) { emit8(object, 0x49U); emit8(object, 0xC1U); emit8(object, 0xE3U); emit8(object, (uint8_t)(byte * 8U)); }
        emit8(object, 0x4CU); emit8(object, 0x09U); emit8(object, 0xD8U);
    }
}

static int emit_call(ProgramBuilder *object, const CinderMIRInst *selected, CinderDiagnostics *diags) {
    const CinderIRInst *inst = &selected->operands;
    const CinderMIRCallPlan *plan = selected->call;
    if (plan == NULL || plan->argument_count != inst->args.len) { cinder_diag(diags, CINDER_FATAL, inst->loc, "call has no selected ABI plan"); return 1; }
    CinderABIValue result = plan->result;
    CinderABIState state = plan->state;
    const CinderABIArgument *arguments = plan->arguments;
    const size_t *staging = plan->staging;
    size_t frame = plan->frame_size;
    stack_adjust(object, frame, true);
    /* Stage every argument before assigning registers, including arguments
     * currently held in XMM2-XMM7. Register exhaustion rolls back an entire
     * aggregate, leaving both register banks available to later arguments. */
    for (size_t a = 0U; a < inst->args.len; ++a) {
        const CinderType *type = plan->argument_types[a];
        if (aggregate_type(type)) {
            if (arguments[a].stack_offset != SIZE_MAX) {
                load_value_alloc(object, inst->args.data[a]); emit_mov_reg_reg(object, 6U, 0U);
                address_rsp(object, 7U, staging[a]); copy_bytes(object, type->size);
            } else {
                load_value_to_r10(object, inst->args.data[a]);
                for (unsigned p = 0U; p < arguments[a].value.count; ++p) {
                    pack_eightbyte(object, p * 8U, type->size); stack_store_rax(object, staging[a] + p * 8U);
                }
            }
        } else if (cinder_ir_floating(type)) {
            load_float_value(object, inst->args.data[a], 0U);
            if (type->kind == TYPE_FLOAT) { emit8(object, 0xF2U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
            xmm_rsp(object, 0U, staging[a], true);
        } else { load_value_alloc(object, inst->args.data[a]); stack_store_rax(object, staging[a]); }
    }
    if (result.memory) frame_address(object, 7U, inst->slot);
    for (size_t a = 0U; a < inst->args.len; ++a) {
        if (arguments[a].stack_offset != SIZE_MAX) continue;
        for (unsigned p = 0U; p < arguments[a].value.count; ++p) {
            if (arguments[a].value.classes[p] == ABI_INTEGER) { stack_load_rax(object, staging[a] + p * 8U); emit_mov_reg_from_rax(object, abi_register(arguments[a].registers[p])); }
            else if (arguments[a].value.classes[p] == ABI_SSE) xmm_rsp(object, arguments[a].registers[p], staging[a] + p * 8U, false);
        }
    }
    if (inst->callee == NULL) { load_value_alloc(object, inst->left); emit_mov_reg_reg(object, 11U, 0U); }
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
                    if (result.classes[p] == ABI_INTEGER) { if (integer++ != 0U) emit_mov_reg_reg(object, 0U, 2U); local_store(object, inst->slot, p); }
                    else if (result.classes[p] == ABI_SSE) local_xmm_store(object, floating++, inst->slot, p);
                }
            }
            frame_address(object, 0U, inst->slot);
            store_value_alloc(object, inst->dst);
        } else if (inst->floating_result) {
            if (inst->type->kind == TYPE_FLOAT) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
            store_float_value(object, inst->dst);
        }
        else {
            normalize_incoming_scalar(object, inst->type->kind == TYPE_BOOL);
            store_value_alloc(object, inst->dst);
        }
    }
    return 0;
}

static void emit_aggregate_argument(ProgramBuilder *object, const CinderIRInst *inst, const CinderABIArgument *argument) {
    if (argument->stack_offset != SIZE_MAX) {
        address_rbp(object, 6U, (int)(16U + argument->stack_offset));
        frame_address(object, 7U, inst->slot); copy_bytes(object, inst->type->size);
    } else {
        for (unsigned p = 0U; p < argument->value.count; ++p) {
            unsigned reg = argument->registers[p];
            if (argument->value.classes[p] == ABI_INTEGER) incoming_load(object, reg, false);
            else if (argument->value.classes[p] == ABI_SSE) incoming_load(object, reg, true);
            else continue;
            local_store(object, inst->slot, p);
        }
    }
}

static void emit_aggregate_return(ProgramBuilder *object, const CinderIRInst *inst, const CinderABIValue *result) {
    if (result->memory) {
        incoming_load(object, 0U, false); emit_mov_reg_reg(object, 7U, 0U);
        load_value_alloc(object, inst->left); emit_mov_reg_reg(object, 6U, 0U);
        copy_bytes(object, inst->type->size); incoming_load(object, 0U, false);
    } else {
        load_value_to_r10(object, inst->left); stack_adjust(object, 16U, true);
        for (unsigned p = 0U; p < result->count; ++p) { pack_eightbyte(object, p * 8U, inst->type->size); stack_store_rax(object, p * 8U); }
        unsigned floating = 0U, integer = 0U;
        for (unsigned p = 0U; p < result->count; ++p) if (result->classes[p] == ABI_SSE) xmm_rsp(object, floating++, p * 8U, false);
        for (unsigned p = 0U; p < result->count; ++p) if (result->classes[p] == ABI_INTEGER) ++integer;
        for (unsigned p = result->count; p-- > 0U;) if (result->classes[p] == ABI_INTEGER) { stack_load_rax(object, p * 8U); if (--integer != 0U) emit_mov_reg_reg(object, 2U, 0U); }
        stack_adjust(object, 16U, false);
    }
}

static void r10_store_rax(ProgramBuilder *object, unsigned offset) {
    emit8(object, 0x49U); emit8(object, 0x89U); emit8(object, 0x82U); emit32(object, offset);
}

static void emit_va_start(ProgramBuilder *object, const CinderIRInst *inst, const CinderABIState *named) {
    load_value_to_r10(object, inst->left);
    emit8(object, 0x41U); emit8(object, 0xC7U); emit8(object, 0x82U); emit32(object, 0U); emit32(object, named->gpr * 8U);
    emit8(object, 0x41U); emit8(object, 0xC7U); emit8(object, 0x82U); emit32(object, 4U); emit32(object, 48U + named->sse * 16U);
    address_rbp(object, 0U, (int)(16U + named->stack)); r10_store_rax(object, 8U);
    emit8(object, 0x48U); emit8(object, 0x8dU); emit8(object, 0x85U); frame_patch(object, MIR_PATCH_VARIADIC, 0, 0); r10_store_rax(object, 16U);
}

static void patch_here(ProgramBuilder *object, size_t offset) {
    cinder_bytes_patch32(&object->text, offset, (uint32_t)(object->text.len - offset - 4U));
}

static int emit_va_arg(ProgramBuilder *object, const CinderMIRInst *selected, CinderDiagnostics *diags) {
    const CinderIRInst *inst = &selected->operands;
    if (selected->variadic_layout == NULL) { cinder_diag(diags, CINDER_FATAL, inst->loc, "va_arg has no selected layout"); return 1; }
    CinderABIValue value = *selected->variadic_layout;
    bool aggregate = aggregate_type(inst->source_type);
    unsigned gpr = 0U, sse = 0U;
    for (unsigned p = 0U; p < value.count; ++p) {
        if (value.classes[p] == ABI_INTEGER) ++gpr;
        else if (value.classes[p] == ABI_SSE) ++sse;
    }
    load_value_to_r10(object, inst->left);
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
        if (aggregate) frame_address(object, 7U, inst->slot);
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
        frame_address(object, 7U, inst->slot); copy_bytes(object, value.size);
        emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xC6U); emit8(object, 7U);
        emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xE6U); emit8(object, 0xF8U);
    } else {
        emit8(object, 0x48U); emit8(object, 0x8BU); emit8(object, 0x06U);
        emit8(object, 0x48U); emit8(object, 0x83U); emit8(object, 0xC6U); emit8(object, 8U);
    }
    emit8(object, 0x49U); emit8(object, 0x89U); emit8(object, 0xB2U); emit32(object, 8U);
    if (done != SIZE_MAX) patch_here(object, done);
    if (aggregate) { frame_address(object, 0U, inst->slot); store_value_alloc(object, inst->dst); }
    else if (cinder_ir_floating(inst->type)) {
        emit8(object, 0x66U); emit8(object, 0x48U); emit8(object, 0x0FU); emit8(object, 0x6EU); emit8(object, 0xC0U);
        if (inst->type->kind == TYPE_FLOAT) { emit8(object, 0xF3U); emit8(object, 0x0FU); emit8(object, 0x5AU); emit8(object, 0xC0U); }
        store_float_value(object, inst->dst);
    } else { normalize_incoming_scalar(object, inst->type->kind == TYPE_BOOL); store_value_alloc(object, inst->dst); }
    return 0;
}


void cinder_mir_program_destroy(CinderMIRProgram *program) {
    if (program == NULL) return;
    for (size_t s = 0U; s < program->steps.len; ++s) {
        CinderMIRProgramStep *step = &program->steps.data[s];
        free(step->bytes.data); free(step->patches.data);
        for (size_t f = 0U; f < step->fixups.len; ++f) free(step->fixups.data[f].symbol);
        free(step->fixups.data);
    }
    free(program->steps.data); free(program);
}

bool cinder_mir_program_matches(const CinderMIRProgram *program, const CinderMIRProgram *expected) {
    if (program == NULL || expected == NULL) return program == expected;
    if (program->role != expected->role || program->steps.len != expected->steps.len || (program->steps.len != 0U && program->steps.data == NULL)) return false;
    for (size_t s = 0U; s < expected->steps.len; ++s) {
        const CinderMIRProgramStep *a = &program->steps.data[s], *b = &expected->steps.data[s];
        if (a->kind != b->kind || a->value != b->value || a->reg != b->reg || a->bytes.len != b->bytes.len || a->patches.len != b->patches.len || a->fixups.len != b->fixups.len) return false;
        if ((a->bytes.len != 0U && (a->bytes.data == NULL || memcmp(a->bytes.data, b->bytes.data, a->bytes.len) != 0)) ||
            (a->patches.len != 0U && a->patches.data == NULL) || (a->fixups.len != 0U && a->fixups.data == NULL)) return false;
        for (size_t p = 0U; p < b->patches.len; ++p) {
            const CinderMIRFramePatch *x = &a->patches.data[p], *y = &b->patches.data[p];
            if (x->kind != y->kind || x->offset != y->offset || x->slot != y->slot || x->addend != y->addend) return false;
        }
        for (size_t f = 0U; f < b->fixups.len; ++f) {
            const CinderFixup *x = &a->fixups.data[f], *y = &b->fixups.data[f];
            if (x->offset != y->offset || x->type != y->type || x->addend != y->addend || x->section_kind != y->section_kind || x->symbol == NULL || strcmp(x->symbol, y->symbol) != 0) return false;
        }
    }
    return true;
}

CinderMIRProgram *cinder_mir_program_select(const CinderIRInst *inst, const CinderMIRCallPlan *signature, const CinderMIRCallPlan *call, const CinderABIValue *variadic, CinderDiagnostics *diags) {
    if (inst->op != IR_ARG && inst->op != IR_FARG && inst->op != IR_AGG_ARG && inst->op != IR_AGG_RETURN && inst->op != IR_VA_START && inst->op != IR_VA_ARG && inst->op != IR_CALL) return NULL;
    CinderMIRProgram *program = cinder_alloc(sizeof(*program)); memset(program, 0, sizeof(*program));
    ProgramBuilder builder; memset(&builder, 0, sizeof(builder)); builder.program = program; ProgramBuilder *object = &builder;
    bool failed = signature == NULL;
    if (!failed && (inst->op == IR_ARG || inst->op == IR_FARG || inst->op == IR_AGG_ARG)) {
        int index = inst->op == IR_AGG_ARG ? inst->operator_code : inst->slot;
        if (index < 0 || (size_t)index >= signature->argument_count) failed = true;
        else {
            const CinderABIArgument *argument = &signature->arguments[index];
            if (inst->op == IR_AGG_ARG) { program->role = MIR_PROGRAM_AGG_ARGUMENT; emit_aggregate_argument(object, inst, argument); }
            else if (inst->op == IR_ARG) {
                program->role = MIR_PROGRAM_INTEGER_ARGUMENT;
                if (argument->stack_offset == SIZE_MAX) incoming_load(object, argument->registers[0], false);
                else emit_mov_rax_mem(object, (int)(16U + argument->stack_offset));
                normalize_incoming_scalar(object, inst->type->kind == TYPE_BOOL); store_value_alloc(object, inst->dst);
            } else {
                program->role = MIR_PROGRAM_FLOAT_ARGUMENT;
                emit8(object, 0xf2U); emit8(object, 0x0fU); emit8(object, 0x10U); emit8(object, 0x85U);
                if (argument->stack_offset == SIZE_MAX) frame_patch(object, MIR_PATCH_INCOMING_SSE, (int)argument->registers[0], 0);
                else emit32(object, (uint32_t)(16U + argument->stack_offset));
                if (inst->type->kind == TYPE_FLOAT) { emit8(object, 0xf3U); emit8(object, 0x0fU); emit8(object, 0x5aU); emit8(object, 0xc0U); }
                store_float_value(object, inst->dst);
            }
        }
    } else if (!failed && inst->op == IR_AGG_RETURN) {
        program->role = MIR_PROGRAM_AGG_RETURN; emit_aggregate_return(object, inst, &signature->result);
    } else if (!failed && inst->op == IR_VA_START) {
        program->role = MIR_PROGRAM_VA_START; emit_va_start(object, inst, &signature->state);
    } else if (!failed) {
        CinderMIRInst selected; memset(&selected, 0, sizeof(selected)); selected.operands = *inst;
        if (inst->op == IR_CALL) {
            program->role = MIR_PROGRAM_CALL; selected.call = (CinderMIRCallPlan *)call;
            failed = call == NULL || emit_call(object, &selected, diags) != 0;
        } else {
            program->role = MIR_PROGRAM_VA_ARG; selected.variadic_layout = (CinderABIValue *)variadic;
            failed = variadic == NULL || emit_va_arg(object, &selected, diags) != 0;
        }
    }
    flush(object);
    if (failed) { cinder_diag(diags, CINDER_FATAL, inst->loc, "cannot select complete ABI instruction program"); cinder_mir_program_destroy(program); return NULL; }
    return program;
}

void cinder_dump_mir_program(const CinderMIRProgram *program, FILE *out) {
    fprintf(out, "    protocol role=%u steps=%zu\n", (unsigned)program->role, program->steps.len);
    for (size_t s = 0U; s < program->steps.len; ++s) {
        const CinderMIRProgramStep *step = &program->steps.data[s];
        fprintf(out, "      step=%zu kind=%u value=%u reg=%u bytes=", s, (unsigned)step->kind, step->value, step->reg);
        for (size_t b = 0U; b < step->bytes.len; ++b) fprintf(out, "%02x", step->bytes.data[b]);
        fputc('\n', out);
        for (size_t p = 0U; p < step->patches.len; ++p) {
            const CinderMIRFramePatch *patch = &step->patches.data[p];
            fprintf(out, "        frame-patch kind=%u offset=%zu slot=%d addend=%" PRId64 "\n", (unsigned)patch->kind, patch->offset, patch->slot, patch->addend);
        }
        for (size_t f = 0U; f < step->fixups.len; ++f) {
            const CinderFixup *fix = &step->fixups.data[f];
            fprintf(out, "        symbol-fixup offset=%zu type=%d symbol=%s addend=%" PRId64 "\n", fix->offset, fix->type, fix->symbol, fix->addend);
        }
    }
}
