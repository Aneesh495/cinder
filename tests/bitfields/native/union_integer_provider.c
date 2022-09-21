union S { unsigned a:7; int n; };
union S reference(union S (*callback)(union S),union S s,int a,int b,int c,int d,int e,int f,int g){if(a+b+c+d+e+f+g!=28)return s;s=callback(s);return callback(s);}
