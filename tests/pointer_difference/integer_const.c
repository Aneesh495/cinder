int main(void) {
    int values[4]; const int *left = values + 3; int *right = values;
    return left - right == 3 && right - left == -3 ? 41 : 1;
}
