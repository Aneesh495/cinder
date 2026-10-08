int f(void) { return 3; } int main(void) { int f=2; int sum=f; { extern int f(void); sum+=f(); } return sum!=5 || f!=2; }
