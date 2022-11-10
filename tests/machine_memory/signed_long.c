volatile long global = -5000000000L;
int main(void) {
    volatile long local = -3000000000L;
    volatile long *pointer = &local;
    global = *pointer + 3L;
    *pointer = global - 1L;
    return global == -2999999997L && local == -2999999998L ? 29 : 1;
}
