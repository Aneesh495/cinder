
int main(void) { int x = 3; { int x = 7; x++; } return x != 3; }
