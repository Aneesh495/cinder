typedef int finish; int main(void) { finish n=3; goto finish; n=7; finish: return n!=3; }
