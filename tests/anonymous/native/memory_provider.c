struct S{struct{long a,b;};union{long c;long other;};};
struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g){if(a+b+c+d+e+f+g!=28)return s;s=callback(s);return callback(s);}
