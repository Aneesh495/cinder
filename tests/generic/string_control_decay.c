int main(void) { return _Generic("abc",char*:1,char[4]:2,default:3)-1; }
