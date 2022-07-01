struct P { const int n; }; struct Q { struct P p; int x; };
int main(void) { struct Q a={{1},2}, b={{3},4}; a=b; return a.x; }
