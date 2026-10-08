#include <ctype.h>
int main(void) { return !isdigit('7') || isdigit('a') || !isspace('\n') || !isalnum('Z') || !isalpha('b') || !isxdigit('f') || tolower('A') != 'a' || toupper('z') != 'Z'; }
