struct Holder { int *pointer; long offset; };
struct Holder advance(struct Holder input) { input.pointer += input.offset; return input; }
int main(void) { int array[4] = {3,7,11,19}; struct Holder input = {array,2}; struct Holder result = advance(input); return *result.pointer != 11 || result.pointer - input.pointer != 2; }
