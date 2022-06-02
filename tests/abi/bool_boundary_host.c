#include <stdio.h>

int abi_poisoned_arguments(void);
int cinder_bool_results(void);

int main(void) {
    int arguments = abi_poisoned_arguments();
    int results = cinder_bool_results();
    printf("arguments=%d results=%d\n", arguments, results);
    return arguments != 1 || results != 10;
}
