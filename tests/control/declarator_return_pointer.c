int *lookup(int);
int *(*callback)(int);
int main(void) { return sizeof(callback) != 8; }
