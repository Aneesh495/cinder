static int values[3] = {2, 5, 7};
static int *anchor = &values[1];
static int total(int count) {
    static int bias = 3;
    int result = bias;
    for (int i = 0; i < count; ++i) result += values[i];
    if (count < 0) goto end;
    switch (count) {
        case 3: result += *anchor; break;
        default: result -= 1; break;
    }
end:
    return result;
}
int main(void) { return total(3) + 4; }
