#include "cinder.h"

/* Machine lowering is intentionally kept in x86_64.c. This module is the
 * stable boundary for future target-independent MIR legality checks. */
int cinder_mir_boundary(const CinderIRFunction *function, CinderDiagnostics *diags) {
    if (function == NULL || function->blocks.len == 0U) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "cannot lower an empty IR function to MIR");
        return 1;
    }
    return 0;
}
