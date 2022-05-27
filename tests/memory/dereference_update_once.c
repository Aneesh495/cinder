int main(void) { int a[2]; a[0] = 12; a[1] = 30; int *p = a; int old = (*p++)++; return old + a[0] + (int)(p - a) + 17; }
