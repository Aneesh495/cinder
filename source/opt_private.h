#ifndef CINDER_OPT_PRIVATE_H
#define CINDER_OPT_PRIVATE_H
#include "cinder.h"
int64_t cinder_opt_normalize(int64_t value, const CinderType *type);
bool cinder_opt_integer_value(const CinderIRInst *inst, int64_t left, int64_t right, int64_t *out);
bool *cinder_opt_defined_values(const CinderIRFunction *function);
#endif
