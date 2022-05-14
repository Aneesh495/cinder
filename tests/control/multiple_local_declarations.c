int main(void) {
    int first = 5, second = first + 3, third = second * 2;
    double fraction = 1.5, total = fraction + third;
    return first != 5 || second != 8 || third != 16 || total != 17.5;
}
