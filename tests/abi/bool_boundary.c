typedef _Bool (*Flag)(void);
_Bool abi_return_false(void);
_Bool abi_return_true(void);

int cinder_bool_argument(_Bool value) { return value; }
int cinder_bool_stack(_Bool a, _Bool b, _Bool c, _Bool d, _Bool e, _Bool f, _Bool g) {
    return a + 2 * b + 4 * c + 8 * d + 16 * e + 32 * f + 64 * g;
}
int cinder_bool_results(void) {
    Flag false_callback = abi_return_false;
    Flag true_callback = abi_return_true;
    return abi_return_false() + 2 * abi_return_true() + 4 * false_callback() + 8 * true_callback();
}
