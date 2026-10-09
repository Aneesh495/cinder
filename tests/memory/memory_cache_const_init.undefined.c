struct S{const int x;};int main(void){struct S s={13};int *p=(int *)&s.x;*p=13;return 0;}
