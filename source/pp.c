#include "cinder.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Token-aware enough for ordinary object-like and function-like macros. The
 * preprocessor keeps directive state and rescans replacement identifiers; it
 * never performs a blind global text replacement. */
typedef struct {
    char *name;
    char *replacement;
    CINDER_VEC_TYPE(char *) parameters;
    bool function_like;
    bool variadic;
} CinderMacro;

typedef struct {
    CINDER_VEC_TYPE(CinderMacro) macros;
    CINDER_VEC_TYPE(char *) include_dirs;
    CinderSourceManager *sources;
    CinderDiagnostics *diags;
    unsigned depth;
} CinderPP;

static void string_append(char **buffer, size_t *length, size_t *capacity, const char *text, size_t text_len) {
    if (text_len > SIZE_MAX - *length - 1U) abort();
    size_t required = *length + text_len + 1U;
    if (required > *capacity) {
        size_t next = *capacity == 0U ? 256U : *capacity;
        while (next < required) next *= 2U;
        *buffer = cinder_realloc(*buffer, next);
        *capacity = next;
    }
    memcpy(*buffer + *length, text, text_len);
    *length += text_len;
    (*buffer)[*length] = '\0';
}

static CinderMacro *macro_find(CinderPP *pp, const char *name, size_t length) {
    for (size_t i = pp->macros.len; i > 0U; --i) {
        CinderMacro *macro = &pp->macros.data[i - 1U];
        if (strlen(macro->name) == length && memcmp(macro->name, name, length) == 0) return macro;
    }
    return NULL;
}

static void macro_free(CinderMacro *macro) {
    free(macro->name);
    free(macro->replacement);
    for (size_t i = 0U; i < macro->parameters.len; ++i) free(macro->parameters.data[i]);
    free(macro->parameters.data);
}

static const char *skip_space(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
    return p;
}

static bool identifier_at(const char *p, const char **end) {
    if (!(isalpha((unsigned char)*p) != 0 || *p == '_')) return false;
    const char *q = p + 1;
    while (isalnum((unsigned char)*q) != 0 || *q == '_') ++q;
    *end = q;
    return true;
}

static int macro_parameter(CinderMacro *macro, const char *name, size_t length) {
    for (size_t i = 0U; i < macro->parameters.len; ++i) {
        if (strlen(macro->parameters.data[i]) == length && memcmp(macro->parameters.data[i], name, length) == 0) return (int)i;
    }
    return -1;
}

static void expand_text(CinderPP *pp, const char *line, size_t line_len, char **out, size_t *out_len, size_t *out_cap, unsigned depth);

static void expand_macro(CinderPP *pp, CinderMacro *macro, const char **after, const char *p, char **out, size_t *out_len, size_t *out_cap, unsigned depth) {
    if (!macro->function_like) {
        expand_text(pp, macro->replacement, strlen(macro->replacement), out, out_len, out_cap, depth + 1U);
        *after = p;
        return;
    }
    const char *open = skip_space(p);
    if (*open != '(') {
        string_append(out, out_len, out_cap, macro->name, strlen(macro->name));
        *after = p;
        return;
    }
    open++;
    CINDER_VEC_TYPE(char *) args = {NULL, 0U, 0U};
    const char *cursor = open;
    const char *arg_start = cursor;
    unsigned nesting = 0U;
    while (*cursor != '\0') {
        if (*cursor == '(') nesting++;
        if (*cursor == ')') {
            if (nesting == 0U) {
                size_t n = (size_t)(cursor - arg_start);
                char *arg = cinder_strndup(arg_start, n);
                cinder_vec_push((CinderVec *)&args, &arg);
                ++cursor;
                break;
            }
            nesting--;
        }
        if (*cursor == ',' && nesting == 0U) {
            size_t n = (size_t)(cursor - arg_start);
            char *arg = cinder_strndup(arg_start, n);
            cinder_vec_push((CinderVec *)&args, &arg);
            arg_start = cursor + 1;
        }
        ++cursor;
    }
    if (cursor == open || args.len == 0U) {
        string_append(out, out_len, out_cap, macro->name, strlen(macro->name));
        for (size_t i = 0U; i < args.len; ++i) free(args.data[i]);
        free(args.data);
        *after = p;
        return;
    }
    const char *r = macro->replacement;
    while (*r != '\0') {
        const char *end = NULL;
        if (identifier_at(r, &end)) {
            int index = macro_parameter(macro, r, (size_t)(end - r));
            if (index >= 0 && (size_t)index < args.len) {
                expand_text(pp, args.data[index], strlen(args.data[index]), out, out_len, out_cap, depth + 1U);
            } else if (index >= 0 && macro->variadic && (size_t)index >= macro->parameters.len - 1U) {
                for (size_t i = (size_t)index; i < args.len; ++i) {
                    if (i != (size_t)index) string_append(out, out_len, out_cap, ",", 1U);
                    expand_text(pp, args.data[i], strlen(args.data[i]), out, out_len, out_cap, depth + 1U);
                }
            } else {
                string_append(out, out_len, out_cap, r, (size_t)(end - r));
            }
            r = end;
        } else {
            string_append(out, out_len, out_cap, r, 1U);
            ++r;
        }
    }
    for (size_t i = 0U; i < args.len; ++i) free(args.data[i]);
    free(args.data);
    *after = cursor;
}

