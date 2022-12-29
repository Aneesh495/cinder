#ifndef CINDER_DEBUG_PRIVATE_H
#define CINDER_DEBUG_PRIVATE_H
#include "cinder.h"

void cinder_dwarf_uleb(CinderBytes *bytes, uint64_t value);
void cinder_dwarf_sleb(CinderBytes *bytes, int64_t value);
typedef struct { const CinderType *type; unsigned qualifiers; size_t offset; } CinderDebugType;
typedef struct { size_t offset; size_t type; } CinderDebugTypeFixup;
typedef struct {
    CinderBytes *bytes;
    const CinderType *index_type;
    CINDER_VEC_TYPE(CinderDebugType) types;
    CINDER_VEC_TYPE(CinderDebugTypeFixup) fixups;
    bool failed;
} CinderDebugTypes;
void cinder_debug_type_abbreviations(CinderBytes *bytes);
void cinder_debug_type_reference(CinderDebugTypes *types, const CinderType *type);
int cinder_debug_types_finish(CinderDebugTypes *types, CinderDiagnostics *diags);

#endif
