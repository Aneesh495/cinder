#include "interp_private.h"

static size_t find_state(const InterpContext *context, InterpPointer list) {
    for (size_t i = 0U; i < context->va_states.len; ++i) {
        const InterpPointer *location = &context->va_states.data[i].location;
        if (location->object == list.object && location->offset == list.offset) return i;
    }
    return SIZE_MAX;
}

static InterpVaState *active_state(InterpContext *context, InterpPointer list, CinderLoc loc) {
    size_t index = find_state(context, list);
    if (index == SIZE_MAX || !context->va_states.data[index].active) {
        cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "va_list is not initialized or has ended"); return NULL;
    }
    InterpVaState *state = &context->va_states.data[index];
    if (!state->frame_alive) { cinder_interp_fail(context, INTERP_OBJECT_LIFETIME, loc, "variadic argument frame has expired"); return NULL; }
    return state;
}

/* The interpreter models the cursor in source argument order. It deliberately
 * does not simulate hardware registers or consult the native ABI classifier. */
static bool writable_list(InterpContext *context, InterpPointer list, CinderLoc loc) {
    InterpValue cursor = {.defined = true};
    return cinder_interp_store(context, list, context->module->types->uint_type, &cursor, false, loc);
}

static bool install(InterpContext *context, InterpPointer destination, InterpVaState state, CinderLoc loc) {
    size_t index = find_state(context, destination);
    if (index != SIZE_MAX && context->va_states.data[index].active) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "va_list is reinitialized before va_end"); return false; }
    if (!writable_list(context, destination, loc) || !cinder_interp_zero(context, destination, 24U, loc)) return false;
    state.location = destination; state.active = true;
    if (index == SIZE_MAX) cinder_vec_push((CinderVec *)&context->va_states, &state);
    else context->va_states.data[index] = state;
    return true;
}

bool cinder_interp_va_start(InterpContext *context, InterpPointer list, const InterpValue *arguments, size_t count, const CinderType *signature, size_t first, uint64_t frame, CinderLoc loc) {
    if (signature == NULL || signature->kind != TYPE_FUNCTION || signature->params.len != count || first > count) { cinder_interp_fail(context, INTERP_MALFORMED, loc, "variadic call has no actual argument signature"); return false; }
    InterpVaState state = {list, arguments, signature, count, first, frame, frame, true, true};
    return install(context, list, state, loc);
}

bool cinder_interp_va_copy(InterpContext *context, InterpPointer destination, InterpPointer source, uint64_t frame, CinderLoc loc) {
    InterpVaState *original = active_state(context, source, loc);
    if (original == NULL || !writable_list(context, source, loc)) return false;
    InterpVaState copy = *original; copy.initializing_frame = frame;
    return install(context, destination, copy, loc);
}

static bool allowed_type(const CinderType *actual, const CinderType *requested, const InterpValue *value) {
    CinderType a = *actual, b = *requested; a.qualifiers = 0U; b.qualifiers = 0U;
    if (cinder_type_compatible(&a, &b)) return true;
    if (a.kind == b.kind && a.size == b.size && a.kind >= TYPE_CHAR && a.kind <= TYPE_LLONG && a.is_unsigned != b.is_unsigned) {
        if (!a.is_unsigned && value->integer < 0) return false;
        uint64_t maximum = a.size == 8U ? UINT64_C(0x7fffffffffffffff) : (UINT64_C(1) << (a.size * 8U - 1U)) - 1U;
        return (uint64_t)value->integer <= maximum;
    }
    if (a.kind == TYPE_POINTER && b.kind == TYPE_POINTER && a.base != NULL && b.base != NULL)
        return (a.base->kind == TYPE_VOID && b.base->kind == TYPE_CHAR) || (a.base->kind == TYPE_CHAR && b.base->kind == TYPE_VOID);
    return false;
}

bool cinder_interp_va_arg(InterpContext *context, InterpPointer list, const CinderType *type, InterpValue *value, CinderLoc loc) {
    InterpVaState *state = active_state(context, list, loc);
    if (state == NULL || !writable_list(context, list, loc)) return false;
    if (state->index >= state->count) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "va_arg has no remaining argument"); return false; }
    const InterpValue *argument = &state->arguments[state->index];
    if (!allowed_type(state->signature->params.data[state->index].type, type, argument)) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "va_arg type disagrees with promoted argument"); return false; }
    *value = *argument; ++state->index;
    if (!value->defined) { cinder_interp_fail(context, INTERP_UNINITIALIZED, loc, "variadic argument is uninitialized"); return false; }
    if (!value->fp && type->kind != TYPE_STRUCT && type->kind != TYPE_UNION) value->integer = cinder_interp_integer((uint64_t)value->integer, type);
    return true;
}

bool cinder_interp_va_end(InterpContext *context, InterpPointer list, uint64_t frame, CinderLoc loc) {
    InterpVaState *state = active_state(context, list, loc);
    if (state == NULL || !writable_list(context, list, loc)) return false;
    if (state->initializing_frame != frame) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "va_end is outside the initializing function"); return false; }
    state->active = false; return true;
}

bool cinder_interp_va_finish(InterpContext *context, uint64_t frame, bool success, CinderLoc loc) {
    for (size_t i = 0U; i < context->va_states.len; ++i) {
        InterpVaState *state = &context->va_states.data[i];
        if (state->initializing_frame == frame && state->active && success) {
            cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "function returns without ending its va_list"); success = false;
        }
        if (state->argument_frame == frame) state->frame_alive = false;
    }
    return success;
}
