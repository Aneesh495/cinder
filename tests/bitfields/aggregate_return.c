struct S { unsigned a:3; signed b:5; }; struct S change(struct S s){s.a+=3;s.b-=2;return s;} int main(void){struct S s={2,-5};s=change(s);return s.a!=5 || s.b!=-7;}
