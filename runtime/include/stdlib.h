#ifndef CINDER_STDLIB_H
#define CINDER_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *allocation, size_t size);
void free(void *allocation);
_Noreturn void abort(void);
_Noreturn void exit(int status);
char *getenv(const char *name);
unsigned long long strtoull(const char *restrict text, char **restrict end, int base);
long strtol(const char *restrict text, char **restrict end, int base);
float strtof(const char *restrict text, char **restrict end);
double strtod(const char *restrict text, char **restrict end);
void qsort(void *base, size_t count, size_t width, int (*compare)(const void *, const void *));
int mkstemp(char *pattern);
char *mkdtemp(char *pattern);
char *realpath(const char *restrict path, char *restrict resolved);

#endif
