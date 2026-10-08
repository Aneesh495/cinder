int main(void) { goto inside; { const int x=3; inside: *(int *)&x=7; return 0; } }
