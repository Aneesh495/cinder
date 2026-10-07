struct Pair { float left; float right; };
struct Pair change(struct Pair input) { float original = input.left; input.left = input.right * 2.0f; input.right = original - 0.25f; return input; }
int main(void) { struct Pair input = {1.5f, -8.25f}; struct Pair result = change(input); return result.left != -16.5f || result.right != 1.25f; }
