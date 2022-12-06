struct pair { int left; int right; };
static struct pair make(int value) {
    struct pair result = {value, value * 2};
    return result;
}
int main(void) { return make(3).right + make(4).left; }
