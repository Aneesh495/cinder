#include <stddef.h>
#include <string.h>

int main(void) {
    char text[] = "ababcabc";
    char *(*search)(const char *, const char *) = strstr;
    return search(text, "abc") != text + 2 || search(text, "") != text ||
           search(text, "ababcabc") != text || search(text, "missing") != NULL ||
           search("", "x") != NULL;
}
