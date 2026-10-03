
int main(void) { double x = 0.25; float y = 0.5f; return x + y != 0.75 || sizeof(x + y) != 8; }
