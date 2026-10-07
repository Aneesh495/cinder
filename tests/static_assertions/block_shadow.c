typedef long T; int main(void) { int T=3; _Static_assert(sizeof T==4,"object shadows typedef"); return T-3; }
