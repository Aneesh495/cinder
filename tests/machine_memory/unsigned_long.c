volatile unsigned long global = 1;
int main(void) {
    volatile unsigned long local = 18446744073709551610UL;
    volatile unsigned long *pointer = &local;
    global = *pointer + 9UL;
    *pointer = global + 18446744073709551614UL;
    return global == 3UL && local == 1UL ? 30 : 1;
}
