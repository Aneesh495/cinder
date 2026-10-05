int main(void) { int *p = 0; for (int i = 0; i < 1; ++i) { int x = 17; p = &x; continue; } return *p; }
