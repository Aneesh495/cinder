int execute(int seed) { int pc = 0; int value = seed; while (pc < 6) { if ((pc & 1) == 0) value = value + pc; else value = value ^ (pc * 3); pc++; } return value & 255; }
int main(void) { return execute(11); }
