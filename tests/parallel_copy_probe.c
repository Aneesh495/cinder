#include "cinder.h"
#include <stdlib.h>

int main(void) {
    CinderValueId sources[] = {1U, 2U};
    CinderValueId destinations[] = {2U, 1U};
    CinderDiagnostics diagnostics;
    cinder_diags_init(&diagnostics);
    CinderParallelCopyPlan plan;
    cinder_parallel_copy_init(&plan);
    int result = cinder_resolve_parallel_copies(sources, destinations, 2U, &plan, &diagnostics);
    int valid = result == 0 && diagnostics.errors == 0U && plan.moves.len == 3U && plan.temporary_count == 1U && plan.moves.data[0].destination == CINDER_INVALID_VALUE;
    cinder_parallel_copy_destroy(&plan);
    cinder_diags_destroy(&diagnostics);
    return valid ? 0 : 1;
}
