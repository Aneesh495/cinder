
int main(void) { float a=1.25f; float b=2.5f; for(int i=0;i<3;i++) { float t=a; a=b; b=t; } return a!=2.5f || b!=1.25f; }
