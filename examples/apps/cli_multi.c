int parse_digit(int value) { if (value < 48) return 0; if (value > 57) return 0; return value - 48; }
int command_score(int a, int b) { return (parse_digit(a) * 10 + parse_digit(b)) & 255; }
int main(void) { return command_score(52, 55); }
