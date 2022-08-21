int main(void) { int sum=0; { int x=3; sum=x; goto next; } { int x=4; next: x=7; sum+=x; } return sum!=10; }
