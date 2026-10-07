struct Pair { double left; double right; };
double sum(double a, double b, double c, double d, double e, double f, double g, struct Pair pair, double last) { return a+b+c+d+e+f+g + pair.left*2 + pair.right*3 + last*5; }
int main(void) { struct Pair pair = {7,11}; return sum(1,2,3,4,5,6,7,pair,13) != 140; }
