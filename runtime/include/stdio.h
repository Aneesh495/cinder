#ifndef CINDER_STDIO_H
#define CINDER_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct __cinder_FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

FILE *fopen(const char *restrict path, const char *restrict mode);
FILE *fdopen(int descriptor, const char *mode);
FILE *tmpfile(void);
int fclose(FILE *stream);
int fflush(FILE *stream);
int ferror(FILE *stream);
int feof(FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
void rewind(FILE *stream);
size_t fread(void *restrict data, size_t size, size_t count, FILE *restrict stream);
size_t fwrite(const void *restrict data, size_t size, size_t count, FILE *restrict stream);
int fputc(int character, FILE *stream);
int fgetc(FILE *stream);
int fputs(const char *restrict text, FILE *restrict stream);
int puts(const char *text);
int printf(const char *restrict format, ...);
int fprintf(FILE *restrict stream, const char *restrict format, ...);
int snprintf(char *restrict data, size_t size, const char *restrict format, ...);
int vsnprintf(char *restrict data, size_t size, const char *restrict format, va_list arguments);
int rename(const char *old_path, const char *new_path);
int remove(const char *path);

#endif
