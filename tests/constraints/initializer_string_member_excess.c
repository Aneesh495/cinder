struct P { char a[2]; }; int main(void) { struct P p={"abc"}; return p.a[0]; }
