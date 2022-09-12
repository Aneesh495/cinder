struct S{struct{long a,b,c;};};static struct S make(void){return (struct S){.a=7,.b=11,.c=13};}int main(void){struct S s=make();return s.a+s.b+s.c!=31;}
