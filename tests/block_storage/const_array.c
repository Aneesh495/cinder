int f(void) { static const int a[]={1,4,9}; return a[2]+sizeof(a); } int main(void) { return f()!=21 || f()!=21; }
