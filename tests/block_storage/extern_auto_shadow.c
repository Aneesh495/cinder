int x=7; int main(void) { int x=3; int sum=x; { extern int x; sum+=x; } return sum!=10 || x!=3; }
