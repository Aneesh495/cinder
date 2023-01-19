#include "cinder.h"

#include <limits.h>
#include <string.h>

static void frame_immediate(unsigned char *bytes, int value) {
    for (unsigned n = 0U; n < 4U; ++n) bytes[n] = (unsigned char)((uint32_t)value >> (n * 8U));
}

static int frame_program(const CinderAllocation *allocation, CinderMIRFrame *frame, CinderDiagnostics *diags) {
    memset(frame, 0, sizeof(*frame));
    if (allocation->ir == NULL || allocation->ir->type == NULL || allocation->frame_size > INT32_MAX ||
        allocation->local_bytes > INT32_MAX || allocation->spill_slots > (size_t)INT32_MAX / 8U ||
        (allocation->frame_size & 15U) != 0U || (allocation->saved_gpr_mask & ~15U) != 0U) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "frame cannot be encoded with signed target displacements"); return 1;
    }
    frame->size = allocation->frame_size;
    const unsigned char enter[] = {0x55U, 0x48U, 0x89U, 0xe5U, 0x48U, 0x81U, 0xecU};
    memcpy(frame->prologue, enter, frame->size == 0U ? 4U : sizeof(enter));
    frame->prologue_size = frame->size == 0U ? 4U : 11U;
    if (frame->size != 0U) frame_immediate(frame->prologue + 7U, (int)frame->size);
    frame->epilogue[0] = 0xc9U; frame->epilogue[1] = 0xc3U;
    size_t base = allocation->local_bytes / 8U + allocation->spill_slots;
    for (unsigned bit = 0U; bit < 4U; ++bit) {
        if ((allocation->saved_gpr_mask & (1U << bit)) == 0U) continue;
        CinderMIRPreserved *saved = &frame->saved[frame->saved_count++]; saved->reg = bit + 12U;
        size_t extent = (base + frame->saved_count) * 8U;
        if (extent > frame->size) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "preserved register exceeds its allocated frame"); return 1; }
        saved->offset = -(int)extent;
        const unsigned char save[] = {0x4cU, 0x89U, 0xe0U, 0x48U, 0x89U, 0x85U};
        const unsigned char restore[] = {0x4cU, 0x8bU, 0x95U, 0U, 0U, 0U, 0U, 0x4dU, 0x89U, 0xd4U};
        memcpy(saved->save, save, sizeof(save)); saved->save[2] = (unsigned char)(0xc0U | ((saved->reg - 8U) << 3U));
        frame_immediate(saved->save + 6U, saved->offset);
        memcpy(saved->restore, restore, sizeof(restore)); saved->restore[9] = (unsigned char)(0xd0U | (saved->reg - 8U));
        frame_immediate(saved->restore + 3U, saved->offset);
    }
    frame->incoming_base = base + frame->saved_count;
    size_t required = (frame->incoming_base + 14U + (allocation->ir->type->variadic ? 22U : 0U)) * 8U;
    if (required > frame->size) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "incoming register storage exceeds its allocated frame"); return 1; }
    for (unsigned a = 0U; a < 6U; ++a) frame->incoming_gpr[a] = -(int)((frame->incoming_base + a + 1U) * 8U);
    for (unsigned a = 0U; a < 8U; ++a) frame->incoming_sse[a] = -(int)((frame->incoming_base + a + 7U) * 8U);
    if (allocation->ir->type->variadic) frame->variadic_area = -(int)frame->size;
    return 0;
}

int cinder_plan_frame(CinderAllocation *allocation, CinderDiagnostics *diags) {
    return frame_program(allocation, &allocation->frame, diags);
}

int cinder_verify_frame(const CinderAllocation *allocation, CinderDiagnostics *diags) {
    CinderMIRFrame expected;
    if (frame_program(allocation, &expected, diags) != 0) return 1;
    if (memcmp(&allocation->frame, &expected, sizeof(expected)) != 0)
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "selected frame program disagrees with physical storage");
    return diags->errors == 0U ? 0 : 1;
}

size_t cinder_ir_local_alignment(const CinderIRFunction *function, size_t slot) {
    if (slot >= function->local_types.len || function->local_types.data[slot] == NULL) return 0U;
    size_t specified = slot < function->local_alignments.len ? function->local_alignments.data[slot] : 0U;
    return specified == 0U ? function->local_types.data[slot]->align : specified;
}

int cinder_layout_stack(CinderAllocation *allocation, CinderDiagnostics *diags) {
    const CinderIRFunction *function = allocation->ir;
    allocation->local_offsets.len = 0U; allocation->local_bytes = 0U;
    if (function->local_types.len != function->local_count || (function->local_alignments.len != 0U && function->local_alignments.len != function->local_count)) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "stack layout has no complete local type table"); return 1;
    }
    for (size_t slot = 0U; slot < function->local_count; ++slot) {
        const CinderType *type = function->local_types.data[slot];
        size_t alignment = cinder_ir_local_alignment(function, slot);
        if (type == NULL || !type->complete || type->size == 0U || !cinder_object_alignment_valid(type, alignment)) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "local storage has an incomplete type or unsupported alignment"); return 1;
        }
        size_t size = type->size < 8U ? 8U : type->size;
        size_t align = alignment < 8U ? 8U : alignment;
        size_t cursor = allocation->local_bytes;
        if (cursor > 64U * 1024U * 1024U || size > 64U * 1024U * 1024U - cursor || cursor + size > (size_t)INT_MAX - (align - 1U)) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "local stack storage exceeds the target frame limit"); return 1;
        }
        cursor = (cursor + size + align - 1U) & ~(align - 1U);
        if (cursor > 64U * 1024U * 1024U) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "aligned local stack storage exceeds the target frame limit"); return 1; }
        int offset = -(int)cursor; cinder_vec_push((CinderVec *)&allocation->local_offsets, &offset);
        allocation->local_bytes = cursor;
    }
    return 0;
}
