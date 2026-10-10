volatile unsigned char global = 1;
int main(void) {
    volatile unsigned char local = 250;
    volatile unsigned char *pointer = &local;
    global = *pointer + 9;
    *pointer = global + 254;
    return global == 3 && local == 1 ? 24 : 1;
}
