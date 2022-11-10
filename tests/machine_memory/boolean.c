volatile _Bool global = 0;
int main(void) {
    volatile _Bool local = 1;
    volatile _Bool *pointer = &local;
    global = *pointer == 0;
    *pointer = global + 9;
    return global == 0 && local == 1 ? 31 : 1;
}
