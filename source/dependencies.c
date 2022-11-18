#define _XOPEN_SOURCE 700
#include "cinder.h"
#include <stdlib.h>
#include <string.h>

char *cinder_dependency_path(const char *target) {
    size_t length = strlen(target);
    const char *slash = strrchr(target, '/'), *dot = strrchr(target, '.');
    if (dot != NULL && (slash == NULL || dot > slash + 1) && dot != target) length = (size_t)(dot - target);
    if (length > SIZE_MAX - 3U) return NULL;
    char *path = cinder_alloc(length + 3U);
    memcpy(path, target, length); memcpy(path + length, ".d", 3U); return path;
}

static char *destination_name(const char *path) {
    const char *slash = strrchr(path, '/');
    size_t directory = slash == NULL ? 1U : slash == path ? 1U : (size_t)(slash - path);
    char *parent = slash == NULL ? cinder_strndup(".", 1U) : cinder_strndup(path, directory);
    char *resolved = realpath(parent, NULL); free(parent);
    if (resolved == NULL) return NULL;
    const char *base = slash == NULL ? path : slash + 1;
    size_t n = strlen(resolved), m = strlen(base);
    if (n > SIZE_MAX - m - 2U) { free(resolved); return NULL; }
    char *result = cinder_alloc(n + m + 2U);
    memcpy(result, resolved, n); result[n] = '/'; memcpy(result + n + 1U, base, m + 1U);
    free(resolved); return result;
}

bool cinder_output_same_path(const char *left, const char *right) {
    if (strcmp(left, right) == 0) return true;
    char *existing_left = realpath(left, NULL), *existing_right = realpath(right, NULL);
    bool existing = existing_left != NULL && existing_right != NULL && strcmp(existing_left, existing_right) == 0;
    free(existing_left); free(existing_right); if (existing) return true;
    char *a = destination_name(left), *b = destination_name(right);
    bool same = a != NULL && b != NULL && strcmp(a, b) == 0;
    free(a); free(b); return same;
}

static bool representable(const char *path) {
    return strchr(path, '\n') == NULL && strchr(path, '\r') == NULL && *path != '\0';
}

static void make_path(FILE *out, const char *path) {
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '$') fputc('$', out);
        else if (*p == ' ' || *p == '\t' || *p == '#' || *p == '\\' || *p == ':') fputc('\\', out);
        fputc((unsigned char)*p, out);
    }
}

static int compare_paths(const void *left, const void *right) {
    const char *const *a = left, *const *b = right; return strcmp(*a, *b);
}

int cinder_write_dependencies(CinderSourceManager *sources, const char *target, FILE *out, CinderDiagnostics *diags) {
    if (!representable(target) || sources->files.len == 0U || sources->files.len > SIZE_MAX / sizeof(char *)) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "dependency target or source inventory cannot be represented"); return 1;
    }
    char **paths = cinder_alloc(sources->files.len * sizeof(*paths));
    for (size_t i = 0U; i < sources->files.len; ++i) {
        paths[i] = sources->files.data[i].path;
        if (!sources->files.data[i].valid || !representable(paths[i])) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "dependency source cannot be represented: %s", paths[i]); free(paths); return 1;
        }
    }
    /* Root first; canonical included files follow in deterministic order. */
    if (sources->files.len > 1U) qsort(paths + 1U, sources->files.len - 1U, sizeof(*paths), compare_paths);
    make_path(out, target); fputc(':', out);
    for (size_t i = 0U; i < sources->files.len; ++i) {
        if (i > 1U && strcmp(paths[i], paths[i - 1U]) == 0) continue;
        fputc(' ', out); make_path(out, paths[i]);
    }
    fputc('\n', out); free(paths);
    if (ferror(out) != 0) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot write complete dependency output"); return 1; }
    return 0;
}
