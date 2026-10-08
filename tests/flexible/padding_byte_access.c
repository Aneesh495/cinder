struct S{long n; char tag; char data[];};int main(void){struct S s={1,2};for(int i=0;i<7;++i)s.data[i]=(char)(i+3);int sum=0;for(int i=0;i<7;++i)sum+=s.data[i];return sum!=42;}
