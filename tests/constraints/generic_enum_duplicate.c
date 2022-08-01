enum E { A=-1 }; int main(void) { enum E x=A; return _Generic(x,enum E:1,int:2); }
