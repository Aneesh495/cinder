struct S { unsigned a:3; }; int main(void){struct S s={5};switch(s.a){case 5:return 0;case -1:return 1;default:return 2;}}
