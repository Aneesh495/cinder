_Noreturn void stop(void);
_Noreturn void halt(int condition) { if (condition) stop(); else for (;;) { } }
int guarded(int n) { if (n < 0) halt(n); return n + 1; }
