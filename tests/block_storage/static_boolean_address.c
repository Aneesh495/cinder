int main(void) { static int x; static _Bool yes=&x; static _Bool no=(int *)0; return yes!=1 || no!=0; }
