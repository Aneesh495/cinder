struct S{int n;int data[];};union U{struct S s;int n;};struct T{union U u;};
