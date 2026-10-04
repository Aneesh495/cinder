enum { First = 2, Second = First * 7, Third = (Second << 2) | 3 };
int main(void) { return Third != 59; }
