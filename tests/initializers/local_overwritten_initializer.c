int calls; int effect(void) { ++calls; return 7; } int main(void) { int data[2]={[0]=effect(),[0]=11}; return data[0]; }
