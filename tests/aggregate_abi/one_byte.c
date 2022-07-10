struct Tiny { unsigned char value; };
struct Tiny bump(struct Tiny input) { input.value += 7; return input; }
int main(void) { struct Tiny input = {231}; struct Tiny result = bump(input); return result.value != 238 || input.value != 231; }
