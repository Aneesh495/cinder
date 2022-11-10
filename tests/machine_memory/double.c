volatile double global = 0;
int main(void) {
    volatile double local = 1.5;
    volatile double *pointer = &local;
    global = *pointer + 0.25;
    *pointer = global - 0.5;
    return global == 1.75 && local == 1.25 ? 33 : 1;
}
