int main(void) { int sum=0; { int x=3; sum=x; goto outside; sum=9; } outside: return sum!=3; }
