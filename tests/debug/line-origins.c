#include "line-origins.h"
#define ADJUST(value) ((value) + 3)
#line 70 "logical-source.c"
static int adjusted(int value) {
    int adjusted_value = ADJUST(header_value(value));
    return adjusted_value;
}
#line 8 "other-source.c"
int main(void) {
    return adjusted(5);
}
