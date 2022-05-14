int combine(int, double);
int combine(int first, double second) { return first + (int)second; }
int main(void) { return combine(4, 3.0) != 7; }
