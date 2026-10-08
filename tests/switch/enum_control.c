enum E { A=-3, B=8, C=19 }; int pick(enum E x) { switch(x) { case A: return B; case B: return C; default: return A; } } int main(void) { return pick(A)!=B || pick(B)!=C || pick(C)!=A; }
