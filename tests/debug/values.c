volatile int value_sink;
int value_global = 11;

int debug_initialize(int seed) {
    int pending;
    value_sink = seed; /* before-init */
    pending = seed + 2;
    value_sink = pending; /* after-init */
    return pending;
}

int debug_partial(int flag) {
    int partial;
    if (flag) partial = 3;
    value_sink = flag; /* partial-join */
    return flag;
}

int debug_complete(int flag) {
    int complete;
    if (flag) complete = 3;
    else complete = 4;
    value_sink = complete; /* complete-join */
    return complete;
}

int debug_loop(void) {
    for (int iteration = 0; iteration < 2; ++iteration) {
        int fresh;
        value_sink = iteration; /* loop-before */
        fresh = iteration + 1;
        value_sink = fresh; /* loop-after */
    }
    return 0;
}

void debug_update(int *pointer) { *pointer = 5; }

int debug_escaped(void) {
    int escaped = 4;
    debug_update(&escaped);
    value_sink = escaped; /* escaped-address */
    return escaped;
}

int debug_pointer(void) {
    int *pointer = &value_global;
    value_sink = *pointer; /* pointer-value */
    return *pointer;
}

int main(void) {
    return debug_initialize(5) + debug_partial(1) + debug_partial(0)
           + debug_complete(1) + debug_complete(0) + debug_loop()
           + debug_escaped() + debug_pointer();
}
