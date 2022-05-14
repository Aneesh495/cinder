int main(void) {
    int sum = 0;
    for (int i = 0, weight = 3; i < 5; ++i) sum += i * weight;
    return sum != 30;
}
