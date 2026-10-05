#ifndef CINDER_INTERP_PRIVATE_H
#define CINDER_INTERP_PRIVATE_H
#include "cinder.h"

typedef struct {
    uint32_t object;
    int64_t offset;
    size_t begin;
    size_t end;
} InterpPointer;

typedef struct {
    int64_t integer;
    double floating;
    bool fp;
    bool defined;
    bool pointer;
    InterpPointer address;
} InterpValue;

typedef struct {
    size_t offset;
    InterpPointer pointer;
} InterpStoredPointer;

typedef struct {
    const CinderType *type;
    unsigned char *bytes;
    unsigned char *initialized;
    size_t size;
    bool alive;
    bool readonly;
    CINDER_VEC_TYPE(InterpStoredPointer) pointers;
} InterpObject;

typedef struct {
    const CinderIRModule *module;
    uint32_t *globals;
    CINDER_VEC_TYPE(InterpObject) objects;
    size_t live_bytes;
    unsigned steps;
    unsigned limit;
    unsigned depth;
    CinderInterpClass classification;
    CinderDiagnostics *diags;
} InterpContext;

void cinder_interp_fail(InterpContext *context, CinderInterpClass classification, CinderLoc loc, const char *reason);
int64_t cinder_interp_integer(uint64_t bits, const CinderType *type);
uint32_t cinder_interp_object(InterpContext *context, const CinderType *type, bool zero, bool readonly, CinderLoc loc);
void cinder_interp_retire(InterpContext *context, uint32_t id);
InterpValue cinder_interp_address(const InterpContext *context, uint32_t id);
bool cinder_interp_load(InterpContext *context, InterpPointer pointer, const CinderType *type, InterpValue *value, CinderLoc loc);
bool cinder_interp_store(InterpContext *context, InterpPointer pointer, const CinderType *type, const InterpValue *value, CinderLoc loc);
bool cinder_interp_offset(InterpContext *context, InterpPointer pointer, int64_t index, int direction, size_t stride, InterpValue *value, CinderLoc loc);
bool cinder_interp_member(InterpContext *context, InterpPointer pointer, size_t offset, size_t size, InterpValue *value, CinderLoc loc);
bool cinder_interp_difference(InterpContext *context, InterpPointer left, InterpPointer right, size_t stride, int64_t *value, CinderLoc loc);
bool cinder_interp_compare(InterpContext *context, CinderIROp op, const InterpValue *left, const InterpValue *right, int64_t *value, CinderLoc loc);
void cinder_interp_memory_destroy(InterpContext *context);
#endif
