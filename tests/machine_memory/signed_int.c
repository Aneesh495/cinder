volatile int global = -500000;
int main(void) {
    volatile int local = -300000;
    volatile int *pointer = &local;
    global = *pointer + 3;
    *pointer = global - 1;
    return global == -299997 && local == -299998 ? 27 : 1;
}
