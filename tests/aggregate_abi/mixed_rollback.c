struct Mixed { long whole; double fraction; };
double sum(double a, double b, double c, double d, double e, double f, double g, double h, struct Mixed mixed, long last) { return a+b+c+d+e+f+g+h + mixed.whole*2 + mixed.fraction*3 + last*5; }
int main(void) { struct Mixed mixed = {7,11}; return sum(1,2,3,4,5,6,7,8,mixed,13) != 148; }
