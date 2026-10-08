static int calls;int tick(void){++calls;return calls;}int main(void){(void)tick();return calls;}
