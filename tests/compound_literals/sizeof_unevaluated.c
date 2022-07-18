struct S { int a,b; }; int calls; int tick(void) { calls+=1; return 3; } int main(void) { unsigned long size=sizeof (struct S){tick(),tick()}; return size!=8 || calls!=0; }
