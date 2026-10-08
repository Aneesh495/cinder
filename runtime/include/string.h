#ifndef CINDER_STRING_H
#define CINDER_STRING_H

#include <stddef.h>

void *memcpy(void *restrict destination, const void *restrict source, size_t size);
void *memmove(void *destination, const void *source, size_t size);
void *memset(void *destination, int byte, size_t size);
int memcmp(const void *left, const void *right, size_t size);
void *memchr(const void *data, int byte, size_t size);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t size);
char *strchr(const char *text, int character);
char *strrchr(const char *text, int character);
char *strcat(char *restrict destination, const char *restrict source);
char *strcpy(char *restrict destination, const char *restrict source);
char *strerror(int code);

#endif
