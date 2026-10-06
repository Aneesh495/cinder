int data[3]; int *p=&data[2]-1; int main(void) { data[1]=67; return *p; }
