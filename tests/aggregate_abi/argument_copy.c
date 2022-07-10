struct Row { long values[3]; };
long consume(struct Row input, struct Row *original) { input.values[0] = 101; original->values[1] = 37; return input.values[0] + input.values[1]; }
int main(void) { struct Row input = {{3,7,11}}; long result = consume(input, &input); return result != 108 || input.values[0] != 3 || input.values[1] != 37; }
