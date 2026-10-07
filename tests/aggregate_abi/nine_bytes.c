struct Nine { unsigned char bytes[9]; };
struct Nine alter(struct Nine input) { input.bytes[0] += 5; input.bytes[8] -= 2; return input; }
int main(void) { struct Nine input = {{1,2,3,4,5,6,7,8,9}}; struct Nine result = alter(input); return result.bytes[0] != 6 || result.bytes[7] != 8 || result.bytes[8] != 7 || input.bytes[8] != 9; }
