#ifndef CINDER_REGALLOC_PRIVATE_H
#define CINDER_REGALLOC_PRIVATE_H

#include "cinder.h"

typedef struct {
    size_t words;
    size_t blocks;
    uint64_t *use;
    uint64_t *def;
    uint64_t *in;
    uint64_t *out;
    size_t *begin;
    size_t *end;
} CinderLiveness;

void cinder_liveness_destroy(CinderLiveness *live);
int cinder_liveness_build(const CinderIRFunction *function, CinderLiveness *live, CinderDiagnostics *diags);
bool cinder_live_has(const uint64_t *bits, CinderValueId value);

#endif
