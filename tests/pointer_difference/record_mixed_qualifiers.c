struct Item { int value; };
int main(void) {
    struct Item values[4]; const struct Item *left = values + 3; volatile struct Item *right = values;
    return left - right == 3 && right - left == -3 ? 48 : 1;
}
