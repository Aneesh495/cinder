int main(void) { int x=43,y=47; int *data[2]={&x,&y}; return *data[0]+*data[1]; }
