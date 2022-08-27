union U { int n;double x; };int main(void) { register union U u={3};return &u.n!=0; }
