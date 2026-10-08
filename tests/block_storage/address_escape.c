int *get(void) { static int x=2; return &x; } int main(void) { int *p=get(); *p=7; return *get()!=7 || p!=get(); }
