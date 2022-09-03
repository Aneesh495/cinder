#ifndef CINDER_SYS_WAIT_H
#define CINDER_SYS_WAIT_H

#include <sys/types.h>
pid_t waitpid(pid_t process, int *status, int options);
#define WIFEXITED(status) (((status) & 127) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 255)

#endif
