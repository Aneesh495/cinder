int main(void) {
    int values[4]; volatile int *left = values + 3; int *right = values;
    return left - right == 3 && right - left == -3 ? 42 : 1;
}
