struct Holder { int *pointer; long marker; };
struct Holder create(void) { int value = 17; struct Holder result = {&value,1}; return result; }
int main(void) { struct Holder result = create(); return *result.pointer; }
