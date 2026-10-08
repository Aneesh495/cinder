struct S{long n; short tag; short data[];};int main(void){struct S s={1,2};s.data[0]=7;s.data[1]=11;s.data[2]=13;return s.data[0]+s.data[1]+s.data[2]!=31;}
