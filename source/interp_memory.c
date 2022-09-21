#include "interp_private.h"

#include <stdlib.h>
#include <string.h>

static int64_t pointer_bits(InterpPointer pointer) {
    if (pointer.object == 0U) return 0;
    uint64_t bits = ((uint64_t)pointer.object << 32U) | (uint64_t)pointer.offset;
    return cinder_interp_integer(bits, NULL);
}

static void pointer_value(InterpValue *value, InterpPointer pointer) {
    memset(value, 0, sizeof(*value)); value->defined = true; value->pointer = true; value->address = pointer; value->integer = pointer_bits(pointer);
}

uint32_t cinder_interp_object(InterpContext *context, const CinderType *type, bool zero, bool readonly, CinderLoc loc) {
    if (type == NULL || !type->complete || (type->size == 0U && type->kind != TYPE_FUNCTION) || type->size > 64U * 1024U * 1024U || context->live_bytes > 64U * 1024U * 1024U - type->size || context->objects.len >= 1000000U) {
        cinder_interp_fail(context, INTERP_RESOURCE_LIMIT, loc, "object storage limit exceeded"); return 0U;
    }
    InterpObject object; memset(&object, 0, sizeof(object)); object.type = type; object.size = type->kind == TYPE_FUNCTION ? 1U : type->size; object.alive = true; object.readonly = readonly;
    object.bytes = cinder_alloc(object.size); object.initialized = cinder_alloc(object.size);
    memset(object.bytes, 0, object.size); memset(object.initialized, zero ? 255 : 0, object.size);
    cinder_vec_push((CinderVec *)&context->objects, &object); context->live_bytes += object.size;
    return (uint32_t)context->objects.len;
}

void cinder_interp_retire(InterpContext *context, uint32_t id) {
    if (id == 0U || (size_t)id > context->objects.len) return;
    InterpObject *object = &context->objects.data[id - 1U];
    if (!object->alive) return;
    context->live_bytes -= object->size; object->alive = false;
    free(object->bytes); free(object->initialized); free(object->pointers.data);
    object->bytes = NULL; object->initialized = NULL; object->pointers.data = NULL; object->pointers.len = 0U; object->pointers.cap = 0U;
}

InterpValue cinder_interp_pointer_value(InterpPointer pointer) { InterpValue value; pointer_value(&value, pointer); return value; }

InterpValue cinder_interp_address(const InterpContext *context, uint32_t id) { return cinder_interp_pointer_value((InterpPointer){id, 0, 0U, id == 0U ? 0U : context->objects.data[id - 1U].size}); }

static InterpObject *checked_object(InterpContext *context, InterpPointer pointer, size_t size, CinderLoc loc) {
    if (pointer.object == 0U || (size_t)pointer.object > context->objects.len) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "null or invalid object pointer"); return NULL; }
    InterpObject *object = &context->objects.data[pointer.object - 1U];
    if (!object->alive) { cinder_interp_fail(context, INTERP_OBJECT_LIFETIME, loc, "access to an object outside its lifetime"); return NULL; }
    if (pointer.offset < 0 || pointer.begin > pointer.end || pointer.end > object->size || (uint64_t)pointer.offset < pointer.begin || (uint64_t)pointer.offset > pointer.end || size > pointer.end - (size_t)pointer.offset) {
        cinder_interp_fail(context, INTERP_POINTER_BOUNDS, loc, "object or subobject bounds exceeded"); return NULL;
    }
    return object;
}

static bool compatible_scalar(const CinderType *declared, const CinderType *access) {
    CinderType a = *declared, b = *access; a.qualifiers = 0U; b.qualifiers = 0U;
    if (cinder_type_compatible(&a, &b)) return true;
    if (a.kind == b.kind && a.size == b.size && a.kind >= TYPE_CHAR && a.kind <= TYPE_LLONG) return true;
    return a.kind == TYPE_ENUM && b.kind == TYPE_INT && b.size == a.size;
}

/* Resolve the declared subobject independently of frontend field metadata.
 * Character accesses may inspect any byte of an object representation. */
