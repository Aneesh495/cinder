#define ANSWER 40
int add(int a, int b) { return a + b; }
int main(void) {
    int value = add(ANSWER, 2);
    if (value == 42) return value;
    return 0;
}
