int take(int x); int take(const int x) { return x + 2; }
int main(void) { return take(3) != 5; }
