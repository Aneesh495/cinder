union U { unsigned :3; unsigned a:7; int n; }; int main(void){union U u={79};return u.a!=79 || sizeof(u)!=4;}
