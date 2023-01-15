int global_values[5] = {2,3,5,7,11};
int get(const int *pointer) { return *pointer; }

int main(void) {
    int local[5] = {13,17,19,23,29};
    int *p0 = global_values, *p1 = global_values + 1;
    int *p2 = global_values + 2, *p3 = global_values + 3, *p4 = global_values + 4;
    int *p5 = local, *p6 = local + 1, *p7 = local + 2;
    int *p8 = local + 3, *p9 = local + 4;
    return get(p0) + get(p1) + get(p2) + get(p3) + get(p4)
           + get(p5) + get(p6) + get(p7) + get(p8) + get(p9)
           + (int)(p4 - p0);
}
