float value(float x) { return x+1.25f; } float (*p)(float)=value; int main(void) { return (int)(p(2.5f)*4.0f); }
