int calls; int inc(void) { ++calls; return 1; } int main(void) { _Static_assert(sizeof (int[]){inc(),inc()}==8,"unevaluated initializer"); return calls; }
