#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#define CHECK(name, type) _Static_assert(_Generic(&(name), type: 1, default: 0), #name " prototype")
CHECK(malloc, void *(*)(size_t));
CHECK(calloc, void *(*)(size_t, size_t));
CHECK(realloc, void *(*)(void *, size_t));
CHECK(free, void (*)(void *));
CHECK(abort, void (*)(void));
CHECK(exit, void (*)(int));
CHECK(getenv, char *(*)(const char *));
CHECK(strtoull, unsigned long long (*)(const char *, char **, int));
CHECK(strtol, long (*)(const char *, char **, int));
CHECK(strtof, float (*)(const char *, char **));
CHECK(strtod, double (*)(const char *, char **));
CHECK(qsort, void (*)(void *, size_t, size_t, int (*)(const void *, const void *)));
CHECK(mkstemp, int (*)(char *));
CHECK(mkdtemp, char *(*)(char *));
CHECK(realpath, char *(*)(const char *, char *));
CHECK(memcpy, void *(*)(void *, const void *, size_t));
CHECK(memmove, void *(*)(void *, const void *, size_t));
CHECK(memset, void *(*)(void *, int, size_t));
CHECK(memcmp, int (*)(const void *, const void *, size_t));
CHECK(memchr, void *(*)(const void *, int, size_t));
CHECK(strlen, size_t (*)(const char *));
CHECK(strcmp, int (*)(const char *, const char *));
CHECK(strncmp, int (*)(const char *, const char *, size_t));
CHECK(strchr, char *(*)(const char *, int));
CHECK(strrchr, char *(*)(const char *, int));
CHECK(strcat, char *(*)(char *, const char *));
CHECK(strcpy, char *(*)(char *, const char *));
CHECK(strerror, char *(*)(int));
CHECK(fopen, FILE *(*)(const char *, const char *));
CHECK(fdopen, FILE *(*)(int, const char *));
CHECK(tmpfile, FILE *(*)(void));
CHECK(fclose, int (*)(FILE *));
CHECK(fflush, int (*)(FILE *));
CHECK(ferror, int (*)(FILE *));
CHECK(feof, int (*)(FILE *));
CHECK(fseek, int (*)(FILE *, long, int));
CHECK(ftell, long (*)(FILE *));
CHECK(rewind, void (*)(FILE *));
CHECK(fread, size_t (*)(void *, size_t, size_t, FILE *));
CHECK(fwrite, size_t (*)(const void *, size_t, size_t, FILE *));
CHECK(fputc, int (*)(int, FILE *));
CHECK(fgetc, int (*)(FILE *));
CHECK(fputs, int (*)(const char *, FILE *));
CHECK(puts, int (*)(const char *));
CHECK(printf, int (*)(const char *, ...));
CHECK(fprintf, int (*)(FILE *, const char *, ...));
CHECK(snprintf, int (*)(char *, size_t, const char *, ...));
CHECK(vsnprintf, int (*)(char *, size_t, const char *, va_list));
CHECK(rename, int (*)(const char *, const char *));
CHECK(remove, int (*)(const char *));
CHECK(isspace, int (*)(int));
CHECK(isdigit, int (*)(int));
CHECK(isalnum, int (*)(int));
CHECK(isalpha, int (*)(int));
CHECK(isxdigit, int (*)(int));
CHECK(tolower, int (*)(int));
CHECK(toupper, int (*)(int));
CHECK(time, time_t (*)(time_t *));
CHECK(gmtime, struct tm *(*)(const time_t *));
CHECK(close, int (*)(int));
CHECK(unlink, int (*)(const char *));
CHECK(rmdir, int (*)(const char *));
CHECK(fork, pid_t (*)(void));
CHECK(execvp, int (*)(const char *, char *const *));
CHECK(_exit, void (*)(int));
CHECK(waitpid, pid_t (*)(pid_t, int *, int));
CHECK(mkdir, int (*)(const char *, mode_t));
CHECK(read, ssize_t (*)(int, void *, size_t));
CHECK(write, ssize_t (*)(int, const void *, size_t));
int main(void) { return 0; }
