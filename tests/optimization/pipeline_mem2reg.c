static int choose(int condition) {
    int value = 3;
    if (condition != 0) value = 13;
    else value = 17;
    return value;
}
int main(void) { return choose(0); }
