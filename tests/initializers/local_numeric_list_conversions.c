struct P { unsigned char a; short b; float c; _Bool d; }; int main(void) { struct P p={257,7,6.5,3.0}; return p.a+p.b+(int)p.c+2*p.d; }
