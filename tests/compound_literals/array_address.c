int main(void) { int *p = (int[4]){3,5}; p[2] = 7; return p[0]+p[1]+p[2]+p[3] != 15; }
