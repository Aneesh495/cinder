enum { Value = ((1 ? -1 : 1U) < 0) ? 3 : 7 };
int main(void) { return Value != 7; }
