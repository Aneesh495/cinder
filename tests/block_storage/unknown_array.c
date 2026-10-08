int f(void) { static int a[]={4,5}; ++a[0]; return a[0]+sizeof(a); } int main(void) { return f()!=13 || f()!=14; }
