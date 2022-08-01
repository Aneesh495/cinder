enum E { A=-1 }; int main(void) { enum E x=A; return _Generic(x,int:1,default:2)-1; }
