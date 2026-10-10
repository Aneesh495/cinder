#ifndef CINDER_INTERP_PRIVATE_H
#define CINDER_INTERP_PRIVATE_H
#include "cinder.h"

typedef struct {
    uint32_t object;
    int64_t offset;
    size_t begin;
    size_t end;
    const CinderType *declared_type;
    bool readonly_origin;
} InterpPointer;

typedef struct {
    int64_t integer;
    double floating;
    bool fp;
    bool defined;
    bool pointer;
    bool integer_word;
    bool word_uncertain;
    InterpPointer address;
} InterpValue;

typedef struct {
    size_t offset;
    InterpPointer pointer;
    unsigned word_kind; /* 0: pointer storage; 1: integer word; 2: ambiguous word */
} InterpStoredPointer;

typedef struct {
    const CinderType *type;
    const char *function_name;
    unsigned char *bytes;
    unsigned char *initialized;
    size_t size;
    bool alive;
    bool readonly;
    CINDER_VEC_TYPE(InterpStoredPointer) pointers;
} InterpObject;

typedef struct {
    InterpPointer location;
    const InterpValue *arguments;
    const CinderType *signature;
    size_t count;
    size_t index;
    uint64_t argument_frame;
    uint64_t initializing_frame;
    bool active;
    bool frame_alive;
} InterpVaState;

typedef struct {
    const CinderIRModule *module;
    uint32_t *globals;
    CINDER_VEC_TYPE(InterpObject) objects;
    CINDER_VEC_TYPE(InterpVaState) va_states;
    uint64_t frames;
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
InterpValue cinder_interp_pointer_value(InterpPointer pointer);
bool cinder_interp_word_pointer(const InterpValue *word, InterpValue *value);
void cinder_interp_member_origin(InterpPointer parent, size_t field, size_t offset, InterpValue *value);
int64_t cinder_interp_bit_value(uint64_t bits, const CinderType *type, unsigned width);
bool cinder_interp_bit_load(InterpContext *context, InterpPointer pointer, const CinderType *type, unsigned offset, unsigned width, InterpValue *value, CinderLoc loc);
bool cinder_interp_bit_store(InterpContext *context, InterpPointer pointer, const CinderType *type, unsigned offset, unsigned width, const InterpValue *value, bool initializing, CinderLoc loc);
bool cinder_interp_load(InterpContext *context, InterpPointer pointer, const CinderType *type, InterpValue *value, CinderLoc loc);
bool cinder_interp_store(InterpContext *context, InterpPointer pointer, const CinderType *type, const InterpValue *value, bool initializing, CinderLoc loc);
bool cinder_interp_zero(InterpContext *context, InterpPointer pointer, size_t count, CinderLoc loc);
bool cinder_interp_object_copy(InterpContext *context, InterpPointer destination, InterpPointer source, const CinderType *type, bool initializing, CinderLoc loc);
bool cinder_interp_offset(InterpContext *context, InterpPointer pointer, int64_t index, int direction, size_t stride, InterpValue *value, CinderLoc loc);
bool cinder_interp_member(InterpContext *context, InterpPointer pointer, size_t offset, size_t size, InterpValue *value, CinderLoc loc);
bool cinder_interp_flexible_member(InterpContext *context, InterpPointer pointer, size_t offset, const CinderType *element, InterpValue *value, CinderLoc loc);
bool cinder_interp_extended_member(InterpContext *context, InterpPointer pointer, size_t offset, InterpValue *value, CinderLoc loc);
bool cinder_interp_difference(InterpContext *context, InterpPointer left, InterpPointer right, size_t stride, int64_t *value, CinderLoc loc);
bool cinder_interp_compare(InterpContext *context, CinderIROp op, const InterpValue *left, const InterpValue *right, int64_t *value, CinderLoc loc);
void cinder_interp_memory_destroy(InterpContext *context);
bool cinder_interp_va_start(InterpContext *context, InterpPointer list, const InterpValue *arguments, size_t count, const CinderType *signature, size_t first, uint64_t frame, CinderLoc loc);
bool cinder_interp_va_copy(InterpContext *context, InterpPointer destination, InterpPointer source, uint64_t frame, CinderLoc loc);
bool cinder_interp_va_arg(InterpContext *context, InterpPointer list, const CinderType *type, InterpValue *value, CinderLoc loc);
bool cinder_interp_va_end(InterpContext *context, InterpPointer list, uint64_t frame, CinderLoc loc);
bool cinder_interp_va_finish(InterpContext *context, uint64_t frame, bool success, CinderLoc loc);
#endif
