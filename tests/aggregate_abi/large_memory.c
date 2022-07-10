struct Large { long first; long second; long third; };
struct Large add(struct Large input, long extra) { input.first += extra; input.second -= extra; input.third *= 2; return input; }
int main(void) { struct Large input = {11,22,33}; struct Large result = add(input, 7); return result.first != 18 || result.second != 15 || result.third != 66 || input.first != 11; }
