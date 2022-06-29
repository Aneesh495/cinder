struct P { unsigned char a; short b; float c; _Bool d; }; struct P p={257,7,6.5,3.0}; int main(void) { return p.a+p.b+(int)p.c+2*p.d; }
