struct S { unsigned a:5; signed b:5; }; int main(void){struct S s={21,-6};int r=(s.a+=19);s.b*=2;return r!=8 || s.a!=8 || s.b!=-12;}
