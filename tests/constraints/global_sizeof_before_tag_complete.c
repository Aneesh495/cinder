struct Data; struct Data *p; int read(void) { return sizeof(*p); } struct Data { int x; };
