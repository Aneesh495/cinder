struct S{long n;char tag;char data[];};int main(void){char *p;{struct S s={1,2};s.data[0]=3;p=s.data;}return p[0];}
