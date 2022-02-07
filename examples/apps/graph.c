int path_cost(int nodes) { int total = 0; int i = 1; while (i <= nodes) { total = total + ((i * 7) & 31); i++; } return total & 255; }
int main(void) { return path_cost(9); }
