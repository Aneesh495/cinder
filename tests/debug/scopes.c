volatile int scope_sink;

int scope_call(int input) { return input; }

int scope_probe(int flag) {
    int value = 11;
    int total = value;
    {
        scope_sink = total + value; /* before-shadow */
        total += value;
        double value = 2.5;
        scope_sink = scope_call((int)value); /* after-shadow */
        total += (int)value;
        {
            unsigned char value = 3;
            scope_sink = value; /* inner-shadow */
            total += value;
        }
        scope_sink = (int)value; /* after-inner */
        total += (int)value;
    }
    scope_sink = value; /* after-block */
    total += value;
    if (flag) { long branch = 5; total += (int)branch; }
    else { short branch = 7; total += branch; }
    for (int iteration = 0; iteration < 2; ++iteration) {
        int loop_value = iteration;
        scope_sink = loop_value; /* loop-value */
        total += loop_value;
    }
    return total;
}

int main(void) { return scope_probe(0) + scope_probe(1); }
