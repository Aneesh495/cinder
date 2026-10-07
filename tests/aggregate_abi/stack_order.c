struct Pair { long left; long right; };
struct Large { long fields[3]; };
long sum(long a, long b, long c, long d, long e, long f, struct Pair pair, long seventh, struct Large large, long eighth) { return a+b+c+d+e+f + pair.left*2 + pair.right*3 + seventh*5 + large.fields[0]*7 + large.fields[1]*11 + large.fields[2]*13 + eighth*17; }
int main(void) { struct Pair pair = {7,11}; struct Large large = {{19,23,29}}; return sum(1,2,3,4,5,6,pair,13,large,31) != 1423; }
