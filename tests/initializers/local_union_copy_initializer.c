union U { long x; double y; }; int main(void) { union U u={.y=7.0}; union U v=u; return (int)v.y; }
