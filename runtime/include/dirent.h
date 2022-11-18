#ifndef CINDER_DIRENT_H
#define CINDER_DIRENT_H
/* Opaque directory handles use the host libc's POSIX pointer ABI. */
typedef struct __cinder_directory DIR;
DIR *opendir(const char *name);
int closedir(DIR *directory);
#endif
