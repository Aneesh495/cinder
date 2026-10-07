union U{_Alignas(16) int x; double y;}; int main(void){union U u={7}; return _Alignof(union U)!=16 || sizeof u!=16 || (unsigned long)&u%16 || u.x!=7;}
