int main(void) { int sum=0; goto inside; for(int i=99;i<4;++i) { inside: i=3; sum+=i; } return sum!=3; }
