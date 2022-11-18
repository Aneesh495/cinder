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
    if (fmt == NULL) fmt = "internal diagnostic format is missing";
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

static void json_string(FILE *out, const char *text) {
    size_t invalid = 0U; bool utf8 = cinder_utf8_validate(text, strlen(text), &invalid);
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p != 0U; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', out); fputc(*p, out); }
        else if (*p < 32U || (!utf8 && *p >= 128U)) fprintf(out, "\\u%04x", (unsigned)*p);
        else fputc(*p, out);
    }
    fputc('"', out);
}

static void json_range(FILE *out, CinderSourceManager *sources, CinderLoc loc) {
    CinderSourceFile *file = cinder_source_get(sources, loc.file);
    if (file == NULL) { fputs("null", out); return; }
    if (loc.offset > file->size) loc.offset = file->size;
    if (loc.length > file->size - loc.offset) loc.length = file->size - loc.offset;
    CinderLoc end = cinder_loc(loc.file, loc.offset + loc.length, 0U);
    cinder_loc_linecol(sources, &loc); cinder_loc_linecol(sources, &end);
    fputs("{\"file\":", out); json_string(out, cinder_loc_name(sources, loc));
    fprintf(out, ",\"offset\":%zu,\"length\":%zu,\"line\":%u,\"column\":%u,\"end_line\":%u,\"end_column\":%u}", loc.offset, loc.length, loc.line, loc.column, end.line, end.column);
}

static const CinderSourceSpan *macro_span(const CinderSourceManager *sources, CinderLoc loc) {
    for (size_t p = 0U; p < sources->spans.len; ++p) {
        const CinderSourceSpan *span = &sources->spans.data[p];
        if (span->expansion.file == loc.file && span->expansion.offset == loc.offset && span->definition.file != CINDER_NO_FILE) return span;
    }
    return NULL;
}

static void print_json(CinderDiagnostics *diags, CinderSourceManager *sources, FILE *out) {
    fputs("{\"schema\":1,\"tool\":\"cindercc\",\"diagnostics\":[", out);
    for (size_t i = 0U; i < diags->items.len; ++i) {
        const CinderDiagnostic *item = &diags->items.data[i];
        if (i != 0U) fputc(',', out);
        fputs("{\"severity\":", out); json_string(out, severity_name(item->severity));
        fputs(",\"message\":", out); json_string(out, item->message);
        fputs(",\"range\":", out); json_range(out, sources, item->loc);
        const CinderSourceSpan *span = macro_span(sources, item->loc);
        fputs(",\"macro_definition\":", out); if (span != NULL) json_range(out, sources, span->definition); else fputs("null", out);
        fputc('}', out);
    }
    fputs("]}\n", out);
}

void cinder_diag_print(CinderDiagnostics *diags, CinderSourceManager *sources, FILE *out) {
    if (diags->json) { print_json(diags, sources, out); return; }
    for (size_t i = 0U; i < diags->items.len; ++i) {
        CinderDiagnostic *item = &diags->items.data[i];
        CinderLoc loc = item->loc;
        cinder_loc_linecol(sources, &loc);
        fprintf(out, "%s:%u:%u: %s: %s\n", cinder_loc_name(sources, loc), loc.line, loc.column, severity_name(item->severity), item->message);
        for (size_t p = 0U; p < sources->spans.len; ++p) {
            const CinderSourceSpan *span = &sources->spans.data[p];
            if (span->expansion.file == item->loc.file && span->expansion.offset == item->loc.offset && span->definition.file != CINDER_NO_FILE) {
                CinderLoc definition = span->definition;
                cinder_loc_linecol(sources, &definition);
                fprintf(out, "%s:%u:%u: note: expanded from macro defined here\n", cinder_loc_name(sources, definition), definition.line, definition.column);
                break;
            }
        }
    }
}
