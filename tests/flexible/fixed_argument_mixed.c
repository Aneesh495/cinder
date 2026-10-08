struct S{long n; double weight; char data[];};static int read(struct S s){return s.n==17&&s.weight==2.5;}int main(void){struct S s={17,2.5};return !read(s);}
