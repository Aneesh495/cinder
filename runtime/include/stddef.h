#ifndef CINDER_STDDEF_H
#define CINDER_STDDEF_H

typedef long ptrdiff_t;
typedef unsigned long size_t;
typedef int wchar_t;
typedef struct { _Alignas(16) long long __cinder_alignment; } max_align_t;

#define NULL ((void *)0)
#define offsetof(type, member) __cinder_offsetof(type, member)

#endif
