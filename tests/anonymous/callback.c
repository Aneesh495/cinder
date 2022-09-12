struct S{struct{long n;double d;};};static struct S f(struct S s){s.n+=3;s.d+=1.25;return s;}int main(void){struct S s={.n=7,.d=2.5};struct S (*p)(struct S)=f;s=p(s);return s.n!=10||s.d!=3.75;}
