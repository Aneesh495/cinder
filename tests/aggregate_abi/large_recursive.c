struct State { long total; double scale; long calls; };
struct State recurse(struct State input, int count) { if (count == 0) return input; input.total += count; input.scale += 0.25; input.calls += 1; return recurse(input,count-1); }
int main(void) { struct State input = {1,2.5,0}; struct State result = recurse(input,9); return result.total != 46 || result.scale != 4.75 || result.calls != 9; }
