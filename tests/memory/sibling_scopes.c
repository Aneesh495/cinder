int main(void) { int sum = 0; { int x = 17; int *p = &x; sum += *p; } { int x = 25; int *p = &x; sum += *p; } return sum; }
