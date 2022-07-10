struct Pair { long left; long right; };
long sum(long a, long b, long c, long d, long e, struct Pair pair, long last) { return a+b+c+d+e + pair.left*2 + pair.right*3 + last*5; }
int main(void) { struct Pair pair = {7,11}; return sum(1,2,3,4,5,pair,13) != 127; }