static bool permits_access(const CinderType *declared, size_t extent, size_t offset, const CinderType *access, bool writing, unsigned depth) {
    if (depth > 64U || (writing && (declared->qualifiers & 1U) != 0U)) return false;
    if (offset == 0U && compatible_scalar(declared, access)) return true;
    if (declared->kind == TYPE_ARRAY && declared->base->size != 0U && offset < extent) return permits_access(declared->base, declared->base->size, offset % declared->base->size, access, writing, depth + 1U);
    if (declared->kind == TYPE_STRUCT || declared->kind == TYPE_UNION) {
        for (size_t f = 0U; f < declared->fields.len; ++f) {
            const CinderField *field = &declared->fields.data[f];
            bool flexible = (field->type->kind == TYPE_ARRAY && !field->type->complete) || cinder_type_contains_flexible(field->type);
            size_t field_extent = flexible && field->offset <= extent ? extent - field->offset : field->type->size;
            if (offset >= field->offset && offset - field->offset < field_extent && permits_access(field->type, field_extent, offset - field->offset, access, writing, depth + 1U)) return true;
        }
    }
    return access->kind == TYPE_CHAR && offset < extent;
}

static bool const_storage(const CinderType *type, size_t extent, size_t offset, size_t length, unsigned depth) {
    if (depth > 64U || (type->qualifiers & 1U) != 0U) return true;
    if (type->kind == TYPE_ARRAY && type->base->size != 0U) {
        while (length != 0U) {
            size_t within = offset % type->base->size, chunk = type->base->size - within;
            if (chunk > length) chunk = length;
            if (const_storage(type->base, type->base->size, within, chunk, depth + 1U)) return true;
            offset += chunk; length -= chunk;
        }
    } else if (type->kind == TYPE_STRUCT || type->kind == TYPE_UNION) {
        for (size_t f = 0U; f < type->fields.len; ++f) {
            const CinderField *field = &type->fields.data[f];
            if (field->is_bitfield) {
                size_t begin = field->offset + field->bit_offset / 8U;
                size_t end = field->offset + (field->bit_offset + field->bit_width + 7U) / 8U;
                if (field->name != NULL && field->bit_width != 0U && (field->type->qualifiers & 1U) != 0U && begin < offset + length && offset < end) return true;
                continue;
            }
            bool flexible = (field->type->kind == TYPE_ARRAY && !field->type->complete) || cinder_type_contains_flexible(field->type);
            size_t field_extent = flexible && field->offset <= extent ? extent - field->offset : field->type->size;
            if (field->offset < offset + length && offset < field->offset + field_extent) {
                size_t begin = offset > field->offset ? offset : field->offset;
                size_t end = offset + length < field->offset + field_extent ? offset + length : field->offset + field_extent;
                if (const_storage(field->type, field_extent, begin - field->offset, end - begin, depth + 1U)) return true;
            }
        }
    }
    return false;
}

static InterpObject *checked_access(InterpContext *context, InterpPointer pointer, const CinderType *type, bool writing, bool initializing, CinderLoc loc) {
    InterpObject *object = checked_object(context, pointer, type->size, loc);
    if (object == NULL) return NULL;
    if (writing && (object->readonly || (!initializing && const_storage(object->type, object->size, (size_t)pointer.offset, type->size, 0U)))) { cinder_interp_fail(context, INTERP_READONLY, loc, "write to a read-only object"); return NULL; }
    if (type->align == 0U || (size_t)pointer.offset % type->align != 0U || type->size > 8U || type->kind == TYPE_ARRAY || type->kind == TYPE_STRUCT || type->kind == TYPE_UNION || !permits_access(object->type, object->size, (size_t)pointer.offset, type, writing && !initializing, 0U)) {
        cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "misaligned or incompatible typed object access"); return NULL;
    }
    return object;
}

int64_t cinder_interp_bit_value(uint64_t bits, const CinderType *type, unsigned width) {
    uint64_t mask = (UINT64_C(1) << width) - 1U; bits &= mask;
    if (type->kind != TYPE_BOOL && !type->is_unsigned && (bits & (UINT64_C(1) << (width - 1U))) != 0U) bits |= ~mask;
    return cinder_interp_integer(bits, type);
}

