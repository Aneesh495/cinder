#include <string.h>
int main(void) { char text[48]; strcpy(text, "compiler"); strcat(text, " pipeline"); return strlen(text) != 17 || strcmp(text, "compiler pipeline") || strncmp(text, "compiler", 8) || strchr(text, 'p') != text + 3 || strrchr(text, 'p') != text + 11; }
