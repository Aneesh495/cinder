struct Mixed { long whole; double fraction; };
struct Mixed add(struct Mixed input, double delta, long bias) { input.whole += bias; input.fraction += delta; return input; }
int main(void) { struct Mixed input = {41, 2.25}; struct Mixed result = add(input, 0.5, -7); return result.whole != 34 || result.fraction != 2.75; }
