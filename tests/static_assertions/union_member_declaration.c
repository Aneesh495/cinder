union U { int a; _Static_assert(sizeof(double)==8,"union scalar"); double b; }; int main(void) { union U u={.b=1.5}; return sizeof u!=8 || u.b!=1.5; }
