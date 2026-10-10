volatile unsigned int global = 1;
int main(void) {
    volatile unsigned int local = 4294967290U;
    volatile unsigned int *pointer = &local;
    global = *pointer + 9U;
    *pointer = global + 4294967294U;
    return global == 3U && local == 1U ? 28 : 1;
}
