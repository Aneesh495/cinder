struct Bytes { unsigned char value[3]; };
struct Bytes swap(struct Bytes input) { unsigned char first = input.value[0]; input.value[0] = input.value[2]; input.value[2] = first; return input; }
int main(void) { struct Bytes input = {{3, 117, 249}}; struct Bytes result = swap(input); return result.value[0] != 249 || result.value[1] != 117 || result.value[2] != 3 || input.value[0] != 3; }
