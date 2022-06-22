int data[3]; int (*p)[3]=&data; int main(void) { data[1]=71; return (*p)[1]; }
