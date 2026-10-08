#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
int main(void) { char path[] = "/tmp/cinder-runtime-XXXXXX"; char *directory = mkdtemp(path); if (!directory) return 1; char sub[96]; strcpy(sub, path); strcat(sub, "/sub"); int bad = mkdir(sub, 0700) != 0; char *resolved = realpath(sub, 0); bad |= resolved == 0; free(resolved); bad |= rmdir(sub) != 0; bad |= rmdir(path) != 0; char file[] = "/tmp/cinder-runtime-file-XXXXXX"; int fd = mkstemp(file); if (fd < 0) return 2; FILE *stream = fdopen(fd, "wb"); if (!stream) { close(fd); unlink(file); return 3; } bad |= fputs("compiler", stream) < 0; bad |= fclose(stream) != 0; bad |= unlink(file) != 0; return bad; }
