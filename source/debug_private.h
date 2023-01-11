#ifndef CINDER_DEBUG_PRIVATE_H
#define CINDER_DEBUG_PRIVATE_H
#include "cinder.h"

void cinder_dwarf_uleb(CinderBytes *bytes, uint64_t value);
void cinder_dwarf_sleb(CinderBytes *bytes, int64_t value);
void cinder_dwarf_string(CinderBytes *bytes, const char *value);
void cinder_debug_offset(CinderMachineObject *object, size_t section, const char *symbol, int kind, int64_t addend);
typedef struct { CINDER_VEC_TYPE(const char *) names; } CinderDebugFiles;
size_t cinder_debug_file(CinderDebugFiles *files, const char *name);
bool cinder_debug_location_valid(CinderSourceManager *sources, CinderLoc loc);
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
int cinder_debug_scopes_valid(const CinderMachineObject *object, const CinderIRModule *module, CinderSourceManager *sources, CinderDiagnostics *diags);
int cinder_debug_scope_entries(CinderMachineObject *object, size_t info, size_t ranges, size_t locations, const CinderDebugFunction *function,
                               const CinderIRFunction *ir, CinderDebugFiles *files, CinderSourceManager *sources, CinderDebugTypes *types, CinderDiagnostics *diags);

#endif
