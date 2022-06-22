char data[]="abc"; void *p=(void *)&data[1]; int main(void) { return *(char *)p; }
