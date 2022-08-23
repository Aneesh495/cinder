int main(void) { switch(1) { case _Generic(1, int: 1, default: 1.0 % 2): return 0; } }
