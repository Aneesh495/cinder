struct Pair { double fraction; long whole; };
typedef struct Pair (*Callback)(struct Pair, long);
struct Pair update(struct Pair input, long delta) { input.whole += delta; input.fraction -= 0.25; return input; }
struct Pair invoke(Callback callback, struct Pair input) { return callback(input, 7); }
int main(void) { struct Pair input = {1.5,11}; struct Pair result = invoke(update,input); return result.fraction != 1.25 || result.whole != 18; }
