#define ADD(a, b) ((a) + (b))
#define FLAG 1
#if FLAG
int main(void) { return ADD(20, 22); }
#else
#error inactive
#endif
