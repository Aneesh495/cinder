struct P { int a[2]; } a={{1,2}}, b={{3,4}};
int *get(void) { return (1?a:b).a; }
int main(void) { return *get(); }
