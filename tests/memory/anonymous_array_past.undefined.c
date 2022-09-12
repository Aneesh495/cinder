struct S{struct{int a[2];};int next;};int main(void){struct S s={.a={7,11},.next=13};return s.a[2];}
