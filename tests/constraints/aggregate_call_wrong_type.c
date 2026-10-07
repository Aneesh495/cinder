struct First { long value; };
struct Second { long value; };
long consume(struct First input) { return input.value; }
int main(void) { struct Second input = {7}; return (int)consume(input); }
