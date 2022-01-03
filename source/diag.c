#include "cinder.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void cinder_diags_init(CinderDiagnostics *diags) {
    diags->items.data = NULL;
    diags->items.len = 0U;
    diags->items.cap = 0U;
    diags->errors = 0U;
    diags->warnings = 0U;
    diags->json = false;
}

void cinder_diags_destroy(CinderDiagnostics *diags) {
    for (size_t i = 0U; i < diags->items.len; ++i) {
        free(diags->items.data[i].message);
    }
    free(diags->items.data);
    diags->items.data = NULL;
    diags->items.len = 0U;
    diags->items.cap = 0U;
}

void cinder_diag(CinderDiagnostics *diags, CinderSeverity severity, CinderLoc loc, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0U, fmt, copy);
    va_end(copy);
    if (length < 0) {
        length = 0;
    }
    char *message = cinder_alloc((size_t)length + 1U);
    (void)vsnprintf(message, (size_t)length + 1U, fmt, args);
    va_end(args);
    CinderDiagnostic item;
    item.severity = severity;
    item.loc = loc;
    item.message = message;
    cinder_vec_push((CinderVec *)&diags->items, &item);
    if (severity >= CINDER_ERROR) {
        diags->errors++;
    } else if (severity == CINDER_WARNING) {
        diags->warnings++;
    }
}

static const char *severity_name(CinderSeverity severity) {
    switch (severity) {
        case CINDER_NOTE: return "note";
        case CINDER_WARNING: return "warning";
        case CINDER_ERROR: return "error";
        case CINDER_FATAL: return "fatal";
    }
    return "diagnostic";
}

void cinder_diag_print(CinderDiagnostics *diags, CinderSourceManager *sources, FILE *out) {
    for (size_t i = 0U; i < diags->items.len; ++i) {
        CinderDiagnostic *item = &diags->items.data[i];
        CinderLoc loc = item->loc;
        cinder_loc_linecol(sources, &loc);
        fprintf(out, "%s:%u:%u: %s: %s\n", cinder_source_name(sources, loc.file), loc.line, loc.column, severity_name(item->severity), item->message);
    }
}
