struct Pair { long first; long second; };
long consume(struct Pair input) { return input.first + input.second; }
int main(void) { struct Pair input; input.first = 7; return (int)consume(input); }
