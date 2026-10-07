int add(int x) { return x+1; } int sub(int x) { return x-1; } int main(void) { return _Generic(1,int:add,default:sub)(7)-8; }
