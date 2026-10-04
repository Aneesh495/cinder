double large = 18446744073709551615UL;
float small = 16777217;
int main(void) { return large != 18446744073709551616.0 || small != 16777216.0f; }
