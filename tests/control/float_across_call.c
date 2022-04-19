double twice(double x) { return x+x; } double test(double a,double b) { return a + twice(b); } int main(void) { return test(3.0,4.0) != 11.0; }
