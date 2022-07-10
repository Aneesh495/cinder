struct Pair { long total; double scale; };
struct Pair accumulate(struct Pair input, int count) { if (count == 0) return input; input.total += count; input.scale += 0.5; return accumulate(input, count-1); }
int main(void) { struct Pair input = {1,1.25}; struct Pair result = accumulate(input,7); return result.total != 29 || result.scale != 4.75 || input.total != 1; }
