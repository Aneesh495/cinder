int main(void) { volatile short x=3; return _Generic(x,short:1,volatile short:2,default:3)-1; }
