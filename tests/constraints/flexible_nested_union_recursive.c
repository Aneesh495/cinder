struct S{int n;int data[];};union U{struct S s;int n;};union V{union U u;int n;};struct T{union V v;};
