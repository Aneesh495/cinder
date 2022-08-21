int main(void) { int n=0; double x=0.5; again: x+=1.0; if(++n<4) goto again; return x!=4.5; }
