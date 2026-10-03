int main(void) { int x = 0; int y = (0 || (x = 7)) && (x = 9); return x + y; }
