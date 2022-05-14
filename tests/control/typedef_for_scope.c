typedef long Index;
int main(void) {
    int sum = 0;
    for (int Index = 0; Index < 4; ++Index) sum += Index;
    Index last = 8;
    return sum + last != 14;
}
