volatile int unwind_observed;
int unwind_leaf(int value) {
    unwind_observed = value;
    if (value < 0) return 7 - value;
    return value * 3 + 1;
}
int unwind_middle(int count) {
    int total = 0;
    for (int i = 1; i <= count; ++i) total += unwind_leaf(i);
    return total;
}
int main(void) { return unwind_middle(3) + unwind_observed; }