static void expand_text(CinderPP *pp, const char *line, size_t line_len, char **out, size_t *out_len, size_t *out_cap, unsigned depth) {
    if (depth > 64U) {
        cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(CINDER_NO_FILE, 0U, 0U), "macro expansion exceeded the recursion limit");
        return;
    }
    size_t i = 0U;
    while (i < line_len) {
        const char *end = NULL;
        if (identifier_at(line + i, &end)) {
            CinderMacro *macro = macro_find(pp, line + i, (size_t)(end - (line + i)));
            if (macro != NULL) {
                const char *after = end;
                expand_macro(pp, macro, &after, end, out, out_len, out_cap, depth);
                i = (size_t)(after - line);
                continue;
            }
            string_append(out, out_len, out_cap, line + i, (size_t)(end - (line + i)));
            i = (size_t)(end - line);
            continue;
        }
        string_append(out, out_len, out_cap, line + i, 1U);
        ++i;
    }
}

static void macro_define(CinderPP *pp, const char *text) {
    const char *p = skip_space(text);
    const char *end = NULL;
    if (!identifier_at(p, &end)) {
        cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(CINDER_NO_FILE, 0U, 0U), "expected macro name after #define");
        return;
    }
    CinderMacro macro;
    macro.name = cinder_strndup(p, (size_t)(end - p));
    macro.replacement = NULL;
    macro.parameters.data = NULL;
    macro.parameters.len = 0U;
    macro.parameters.cap = 0U;
    macro.function_like = *end == '(';
    macro.variadic = false;
    p = end;
    if (macro.function_like) {
        ++p;
        while (*p != '\0' && *p != ')') {
            p = skip_space(p);
            if (*p == '.') {
                macro.variadic = true;
                while (*p != '\0' && *p != ')') ++p;
                break;
            }
            if (!identifier_at(p, &end)) break;
            char *parameter = cinder_strndup(p, (size_t)(end - p));
            cinder_vec_push((CinderVec *)&macro.parameters, &parameter);
            p = skip_space(end);
            if (*p == ',') ++p;
        }
        if (*p == ')') ++p;
    }
    p = skip_space(p);
    if (*p == '=') p = skip_space(p + 1);
    macro.replacement = cinder_strndup(p, strlen(p));
    for (size_t i = 0U; i < pp->macros.len; ++i) {
        if (strcmp(pp->macros.data[i].name, macro.name) == 0) {
            macro_free(&pp->macros.data[i]);
            pp->macros.data[i] = macro;
            return;
        }
    }
    cinder_vec_push((CinderVec *)&pp->macros, &macro);
}

static bool pp_condition(CinderPP *pp, const char *text) {
    const char *p = skip_space(text);
    const char *end = NULL;
    if (strncmp(p, "defined", 7U) == 0 && !isalnum((unsigned char)p[7]) && p[7] != '_') {
        p = skip_space(p + 7);
        if (*p == '(') ++p;
        if (identifier_at(p, &end)) return macro_find(pp, p, (size_t)(end - p)) != NULL;
    }
    if (identifier_at(p, &end)) {
        CinderMacro *macro = macro_find(pp, p, (size_t)(end - p));
        if (macro != NULL && !macro->function_like) return pp_condition(pp, macro->replacement);
    }
    char *end_number = NULL;
    long value = strtol(p, &end_number, 0);
    return end_number != p && value != 0L;
}

