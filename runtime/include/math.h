#ifndef CINDER_MATH_H
#define CINDER_MATH_H

/* IEEE binary64 target classification. Copy the representation through bytes
 * so the implementation does not depend on union active-member rules. */
static inline int __cinder_isfinite(double value) {
    unsigned char *bytes = (unsigned char *)&value;
    unsigned exponent = ((unsigned)bytes[7] & 127U) * 16U + ((unsigned)bytes[6] >> 4);
    return exponent != 2047U;
}
#define isfinite(value) __cinder_isfinite(value)

#endif
