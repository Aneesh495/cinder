int first(void) { _Noreturn void absent(void); return sizeof(&absent)!=8; } int second(void) { void absent(void); return sizeof(&absent)!=8; } int main(void) { return first() || second(); }
