#include "debug_private.h"

#include <stdlib.h>
#include <string.h>

/* Codes 1-9 belong to compilation units, functions and source declarations. */
enum {
    DT_VOID = 10, DT_BASE, DT_POINTER, DT_CONST, DT_VOLATILE, DT_RESTRICT,
    DT_ARRAY, DT_SUBRANGE, DT_OPEN_SUBRANGE, DT_STRUCT, DT_UNION,
    DT_STRUCT_DECL, DT_UNION_DECL, DT_MEMBER, DT_BITFIELD,
    DT_FUNCTION, DT_ARGUMENT, DT_VARIADIC, DT_ENUM, DT_ENUMERATOR, DT_FUNCTION_EMPTY
};

static void attribute(CinderBytes *bytes, unsigned name, unsigned form) {
    cinder_dwarf_uleb(bytes, name); cinder_dwarf_uleb(bytes, form);
}
static void abbreviation(CinderBytes *bytes, unsigned code, unsigned tag, bool children) {
    cinder_dwarf_uleb(bytes, code); cinder_dwarf_uleb(bytes, tag); cinder_bytes_put8(bytes, children ? 1U : 0U);
}
static void end(CinderBytes *bytes) { attribute(bytes, 0U, 0U); }
static void string(CinderBytes *bytes, const char *text) {
    if (text == NULL) text = "";
    cinder_bytes_append(bytes, (const unsigned char *)text, strlen(text) + 1U);
}

void cinder_debug_type_abbreviations(CinderBytes *bytes) {
    abbreviation(bytes, DT_VOID, 0x3BU, false); attribute(bytes, 3U, 8U); end(bytes);
    abbreviation(bytes, DT_BASE, 0x24U, false); attribute(bytes, 3U, 8U); attribute(bytes, 0x0BU, 0x0FU); attribute(bytes, 0x3EU, 0x0BU); end(bytes);
    abbreviation(bytes, DT_POINTER, 0x0FU, false); attribute(bytes, 0x0BU, 0x0FU); attribute(bytes, 0x49U, 0x13U); end(bytes);
    const unsigned qualifiers[] = {0x26U, 0x35U, 0x37U};
    for (unsigned i = 0U; i < CINDER_ARRAY_LEN(qualifiers); ++i) {
        abbreviation(bytes, DT_CONST + i, qualifiers[i], false); attribute(bytes, 0x49U, 0x13U); end(bytes);
    }
    abbreviation(bytes, DT_ARRAY, 1U, true); attribute(bytes, 0x49U, 0x13U); end(bytes);
    abbreviation(bytes, DT_SUBRANGE, 0x21U, false); attribute(bytes, 0x49U, 0x13U); attribute(bytes, 0x37U, 0x0FU); end(bytes);
    abbreviation(bytes, DT_OPEN_SUBRANGE, 0x21U, false); attribute(bytes, 0x49U, 0x13U); end(bytes);
    for (unsigned i = 0U; i < 4U; ++i) {
        abbreviation(bytes, DT_STRUCT + i, i % 2U == 0U ? 0x13U : 0x17U, i < 2U);
        attribute(bytes, 3U, 8U); attribute(bytes, i < 2U ? 0x0BU : 0x3CU, i < 2U ? 0x0FU : 0x0CU); end(bytes);
    }
    abbreviation(bytes, DT_MEMBER, 0x0DU, false); attribute(bytes, 3U, 8U); attribute(bytes, 0x49U, 0x13U); attribute(bytes, 0x38U, 0x0FU); end(bytes);
    abbreviation(bytes, DT_BITFIELD, 0x0DU, false); attribute(bytes, 3U, 8U); attribute(bytes, 0x49U, 0x13U);
    attribute(bytes, 0x6BU, 0x0FU); attribute(bytes, 0x0DU, 0x0FU); end(bytes);
    abbreviation(bytes, DT_FUNCTION, 0x15U, true); attribute(bytes, 0x49U, 0x13U); attribute(bytes, 0x27U, 0x0CU); end(bytes);
    abbreviation(bytes, DT_FUNCTION_EMPTY, 0x15U, false); attribute(bytes, 0x49U, 0x13U); attribute(bytes, 0x27U, 0x0CU); end(bytes);
    abbreviation(bytes, DT_ARGUMENT, 5U, false); attribute(bytes, 0x49U, 0x13U); end(bytes);
    abbreviation(bytes, DT_VARIADIC, 0x18U, false); end(bytes);
    abbreviation(bytes, DT_ENUM, 4U, true); attribute(bytes, 3U, 8U); attribute(bytes, 0x0BU, 0x0FU); end(bytes);
    abbreviation(bytes, DT_ENUMERATOR, 0x28U, false); attribute(bytes, 3U, 8U); attribute(bytes, 0x1CU, 0x0DU); end(bytes);
}

static void reference(CinderDebugTypes *types, const CinderType *type, unsigned qualifiers) {
    if (type == NULL || (qualifiers & ~7U) != 0U) { types->failed = true; cinder_bytes_put32(types->bytes, 0U); return; }
    size_t index = 0U;
    while (index < types->types.len && (types->types.data[index].type != type || types->types.data[index].qualifiers != qualifiers)) ++index;
    if (index == types->types.len) {
        if (index >= 1000000U) { types->failed = true; cinder_bytes_put32(types->bytes, 0U); return; }
        CinderDebugType entry = {type, qualifiers, 0U}; cinder_vec_push((CinderVec *)&types->types, &entry);
    }
    CinderDebugTypeFixup fixup = {types->bytes->len, index}; cinder_vec_push((CinderVec *)&types->fixups, &fixup);
    cinder_bytes_put32(types->bytes, 0U);
}

