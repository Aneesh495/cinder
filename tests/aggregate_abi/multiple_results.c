struct Pair { long first; double second; };
struct Pair build(long first, double second) { struct Pair result = {first,second}; return result; }
long combine(struct Pair left, struct Pair right) { return left.first*3 + right.first*5 + (long)(left.second + right.second); }
int main(void) { return combine(build(7,1.25),build(11,2.75)) != 80; }
