struct Item { int value; };
int main(void) {
    struct Item values[4]; const volatile struct Item *left = values + 3; struct Item *right = values;
    return left - right == 3 && right - left == -3 ? 46 : 1;
}
