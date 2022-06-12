int main(void) { int x=27; unsigned long words[2]; words[0]=0; words[1]=(unsigned long)&x; return *(int *)words[1]; }
