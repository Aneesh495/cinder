enum { Rows = 2, Columns = Rows + 3 };
typedef int Matrix[Rows][Columns];
int main(void) { Matrix matrix; return sizeof(matrix) != 40; }
