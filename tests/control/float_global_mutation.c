float value = 1.5f;
double other = 2.25;
void update(void) { value += 0.25f; other *= 2.0; }
int main(void) { update(); return value != 1.75f || other != 4.5; }
