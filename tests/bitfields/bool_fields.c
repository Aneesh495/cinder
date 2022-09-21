struct S { _Bool a:1; _Bool b:1; _Bool c:1; }; int main(void){struct S s={0,1,0};s.a=9;s.b=0;return s.a!=1 || s.b!=0 || s.c!=0 || sizeof(s)!=1;}
