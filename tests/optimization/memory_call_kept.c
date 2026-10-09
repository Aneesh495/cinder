static void change(int *p){*p=17;}int main(void){int x=13;int *p=&x;int a=*p;change(p);int b=*p;return a+b;}
