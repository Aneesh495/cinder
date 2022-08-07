#include "cinder.h"

#include <limits.h>

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