void cinder_debug_type_reference(CinderDebugTypes *types, const CinderType *type) {
    reference(types, type, type == NULL ? 0U : type->qualifiers);
}

static void record(CinderDebugTypes *types, const CinderType *type) {
    CinderBytes *bytes = types->bytes;
    bool structure = type->kind == TYPE_STRUCT;
    unsigned code = type->complete ? (structure ? DT_STRUCT : DT_UNION) : (structure ? DT_STRUCT_DECL : DT_UNION_DECL);
    cinder_dwarf_uleb(bytes, code); string(bytes, type->tag);
    if (!type->complete) { cinder_bytes_put8(bytes, 1U); return; }
    cinder_dwarf_uleb(bytes, type->size);
    for (size_t i = 0U; i < type->fields.len; ++i) {
        const CinderField *field = &type->fields.data[i];
        if (field->is_bitfield && (field->name == NULL || field->bit_width == 0U)) continue;
        cinder_dwarf_uleb(bytes, field->is_bitfield ? DT_BITFIELD : DT_MEMBER);
        string(bytes, field->name); cinder_debug_type_reference(types, field->type);
        if (field->is_bitfield) {
            cinder_dwarf_uleb(bytes, field->offset * 8U + field->bit_offset); cinder_dwarf_uleb(bytes, field->bit_width);
        } else cinder_dwarf_uleb(bytes, field->offset);
    }
    cinder_dwarf_uleb(bytes, 0U);
}

static void emit(CinderDebugTypes *types, CinderDebugType entry) {
    const CinderType *type = entry.type; CinderBytes *bytes = types->bytes;
    if (entry.qualifiers != 0U) {
        unsigned bit = (entry.qualifiers & 1U) != 0U ? 1U : (entry.qualifiers & 2U) != 0U ? 2U : 4U;
        cinder_dwarf_uleb(bytes, bit == 1U ? DT_CONST : bit == 2U ? DT_VOLATILE : DT_RESTRICT);
        reference(types, type, entry.qualifiers & ~bit); return;
    }
    switch (type->kind) {
        case TYPE_VOID: cinder_dwarf_uleb(bytes, DT_VOID); string(bytes, "void"); break;
        case TYPE_BOOL: case TYPE_CHAR: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG: case TYPE_LLONG: case TYPE_FLOAT: case TYPE_DOUBLE: {
            cinder_dwarf_uleb(bytes, DT_BASE);
            CinderType unqualified = *type; unqualified.qualifiers = 0U;
            string(bytes, cinder_type_name(&unqualified)); cinder_dwarf_uleb(bytes, type->size);
            unsigned encoding = type->kind == TYPE_BOOL ? 2U : type->kind == TYPE_FLOAT || type->kind == TYPE_DOUBLE ? 4U :
                type->kind == TYPE_CHAR ? (type->is_unsigned ? 8U : 6U) : (type->is_unsigned ? 7U : 5U);
            cinder_bytes_put8(bytes, (uint8_t)encoding); break;
        }
        case TYPE_POINTER:
            cinder_dwarf_uleb(bytes, DT_POINTER); cinder_dwarf_uleb(bytes, 8U); cinder_debug_type_reference(types, type->base); break;
        case TYPE_ARRAY:
            cinder_dwarf_uleb(bytes, DT_ARRAY); cinder_debug_type_reference(types, type->base);
            cinder_dwarf_uleb(bytes, type->complete ? DT_SUBRANGE : DT_OPEN_SUBRANGE);
            cinder_debug_type_reference(types, types->index_type);
            if (type->complete) cinder_dwarf_uleb(bytes, type->array_len);
            cinder_dwarf_uleb(bytes, 0U); break;
        case TYPE_STRUCT: case TYPE_UNION: record(types, type); break;
        case TYPE_ENUM:
            cinder_dwarf_uleb(bytes, DT_ENUM); string(bytes, type->tag); cinder_dwarf_uleb(bytes, type->size);
            for (size_t i = 0U; i < type->enumerators.len; ++i) {
                cinder_dwarf_uleb(bytes, DT_ENUMERATOR); string(bytes, type->enumerators.data[i].name);
                cinder_dwarf_sleb(bytes, type->enumerators.data[i].value);
            }
            cinder_dwarf_uleb(bytes, 0U); break;
        case TYPE_FUNCTION:
            cinder_dwarf_uleb(bytes, type->params.len == 0U && !type->variadic ? DT_FUNCTION_EMPTY : DT_FUNCTION);
            cinder_debug_type_reference(types, type->return_type); cinder_bytes_put8(bytes, 1U);
            for (size_t i = 0U; i < type->params.len; ++i) {
                cinder_dwarf_uleb(bytes, DT_ARGUMENT); cinder_debug_type_reference(types, type->params.data[i].type);
            }
            if (type->variadic) cinder_dwarf_uleb(bytes, DT_VARIADIC);
            if (type->params.len != 0U || type->variadic) cinder_dwarf_uleb(bytes, 0U);
            break;
        default: types->failed = true; break;
    }
}

int cinder_debug_types_finish(CinderDebugTypes *types, CinderDiagnostics *diags) {
    for (size_t i = 0U; i < types->types.len && !types->failed; ++i) {
        types->types.data[i].offset = types->bytes->len;
        emit(types, types->types.data[i]);
    }
    if (types->bytes->len > UINT32_MAX) types->failed = true;
    if (!types->failed) for (size_t i = 0U; i < types->fixups.len; ++i) {
        const CinderDebugTypeFixup *fix = &types->fixups.data[i];
        cinder_bytes_patch32(types->bytes, fix->offset, (uint32_t)types->types.data[fix->type].offset);
    }
    free(types->types.data); free(types->fixups.data);
    if (types->failed) cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "debug type graph is invalid or exceeds the DWARF32 profile");
    return types->failed ? 1 : 0;
}
