#define static_assert _Static_assert
#define WIDTH(T,N) static_assert(sizeof(T)==(N), "width of " #T)
WIDTH(long,8); int main(void) { return 0; }