static bool const_bit_storage(const CinderType *type, size_t extent, size_t begin, size_t width, unsigned depth) {
    if (depth > 64U || (type->qualifiers & 1U) != 0U) return true;
    if (type->kind == TYPE_ARRAY && type->base->size != 0U) {
        size_t unit = type->base->size * 8U;
        while (width != 0U) {
            size_t within = begin % unit, count = unit - within; if (count > width) count = width;
            if (const_bit_storage(type->base, type->base->size, within, count, depth + 1U)) return true;
            begin += count; width -= count;
        }
    } else if (type->kind == TYPE_STRUCT || type->kind == TYPE_UNION) {
        for (size_t f = 0U; f < type->fields.len; ++f) {
            const CinderField *field = &type->fields.data[f];
            size_t start = field->offset * 8U + field->bit_offset;
            bool flexible = (field->type->kind == TYPE_ARRAY && !field->type->complete) || cinder_type_contains_flexible(field->type);
            size_t count = field->is_bitfield ? field->bit_width : (flexible && field->offset <= extent ? extent - field->offset : field->type->size) * 8U;
            if (begin < start + count && start < begin + width) {
                if (field->is_bitfield) { if ((field->type->qualifiers & 1U) != 0U) return true; }
                else {
                    size_t intersection = begin > start ? begin : start, end = begin + width < start + count ? begin + width : start + count;
                    if (const_bit_storage(field->type, count / 8U, intersection - start, end - intersection, depth + 1U)) return true;
                }
            }
        }
    }
    return false;
}

bool cinder_interp_bit_load(InterpContext *context, InterpPointer pointer, const CinderType *type, unsigned offset, unsigned width, InterpValue *value, CinderLoc loc) {
    size_t bytes = (offset + width + 7U) / 8U;
    InterpObject *object = checked_object(context, pointer, bytes, loc); if (object == NULL) return false;
    if (type->align == 0U || (size_t)pointer.offset % type->align != 0U || !permits_access(object->type, object->size, (size_t)pointer.offset, type, false, 0U)) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "misaligned or incompatible bitfield storage access"); return false; }
    uint64_t bits = 0U;
    for (unsigned bit = 0U; bit < width; ++bit) {
        size_t position = offset + bit, byte = (size_t)pointer.offset + position / 8U;
        unsigned char mask = (unsigned char)(1U << (position % 8U));
        if ((object->initialized[byte] & mask) == 0U) { cinder_interp_fail(context, INTERP_UNINITIALIZED, loc, "read of uninitialized bitfield bits"); return false; }
        if ((object->bytes[byte] & mask) != 0U) bits |= UINT64_C(1) << bit;
    }
    memset(value, 0, sizeof(*value)); value->defined = true; value->integer = cinder_interp_bit_value(bits, type, width); return true;
}

bool cinder_interp_bit_store(InterpContext *context, InterpPointer pointer, const CinderType *type, unsigned offset, unsigned width, const InterpValue *value, bool initializing, CinderLoc loc) {
    size_t bytes = (offset + width + 7U) / 8U;
    InterpObject *object = checked_object(context, pointer, bytes, loc); if (object == NULL) return false;
    size_t begin = (size_t)pointer.offset * 8U + offset;
    if (object->readonly || (!initializing && const_bit_storage(object->type, object->size, begin, width, 0U))) { cinder_interp_fail(context, INTERP_READONLY, loc, "write to a read-only bitfield"); return false; }
    if (type->align == 0U || (size_t)pointer.offset % type->align != 0U || !permits_access(object->type, object->size, (size_t)pointer.offset, type, false, 0U)) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "misaligned or incompatible bitfield storage write"); return false; }
    for (size_t p = 0U; p < object->pointers.len;) {
        size_t old = object->pointers.data[p].offset;
        if (old < (size_t)pointer.offset + bytes && (size_t)pointer.offset < old + 8U) object->pointers.data[p] = object->pointers.data[--object->pointers.len];
        else ++p;
    }
    for (unsigned bit = 0U; bit < width; ++bit) {
        size_t position = offset + bit, byte = (size_t)pointer.offset + position / 8U;
        unsigned char mask = (unsigned char)(1U << (position % 8U));
        object->bytes[byte] = (unsigned char)((object->bytes[byte] & (unsigned char)~mask) | ((((uint64_t)value->integer >> bit) & 1U) != 0U ? mask : 0U));
        object->initialized[byte] |= mask;
    }
    return true;
}

