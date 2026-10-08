struct S { char tag; struct { unsigned a:3; signed b:5; }; }; int main(void){struct S s={.tag='Q',.a=6,.b=-11};s.a=4;return s.tag!='Q' || s.a!=4 || s.b!=-11 || sizeof(s)!=8;}
