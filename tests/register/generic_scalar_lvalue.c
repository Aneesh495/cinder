int main(void) { register int n=3; _Generic(1,int:n)=7; return n!=7; }
