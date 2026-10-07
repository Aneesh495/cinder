#ifndef CINDER_STDARG_H
#define CINDER_STDARG_H

typedef struct __cinder_va_state {
    unsigned int gp_offset;
    unsigned int fp_offset;
    void *overflow_arg_area;
    void *reg_save_area;
} __cinder_va_state;
typedef __cinder_va_state va_list[1];

#define va_start(list, last) __cinder_va_start(list, last)
#define va_arg(list, type) __cinder_va_arg(list, type)
#define va_copy(destination, source) __cinder_va_copy(destination, source)
#define va_end(list) __cinder_va_end(list)

#endif
