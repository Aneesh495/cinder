struct Large { long fields[3]; };
struct Large build(long a, long b, long c, long d, long e, long f, long g, double x, double y) { struct Large result = {{a+b+c, d+e+f+g, (long)(x+y)}}; return result; }
int main(void) { struct Large result = build(1,2,3,4,5,6,7,8.5,9.5); return result.fields[0] != 6 || result.fields[1] != 22 || result.fields[2] != 18; }
