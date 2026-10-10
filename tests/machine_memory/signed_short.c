volatile short global = -500;
int main(void) {
    volatile short local = -300;
    volatile short *pointer = &local;
    global = *pointer + 3;
    *pointer = global - 1;
    return global == -297 && local == -298 ? 25 : 1;
}
