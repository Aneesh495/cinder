struct Word { unsigned long bits; }; int main(void) { int x=19; struct Word w; w.bits=(unsigned long)&x; return *(int *)w.bits; }
