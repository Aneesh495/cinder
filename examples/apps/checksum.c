int checksum(int n) { int total = 0; while (n > 0) { total = (total + n) & 255; n = n - 1; } return total; }
int main(void) { return checksum(12); }