bool cinder_interp_load(InterpContext *context, InterpPointer pointer, const CinderType *type, InterpValue *value, CinderLoc loc) {
    InterpObject *object = checked_access(context, pointer, type, false, false, loc); if (object == NULL) return false;
    size_t offset = (size_t)pointer.offset;
    for (size_t i = 0U; i < type->size; ++i) if (object->initialized[offset + i] != 255U) { cinder_interp_fail(context, INTERP_UNINITIALIZED, loc, "read of uninitialized object bytes"); return false; }
    uint64_t bits = 0U; for (size_t i = 0U; i < type->size; ++i) bits |= (uint64_t)object->bytes[offset + i] << (i * 8U);
    memset(value, 0, sizeof(*value)); value->defined = true; value->fp = cinder_ir_floating(type);
    if (type->kind == TYPE_FLOAT) { uint32_t narrow = (uint32_t)bits; float single; memcpy(&single, &narrow, sizeof(single)); value->floating = (double)single; }
    else if (type->kind == TYPE_DOUBLE) memcpy(&value->floating, &bits, sizeof(bits));
    else value->integer = cinder_interp_integer(bits, type);
    if (type->kind == TYPE_POINTER && bits == 0U) {
        value->pointer = true; value->address = (InterpPointer){0};
    } else if (!value->fp && type->size == 8U) {
        for (size_t p = 0U; p < object->pointers.len; ++p) if (object->pointers.data[p].offset == offset && (uint64_t)pointer_bits(object->pointers.data[p].pointer) == bits) {
            value->pointer = true; value->address = object->pointers.data[p].pointer; return true;
        }
    }
    return true;
}

bool cinder_interp_store(InterpContext *context, InterpPointer pointer, const CinderType *type, const InterpValue *value, bool initializing, CinderLoc loc) {
    InterpObject *object = checked_access(context, pointer, type, true, initializing, loc); if (object == NULL) return false;
    size_t offset = (size_t)pointer.offset; uint64_t bits = (uint64_t)value->integer;
    if (type->kind == TYPE_FLOAT) { float single = (float)value->floating; uint32_t narrow; memcpy(&narrow, &single, sizeof(narrow)); bits = narrow; }
    else if (type->kind == TYPE_DOUBLE) memcpy(&bits, &value->floating, sizeof(bits));
    for (size_t p = 0U; p < object->pointers.len;) {
        size_t old = object->pointers.data[p].offset;
        if (old < offset + type->size && offset < old + 8U) object->pointers.data[p] = object->pointers.data[--object->pointers.len];
        else ++p;
    }
    for (size_t i = 0U; i < type->size; ++i) { object->bytes[offset + i] = (unsigned char)(bits >> (i * 8U)); object->initialized[offset + i] = 255U; }
    if (!cinder_ir_floating(type) && type->size == 8U && value->pointer && value->address.object != 0U && bits == (uint64_t)pointer_bits(value->address)) { InterpStoredPointer stored = {offset, value->address}; cinder_vec_push((CinderVec *)&object->pointers, &stored); }
    return true;
}

bool cinder_interp_zero(InterpContext *context, InterpPointer pointer, size_t count, CinderLoc loc) {
    InterpObject *object = checked_object(context, pointer, count, loc); if (object == NULL) return false;
    if (object->readonly) { cinder_interp_fail(context, INTERP_READONLY, loc, "zero initialization modifies immutable storage"); return false; }
    size_t offset = (size_t)pointer.offset;
    memset(object->bytes + offset, 0, count); memset(object->initialized + offset, 255, count);
    for (size_t p = 0U; p < object->pointers.len;) {
        size_t old = object->pointers.data[p].offset;
        if (old < offset + count && offset < old + 8U) object->pointers.data[p] = object->pointers.data[--object->pointers.len];
        else ++p;
    }
    return true;
}

