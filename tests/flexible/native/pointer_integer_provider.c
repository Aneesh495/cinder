struct S{long n;char data[];};
int reference(void (*callback)(struct S *),struct S *s,int a,int b,int c,int d,int e,int f,int g){callback(s);callback(s);return a+b+c+d+e+f+g;}
