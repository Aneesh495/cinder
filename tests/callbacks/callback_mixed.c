double mix(int a,float b,long c,double d) { return a + b + c + d; } int main(void) { double (*fn)(int,float,long,double) = mix; return (int)(fn(7,2.5f,11L,3.0) * 2.0); }