bool cinder_interp_object_copy(InterpContext *context, InterpPointer destination, InterpPointer source, const CinderType *type, bool initializing, CinderLoc loc) {
    InterpObject *from = checked_object(context, source, type->size, loc);
    InterpObject *to = checked_object(context, destination, type->size, loc);
    if (from == NULL || to == NULL) return false;
    size_t read = (size_t)source.offset, write = (size_t)destination.offset;
    if (type->align == 0U || read % type->align != 0U || write % type->align != 0U || !permits_access(from->type, from->size, read, type, false, 0U) || !permits_access(to->type, to->size, write, type, false, 0U)) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "object transfer is misaligned or incompatible"); return false; }
    if (to->readonly || (!initializing && const_storage(to->type, to->size, write, type->size, 0U))) { cinder_interp_fail(context, INTERP_READONLY, loc, "object transfer modifies read-only storage"); return false; }
    if (source.object == destination.object && read != write && read < write + type->size && write < read + type->size) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "object transfer has partially overlapping storage"); return false; }
    if (source.object == destination.object && read == write) return true;
    CINDER_VEC_TYPE(InterpStoredPointer) copied = {NULL, 0U, 0U};
    for (size_t p = 0U; p < from->pointers.len; ++p) {
        const InterpStoredPointer *pointer = &from->pointers.data[p];
        if (pointer->offset >= read && pointer->offset - read <= type->size && 8U <= type->size - (pointer->offset - read)) {
            InterpStoredPointer value = {write + pointer->offset - read, pointer->pointer}; cinder_vec_push((CinderVec *)&copied, &value);
        }
    }
    for (size_t p = 0U; p < to->pointers.len;) {
        size_t old = to->pointers.data[p].offset;
        if (old < write + type->size && write < old + 8U) to->pointers.data[p] = to->pointers.data[--to->pointers.len];
        else ++p;
    }
    memcpy(to->bytes + write, from->bytes + read, type->size);
    memcpy(to->initialized + write, from->initialized + read, type->size);
    for (size_t p = 0U; p < copied.len; ++p) cinder_vec_push((CinderVec *)&to->pointers, &copied.data[p]);
    free(copied.data); return true;
}

bool cinder_interp_offset(InterpContext *context, InterpPointer pointer, int64_t index, int direction, size_t stride, InterpValue *value, CinderLoc loc) {
    if (checked_object(context, pointer, 0U, loc) == NULL) return false;
    bool negative = (index < 0) != (direction < 0);
    uint64_t magnitude = index < 0 ? UINT64_C(0) - (uint64_t)index : (uint64_t)index;
    size_t distance = negative ? (size_t)pointer.offset - pointer.begin : pointer.end - (size_t)pointer.offset;
    if (stride == 0U || magnitude > distance / stride) { cinder_interp_fail(context, INTERP_POINTER_BOUNDS, loc, "pointer arithmetic exceeds its array bounds"); return false; }
    size_t delta = (size_t)magnitude * stride;
    pointer.offset = negative ? pointer.offset - (int64_t)delta : pointer.offset + (int64_t)delta;
    pointer_value(value, pointer); return true;
}

bool cinder_interp_member(InterpContext *context, InterpPointer pointer, size_t offset, size_t size, InterpValue *value, CinderLoc loc) {
    if (checked_object(context, pointer, 0U, loc) == NULL) return false;
    size_t available = pointer.end - (size_t)pointer.offset;
    if (offset > available || size > available - offset) { cinder_interp_fail(context, INTERP_POINTER_BOUNDS, loc, "member address exceeds aggregate bounds"); return false; }
    pointer.offset += (int64_t)offset; pointer.begin = (size_t)pointer.offset; pointer.end = pointer.begin + size;
    pointer_value(value, pointer); return true;
}

