static double sum(double x, int n) {
    double result = 0.0;
    for (int i = 0; i < n; ++i) result += x * 2.0;
    return result;
}
int main(void) { return (int)sum(2.5, 3); }
