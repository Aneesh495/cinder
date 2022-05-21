#include "cinder.h"

#include <limits.h>

int cinder_layout_stack(CinderAllocation *allocation, CinderDiagnostics *diags) {
    const CinderIRFunction *function = allocation->ir;
    allocation->local_offsets.len = 0U; allocation->local_bytes = 0U;
    if (function->local_types.len != function->local_count) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "stack layout has no complete local type table"); return 1;
    }
    for (size_t slot = 0U; slot < function->local_count; ++slot) {
        const CinderType *type = function->local_types.data[slot];
        if (type == NULL || !type->complete || type->size == 0U || type->align == 0U || type->align > 16U || (type->align & (type->align - 1U)) != 0U) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "local storage has an incomplete type or unsupported alignment"); return 1;
        }
        size_t size = type->size < 8U ? 8U : type->size;
        size_t align = type->align < 8U ? 8U : type->align;
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
