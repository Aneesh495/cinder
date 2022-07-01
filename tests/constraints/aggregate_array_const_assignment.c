struct P { const int n[2]; int x; };
int main(void) { struct P a={{1,2},3}, b={{4,5},6}; a=b; return a.x; }
