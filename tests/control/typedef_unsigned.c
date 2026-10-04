typedef unsigned long Word;
int main(void) {
    Word value = 18446744073709551615UL;
    return value / 3UL != 6148914691236517205UL;
}
