int f(int seed) { int n=0,sum=0; if(seed) goto a; goto b; a: sum+=2; if(++n<5) goto b; goto finish; b: sum+=3; if(++n<5) goto a; finish: return sum; } int main(void) { return f(1)!=12 || f(0)!=13; }
