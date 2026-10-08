#include <stdio.h>
#include <string.h>
int main(void) { FILE *f = tmpfile(); if (f == NULL) return 1; int bad = 0; const char text[] = "owned native objects"; bad |= fwrite(text, 1, sizeof(text), f) != sizeof(text); bad |= fflush(f) != 0; bad |= ftell(f) != (long)sizeof(text); bad |= fseek(f, 0, SEEK_SET) != 0; char readback[sizeof(text)]; bad |= fread(readback, 1, sizeof(text), f) != sizeof(text); bad |= memcmp(text, readback, sizeof(text)) != 0; bad |= ferror(f) != 0; bad |= fclose(f) != 0; return bad; }
