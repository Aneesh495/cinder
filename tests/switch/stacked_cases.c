int pick(int x) { switch(x) { case 2: case 3: case 5: return 1; default: return 0; } } int main(void) { return !pick(2) || !pick(3) || !pick(5) || pick(4); }
