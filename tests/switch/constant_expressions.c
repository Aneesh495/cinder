int main(void) { int n=0; switch(20) { case sizeof(long)+_Alignof(int)*3: n=5; break; case (int)2.0: n=7; } return n!=5; }
