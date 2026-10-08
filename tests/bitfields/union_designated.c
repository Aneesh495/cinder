union U { int n; struct { unsigned a:3; unsigned b:5; }; }; int main(void){union U u={.a=6,.b=27};return u.a!=6 || u.b!=27 || sizeof(u)!=4;}