bool cinder_interp_flexible_member(InterpContext *context, InterpPointer pointer, size_t offset, const CinderType *element, InterpValue *value, CinderLoc loc) {
    if (checked_object(context, pointer, 0U, loc) == NULL) return false;
    size_t available = pointer.end - (size_t)pointer.offset;
    if (offset > available || element == NULL || !element->complete || element->size == 0U) { cinder_interp_fail(context, INTERP_POINTER_BOUNDS, loc, "flexible member exceeds its containing object"); return false; }
    size_t extent = (available - offset) / element->size * element->size;
    return cinder_interp_member(context, pointer, offset, extent, value, loc);
}

bool cinder_interp_extended_member(InterpContext *context, InterpPointer pointer, size_t offset, InterpValue *value, CinderLoc loc) {
    if (checked_object(context, pointer, 0U, loc) == NULL) return false;
    size_t available = pointer.end - (size_t)pointer.offset;
    if (offset > available) { cinder_interp_fail(context, INTERP_POINTER_BOUNDS, loc, "flexible aggregate exceeds its containing object"); return false; }
    return cinder_interp_member(context, pointer, offset, available - offset, value, loc);
}

bool cinder_interp_difference(InterpContext *context, InterpPointer left, InterpPointer right, size_t stride, int64_t *value, CinderLoc loc) {
    if (checked_object(context, left, 0U, loc) == NULL || checked_object(context, right, 0U, loc) == NULL) return false;
    if (left.object != right.object || left.begin != right.begin || left.end != right.end || stride == 0U || (left.offset - right.offset) % (int64_t)stride != 0) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "pointer subtraction requires the same array object"); return false; }
    *value = (left.offset - right.offset) / (int64_t)stride; return true;
}

static bool direct_member(const InterpObject *object, InterpPointer pointer) {
    if (object->type->kind != TYPE_STRUCT && object->type->kind != TYPE_UNION) return false;
    for (size_t f = 0U; f < object->type->fields.len; ++f) {
        const CinderField *field = &object->type->fields.data[f];
        if (pointer.offset == (int64_t)field->offset && pointer.begin == field->offset && pointer.end == field->offset + field->type->size) return true;
    }
    return false;
}

bool cinder_interp_compare(InterpContext *context, CinderIROp op, const InterpValue *left, const InterpValue *right, int64_t *value, CinderLoc loc) {
    if (!left->pointer || !right->pointer) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "comparison of pointers without object provenance"); return false; }
    InterpPointer a = left->address, b = right->address;
    if ((a.object != 0U && checked_object(context, a, 0U, loc) == NULL) || (b.object != 0U && checked_object(context, b, 0U, loc) == NULL)) return false;
    if (op == IR_CMP_EQ || op == IR_CMP_NE) {
        bool equal = a.object == b.object && a.offset == b.offset; *value = op == IR_CMP_EQ ? equal : !equal; return true;
    }
    bool same_array = a.begin == b.begin && a.end == b.end;
    bool same_aggregate = a.object != 0U && a.object == b.object && direct_member(&context->objects.data[a.object - 1U], a) && direct_member(&context->objects.data[b.object - 1U], b);
    if (a.object == 0U || a.object != b.object || (!same_array && !same_aggregate)) { cinder_interp_fail(context, INTERP_INVALID_ACCESS, loc, "relational pointer comparison requires the same array or aggregate"); return false; }
    switch (op) {
        case IR_CMP_LT_U: case IR_CMP_LT_S: *value = a.offset < b.offset; return true;
        case IR_CMP_LE_U: case IR_CMP_LE_S: *value = a.offset <= b.offset; return true;
        case IR_CMP_GT_U: case IR_CMP_GT_S: *value = a.offset > b.offset; return true;
        case IR_CMP_GE_U: case IR_CMP_GE_S: *value = a.offset >= b.offset; return true;
        default: cinder_interp_fail(context, INTERP_MALFORMED, loc, "invalid pointer comparison opcode"); return false;
    }
}

void cinder_interp_memory_destroy(InterpContext *context) {
    for (size_t i = 0U; i < context->objects.len; ++i) cinder_interp_retire(context, (uint32_t)i + 1U);
    free(context->objects.data); context->objects.data = NULL; context->objects.len = 0U; context->objects.cap = 0U;
    free(context->va_states.data); context->va_states.data = NULL; context->va_states.len = 0U; context->va_states.cap = 0U;
}
