volatile unsigned short global = 1;
int main(void) {
    volatile unsigned short local = 65530;
    volatile unsigned short *pointer = &local;
    global = *pointer + 9;
    *pointer = global + 65534;
    return global == 3 && local == 1 ? 26 : 1;
}
