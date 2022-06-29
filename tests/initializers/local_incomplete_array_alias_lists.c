typedef int A[]; int main(void) { A a={1,2}; A b={3,4,5}; return sizeof(a)+sizeof(b)+b[2]; }
