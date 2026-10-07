struct P; int main(void) { return _Generic(1,struct P:1,default:2); }
