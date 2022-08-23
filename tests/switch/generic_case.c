int main(void) { int n=0; switch(7) { case _Generic(1L, int: 4, long: 7): n=3; break; default: n=8; } return n!=3; }
