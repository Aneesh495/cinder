struct Large { long fields[3]; };
typedef struct Large (*Callback)(struct Large, long);
struct Large update(struct Large input, long delta) { input.fields[0] += delta; input.fields[2] -= delta; return input; }
struct Large invoke(Callback callback, struct Large input) { return callback(input, 7); }
int main(void) { struct Large input = {{11,22,33}}; struct Large result = invoke(update,input); return result.fields[0] != 18 || result.fields[1] != 22 || result.fields[2] != 26; }
