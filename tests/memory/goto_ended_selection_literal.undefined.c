int main(void) { int *p=0; if((p=(int[1]){3})!=0) goto finish; finish: return *p; }
