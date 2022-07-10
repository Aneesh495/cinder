struct Row { long values[3]; };
struct Row build(long first) { struct Row result = {{first,first+1,first+2}}; return result; }
int main(void) { return build(41).values[2] != 43 || build(7).values[0] != 7; }
