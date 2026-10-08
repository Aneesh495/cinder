typedef int Tail[];struct S{int n;Tail data;};int main(void){struct S s={41};return s.n!=41||sizeof(s)!=4;}
