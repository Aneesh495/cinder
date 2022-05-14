typedef int (*Callback)(int, double);
typedef Callback Dispatch[4];
int main(void) { Dispatch table; return sizeof(table) != 32 || sizeof(Callback) != 8; }
