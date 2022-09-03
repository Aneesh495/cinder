#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
int main(void) { pid_t p = fork(); if (p < 0) return 1; if (p == 0) { char *args[] = {"/bin/sh", "-c", "exit 23", 0}; execvp(args[0], args); _exit(127); } int status = 0; pid_t result; do { result = waitpid(p, &status, 0); } while (result < 0 && errno == EINTR); return result != p || !WIFEXITED(status) || WEXITSTATUS(status) != 23; }
