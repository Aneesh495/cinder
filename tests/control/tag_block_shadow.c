struct Entry { int value; };
int main(void) {
    int sum = (int)sizeof(struct Entry);
    { struct Entry { double first, second; }; sum += (int)sizeof(struct Entry); }
    return sum != 20 || sizeof(struct Entry) != 4;
}
