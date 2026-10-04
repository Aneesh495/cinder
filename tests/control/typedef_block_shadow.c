typedef int Number;
int main(void) {
    Number value = 7;
    { typedef double Number; Number value = 3.5; if (value != 3.5) return 1; }
    Number second = 11;
    return value + second != 18;
}