static bool read_include(CinderPP *pp, const char *current_path, const char *spec, char **resolved) {
    const char *p = skip_space(spec);
    char closing = *p == '<' ? '>' : '"';
    if (*p != '<' && *p != '"') return false;
    ++p;
    const char *end = strchr(p, closing);
    if (end == NULL) return false;
    if (*p == '/' || (current_path != NULL && *current_path != '\0')) {
        char candidate[4096];
        const char *slash = strrchr(current_path, '/');
        size_t prefix = slash == NULL ? 0U : (size_t)(slash - current_path + 1);
        if (prefix + (size_t)(end - p) + 1U < sizeof(candidate)) {
            memcpy(candidate, current_path, prefix);
            memcpy(candidate + prefix, p, (size_t)(end - p));
            candidate[prefix + (size_t)(end - p)] = '\0';
            FILE *probe = fopen(candidate, "rb");
            if (probe != NULL) {
                fclose(probe);
                *resolved = cinder_strndup(candidate, strlen(candidate));
                return true;
            }
        }
    }
    for (size_t i = 0U; i < pp->include_dirs.len; ++i) {
        char candidate[4096];
        int written = snprintf(candidate, sizeof(candidate), "%s/%.*s", pp->include_dirs.data[i], (int)(end - p), p);
        if (written > 0 && (size_t)written < sizeof(candidate)) {
            FILE *probe = fopen(candidate, "rb");
            if (probe != NULL) {
                fclose(probe);
                *resolved = cinder_strndup(candidate, (size_t)written);
                return true;
            }
        }
    }
    return false;
}

