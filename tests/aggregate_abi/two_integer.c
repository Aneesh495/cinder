struct Pair { long left; long right; };
struct Pair rotate(struct Pair input, long extra) { struct Pair result = {input.right + extra, input.left - extra}; return result; }
int main(void) { struct Pair input = {4886691840L, -17L}; struct Pair result = rotate(input, 9); return result.left != -8 || result.right != 4886691831L; }
