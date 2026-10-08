int f(void) { return 1; } int main(void) { switch(4) { case sizeof(f(3)): return 0; } }
