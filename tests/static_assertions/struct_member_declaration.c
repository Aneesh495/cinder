struct P { int a; _Static_assert(sizeof(long)==8,"long field ABI"); double b; }; int main(void) { struct P p={3,2.5}; return sizeof p!=16 || p.a!=3 || p.b!=2.5; }
