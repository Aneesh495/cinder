int main(void) { int n=0; while(1) { goto stop; ++n; stop: break; } return n!=0; }
