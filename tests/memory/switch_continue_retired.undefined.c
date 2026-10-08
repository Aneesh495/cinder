int main(void) { int *p=0; for(int i=0;i<1;i++) { switch(i) { case 0: { int n=7; p=&n; continue; } } } return *p; }
