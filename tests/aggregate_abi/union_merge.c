union Value { double fraction; long whole; };
union Value bump(union Value input) { input.whole += 5; return input; }
int main(void) { union Value input = {.whole = 4886691840L}; union Value result = bump(input); return result.whole != 4886691845L; }
