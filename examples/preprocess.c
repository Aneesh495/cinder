#define TWICE(x) ((x) + (x))
#if defined(TWICE)
int answer = TWICE(21);
#else
#error inactive branch should not fire
#endif
