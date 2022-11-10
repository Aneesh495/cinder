volatile signed char global = -5;
int main(void) {
    volatile signed char local = -7;
    volatile signed char *pointer = &local;
    global = *pointer + 3;
    *pointer = global - 1;
    return global == -4 && local == -5 ? 23 : 1;
}
