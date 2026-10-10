int first = 3, second = 5;
volatile int * volatile global = &first;
int main(void) {
    volatile int * volatile local = &second;
    volatile int * volatile *pointer = &local;
    global = *pointer;
    *pointer = &first;
    return *global == 5 && **pointer == 3 ? 34 : 1;
}
