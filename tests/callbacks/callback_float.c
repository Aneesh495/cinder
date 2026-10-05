float blend(float a, float b) { return a * 2.0f + b; } int main(void) { float (*fn)(float,float) = blend; return (int)(fn(1.5f,6.25f) * 4.0f); }
