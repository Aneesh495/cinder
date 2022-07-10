struct Mixed { double fraction; long whole; };
struct Mixed add(struct Mixed input, long bias, double delta) { input.whole += bias; input.fraction -= delta; return input; }
int main(void) { struct Mixed input = {-1.25, 77}; struct Mixed result = add(input, 9, 0.5); return result.whole != 86 || result.fraction != -1.75; }
