int main(void) { return sizeof (int[]){sizeof (char[16777216]){1},sizeof (char[16777216]){2}} != 2 * sizeof(int); }
