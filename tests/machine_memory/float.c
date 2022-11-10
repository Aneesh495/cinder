volatile float global = 0;
int main(void) {
    volatile float local = 1.5f;
    volatile float *pointer = &local;
    global = *pointer + 0.25f;
    *pointer = global - 0.5f;
    return global == 1.75f && local == 1.25f ? 32 : 1;
}
