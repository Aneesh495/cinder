#ifndef CINDER_UNISTD_H
#define CINDER_UNISTD_H

#include <stddef.h>
#include <sys/types.h>
int close(int descriptor);
int unlink(const char *path);
int rmdir(const char *path);
pid_t fork(void);
int execvp(const char *file, char *const arguments[]);
_Noreturn void _exit(int status);
ssize_t read(int descriptor, void *data, size_t size);
ssize_t write(int descriptor, const void *data, size_t size);

#endif
