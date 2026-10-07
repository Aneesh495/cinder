struct Row { long values[3]; };
struct Row create(void) { struct Row result = {{7,11,13}}; return result; }
int main(void) { long *pointer = create().values; return (int)pointer[1]; }
