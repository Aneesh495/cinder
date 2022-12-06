int main(void) {
    int values[3] = {5, 7, 9};
    int index = 0;
    values[index++] += 4;
    int previous = values[index++]++;
    return values[0] + values[1] + previous + index;
}
