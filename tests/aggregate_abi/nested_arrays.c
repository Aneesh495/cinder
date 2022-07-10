struct Inner { float values[2]; };
struct Outer { struct Inner rows[2]; };
struct Outer transpose(struct Outer input) { float saved = input.rows[0].values[1]; input.rows[0].values[1] = input.rows[1].values[0]; input.rows[1].values[0] = saved; return input; }
int main(void) { struct Outer input = {{{{1,2}},{{3,4}}}}; struct Outer result = transpose(input); return result.rows[0].values[1] != 3 || result.rows[1].values[0] != 2 || result.rows[1].values[1] != 4; }
