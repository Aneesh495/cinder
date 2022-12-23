#ifndef CINDER_DEBUG_PRIVATE_H
#define CINDER_DEBUG_PRIVATE_H
#include "cinder.h"

void cinder_dwarf_uleb(CinderBytes *bytes, uint64_t value);
void cinder_dwarf_sleb(CinderBytes *bytes, int64_t value);

#endif
