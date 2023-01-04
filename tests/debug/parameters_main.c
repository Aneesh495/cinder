#include "parameters.h"

int debug_stack_call(int value) { return value; }

int main(void) {
    int point = 8;
    return (int)debug_stack(1, 2, 3, 4, 5, 6, 7, &point,
                           1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0);
}