static void process_file(CinderPP *pp, const char *path, char **out, size_t *out_len, size_t *out_cap, unsigned depth) {
    if (depth > 32U) {
        cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(CINDER_NO_FILE, 0U, 0U), "include nesting exceeded the recursion limit");
        return;
    }
    CinderFileId file_id = cinder_source_load(pp->sources, path, stderr);
    CinderSourceFile *file = cinder_source_get(pp->sources, file_id);
    if (file == NULL) return;
    bool active[64];
    bool parent_active[64];
    unsigned condition_depth = 0U;
    active[0] = true;
    parent_active[0] = true;
    const char *line = file->bytes;
    while (*line != '\0') {
        const char *line_end = strchr(line, '\n');
        if (line_end == NULL) line_end = line + strlen(line);
        const char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        bool directive = *p == '#';
        if (directive) {
            ++p;
            p = skip_space(p);
            const char *word_end = NULL;
            if (identifier_at(p, &word_end)) {
                size_t word_len = (size_t)(word_end - p);
                bool current = active[condition_depth];
                if (word_len == 6U && strncmp(p, "define", 6U) == 0 && current) { char *definition = cinder_strndup(word_end, (size_t)(line_end - word_end)); macro_define(pp, definition); free(definition); }
                else if (word_len == 5U && strncmp(p, "undef", 5U) == 0 && current) {
                    const char *name = skip_space(word_end);
                    if (identifier_at(name, &word_end)) {
                        for (size_t i = 0U; i < pp->macros.len; ++i) {
                            if (strlen(pp->macros.data[i].name) == (size_t)(word_end - name) && memcmp(pp->macros.data[i].name, name, (size_t)(word_end - name)) == 0) {
                                macro_free(&pp->macros.data[i]);
                                memmove(&pp->macros.data[i], &pp->macros.data[i + 1U], (pp->macros.len - i - 1U) * sizeof(pp->macros.data[0]));
                                pp->macros.len--;
                                break;
                            }
                        }
                    }
                } else if (word_len == 2U && strncmp(p, "if", 2U) == 0) {
                    if (condition_depth + 1U >= CINDER_ARRAY_LEN(active)) {
                        cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(file_id, (size_t)(line - file->bytes), (size_t)(line_end - line)), "conditional nesting is too deep");
                    } else {
                        ++condition_depth;
                        parent_active[condition_depth] = active[condition_depth - 1U];
                        active[condition_depth] = parent_active[condition_depth] && pp_condition(pp, word_end);
                    }
                } else if (word_len == 5U && strncmp(p, "ifdef", 5U) == 0) {
                    ++condition_depth;
                    parent_active[condition_depth] = active[condition_depth - 1U];
                    const char *name = skip_space(word_end);
                    const char *name_end = NULL;
                    active[condition_depth] = parent_active[condition_depth] && identifier_at(name, &name_end) && macro_find(pp, name, (size_t)(name_end - name)) != NULL;
                } else if (word_len == 6U && strncmp(p, "ifndef", 6U) == 0) {
                    ++condition_depth;
                    parent_active[condition_depth] = active[condition_depth - 1U];
                    const char *name = skip_space(word_end);
                    const char *name_end = NULL;
                    active[condition_depth] = parent_active[condition_depth] && identifier_at(name, &name_end) && macro_find(pp, name, (size_t)(name_end - name)) == NULL;
                } else if (word_len == 4U && strncmp(p, "elif", 4U) == 0 && condition_depth > 0U) {
                    active[condition_depth] = parent_active[condition_depth] && pp_condition(pp, word_end);
                } else if (word_len == 4U && strncmp(p, "else", 4U) == 0 && condition_depth > 0U) {
                    active[condition_depth] = parent_active[condition_depth] && !active[condition_depth];
                } else if (word_len == 5U && strncmp(p, "endif", 5U) == 0 && condition_depth > 0U) {
                    --condition_depth;
                } else if (word_len == 7U && strncmp(p, "include", 7U) == 0 && current) {
                    char *resolved = NULL;
                    if (!read_include(pp, file->path, word_end, &resolved)) {
                        cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(file_id, (size_t)(line - file->bytes), (size_t)(line_end - line)), "cannot resolve include");
                    } else {
                        process_file(pp, resolved, out, out_len, out_cap, depth + 1U);
                        free(resolved);
                    }
                } else if (word_len == 5U && strncmp(p, "error", 5U) == 0 && current) {
                    cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(file_id, (size_t)(line - file->bytes), (size_t)(line_end - line)), "%.*s", (int)(line_end - word_end), word_end);
                }
            }
        } else if (active[condition_depth]) {
            expand_text(pp, line, (size_t)(line_end - line), out, out_len, out_cap, 0U);
            string_append(out, out_len, out_cap, "\n", 1U);
        }
        line = *line_end == '\0' ? line_end : line_end + 1;
    }
    if (condition_depth != 0U) {
        cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(file_id, file->size, 0U), "unterminated conditional directive");
    }
}

int cinder_preprocess(CinderSourceManager *sources, const char *path, const char *const *include_dirs, size_t include_count, const char *const *defines, size_t define_count, CinderDiagnostics *diags) {
    CinderPP pp;
    pp.macros.data = NULL;
    pp.macros.len = 0U;
    pp.macros.cap = 0U;
    pp.include_dirs.data = NULL;
    pp.include_dirs.len = 0U;
    pp.include_dirs.cap = 0U;
    pp.sources = sources;
    pp.diags = diags;
    pp.depth = 0U;
    for (size_t i = 0U; i < include_count; ++i) {
        char *dir = cinder_strndup(include_dirs[i], strlen(include_dirs[i]));
        cinder_vec_push((CinderVec *)&pp.include_dirs, &dir);
    }
    for (size_t i = 0U; i < define_count; ++i) macro_define(&pp, defines[i]);
    char *output = NULL;
    size_t output_len = 0U;
    size_t output_cap = 0U;
    process_file(&pp, path, &output, &output_len, &output_cap, 0U);
    sources->preprocessed = output;
    sources->preprocessed_size = output_len;
    for (size_t i = 0U; i < pp.macros.len; ++i) macro_free(&pp.macros.data[i]);
    free(pp.macros.data);
    for (size_t i = 0U; i < pp.include_dirs.len; ++i) free(pp.include_dirs.data[i]);
    free(pp.include_dirs.data);
    return diags->errors == 0U ? 0 : 1;
}
