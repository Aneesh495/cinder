int main(void) { int sum=0; goto inside; for(int i=99;i<5;++i) { inside: i=2; ++sum; if(sum==3) break; continue; } return sum!=3; }
