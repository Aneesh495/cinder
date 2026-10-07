struct First { const long value; };
long consume(struct First input) { input.value = 7; return input.value; }
