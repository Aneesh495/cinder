struct Pair { double left; double right; };
struct Pair change(struct Pair input) { input.left += 0.25; input.right -= 0.5; return input; }
int main(void) { struct Pair input = {1.5, -8.25}; struct Pair result = change(input); return result.left != 1.75 || result.right != -8.75 || input.left != 1.5; }
