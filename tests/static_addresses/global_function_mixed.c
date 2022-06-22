double value(int x,float y,long z,double w) { return x+y+z+w; } double (*p)(int,float,long,double)=value; int main(void) { return (int)p(3,2.5f,5L,1.5); }
