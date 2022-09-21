struct S { _Bool a:1; unsigned b:9; _Bool c:1; signed d:5; }; int main(void){struct S s={1,399,1,-9};s.b=201;return s.a!=1 || s.b!=201 || s.c!=1 || s.d!=-9 || sizeof(s)!=4;}
