#define _XOPEN_SOURCE 700
#include "pp_private.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Directive state is per included file; macros are shared by one TU only. */
typedef struct {
    bool parent;
    bool active;
    bool taken;
    bool seen_else;
    CinderLoc opening;
} PPConditional;

static void define_text(PP *pp, const char *text, bool predefined) {
    PPTokens tokens = {0};
    pp_scan(pp, text, strlen(text), CINDER_NO_FILE, 0U, &tokens);
    pp_define(pp, &tokens, predefined);
    pp_destroy_tokens(&tokens);
}

static PPFile *file_state(PP *pp, CinderFileId file) {
    for (size_t i = 0U; i < pp->files.len; ++i) if (pp->files.data[i].file == file) return &pp->files.data[i];
    PPFile state = {file, false, false};
    cinder_vec_push((CinderVec *)&pp->files, &state);
    return &pp->files.data[pp->files.len - 1U];
}

static char *decode_header(PP *pp, PPToken token) {
    if (token.kind != PP_LITERAL || token.length < 2U || token.text[0] != '"' || token.text[token.length - 1U] != '"') {
        cinder_diag(pp->diags, CINDER_ERROR, token.loc, "expected ordinary string literal");
        return NULL;
    }
    char *result = cinder_alloc(token.length);
    size_t length = 0U;
    for (size_t i = 1U; i + 1U < token.length; ++i) {
        char c = token.text[i];
        if (c == '\\' && i + 2U < token.length) {
            char escaped = token.text[++i];
            if (escaped != '\\' && escaped != '"') {
                cinder_diag(pp->diags, CINDER_ERROR, token.loc, "unsupported escape in directive string");
                free(result);
                return NULL;
            }
            c = escaped;
        }
        result[length++] = c;
    }
    result[length] = '\0';
    return result;
}

void pp_pragma(PP *pp, const PPTokens *line, CinderLoc loc) {
    if (line->len == 1U && pp_is(line->data[0], "once")) {
        if (loc.file != CINDER_NO_FILE) file_state(pp, loc.file)->once = true;
        return;
    }
    if (line->len > 0U && pp_is(line->data[0], "pack")) {
        cinder_diag(pp->diags, CINDER_ERROR, loc, "packing pragmas are unsupported by the target layout profile");
        return;
    }
    if (line->len == 3U && pp_is(line->data[0], "STDC")) {
        PPToken kind = line->data[1], value = line->data[2];
        bool valid_value = pp_is(value, "ON") || pp_is(value, "OFF") || pp_is(value, "DEFAULT");
        if (pp_is(kind, "FP_CONTRACT") && valid_value) return;
        if ((pp_is(kind, "FENV_ACCESS") || pp_is(kind, "CX_LIMITED_RANGE")) && (pp_is(value, "OFF") || pp_is(value, "DEFAULT"))) return;
        cinder_diag(pp->diags, CINDER_ERROR, loc, "unsupported STDC pragma configuration");
        return;
    }
    cinder_diag(pp->diags, CINDER_WARNING, loc, "unknown pragma ignored");
}

static char *join_path(const char *directory, size_t directory_length, const char *name) {
    size_t length = strlen(name);
    if (directory_length > SIZE_MAX - length - 2U) return NULL;
    char *path = cinder_alloc(directory_length + length + 2U);
    memcpy(path, directory, directory_length);
    if (directory_length != 0U && directory[directory_length - 1U] != '/') path[directory_length++] = '/';
    memcpy(path + directory_length, name, length + 1U);
    return path;
}

static bool readable(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    fclose(file);
    return true;
}

static char *resolve_include(PP *pp, const char *current, const char *name, bool quoted) {
    if (name[0] == '/') return readable(name) ? cinder_strndup(name, strlen(name)) : NULL;
    if (quoted) {
        const char *slash = strrchr(current, '/');
        size_t length = slash == NULL ? 0U : (size_t)(slash - current + 1U);
        char *candidate = join_path(current, length, name);
        if (candidate != NULL && readable(candidate)) return candidate;
        free(candidate);
    }
    for (size_t i = 0U; i < pp->include_dirs.len; ++i) {
        const char *directory = pp->include_dirs.data[i];
        char *candidate = join_path(directory, strlen(directory), name);
        if (candidate != NULL && readable(candidate)) return candidate;
        free(candidate);
    }
    return NULL;
}

static void include_file(PP *pp, const char *current, const PPTokens *line, CinderLoc loc, unsigned depth) {
    PPTokens expanded = {0};
    const PPTokens *tokens = line;
    if (line->len == 0U || (line->data[0].kind != PP_LITERAL && !pp_is(line->data[0], "<"))) {
        pp_expand(pp, line, &expanded, 0U);
        tokens = &expanded;
    }
    char *name = NULL;
    bool quoted = false;
    if (tokens->len == 1U && tokens->data[0].kind == PP_LITERAL) {
        name = decode_header(pp, tokens->data[0]);
        quoted = true;
    } else if (tokens->len >= 3U && pp_is(tokens->data[0], "<") && pp_is(tokens->data[tokens->len - 1U], ">")) {
        CinderBytes bytes = {0};
        for (size_t i = 1U; i + 1U < tokens->len; ++i) {
            if (tokens->data[i].space && i > 1U) cinder_bytes_put8(&bytes, ' ');
            cinder_bytes_append(&bytes, (const unsigned char *)tokens->data[i].text, tokens->data[i].length);
        }
        cinder_bytes_put8(&bytes, 0U);
        name = (char *)bytes.data;
    } else cinder_diag(pp->diags, CINDER_ERROR, loc, "expected one quoted or angle header name");
    if (name != NULL) {
        char *path = *name == '\0' ? NULL : resolve_include(pp, current, name, quoted);
        if (path == NULL) cinder_diag(pp->diags, CINDER_ERROR, loc, "cannot resolve include '%s'", name);
        else { pp_process(pp, path, depth + 1U); free(path); }
        free(name);
    }
    pp_destroy_tokens(&expanded);
}

static void line_control(PP *pp, const PPTokens *line, CinderFileId file, size_t next_offset, CinderLoc loc) {
    PPTokens expanded = {0};
    pp_expand(pp, line, &expanded, 0U);
    if (expanded.len == 0U || expanded.len > 2U || expanded.data[0].kind != PP_NUMBER) {
        cinder_diag(pp->diags, CINDER_ERROR, loc, "#line requires a decimal line number and optional filename");
        pp_destroy_tokens(&expanded);
        return;
    }
    const char *text = expanded.data[0].text;
    uint64_t number = 0U;
    bool valid = *text != '\0';
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') { valid = false; break; }
        number = number * 10U + (unsigned)(text[i] - '0');
        if (number > 2147483647U) { valid = false; break; }
    }
    if (!valid || number == 0U) cinder_diag(pp->diags, CINDER_ERROR, loc, "#line number must be between 1 and 2147483647");
    else {
        CinderLoc physical = cinder_loc(file, next_offset, 0U);
        cinder_loc_physical_linecol(pp->sources, &physical);
        const char *path = cinder_loc_name(pp->sources, loc);
        char *decoded = expanded.len == 2U ? decode_header(pp, expanded.data[1]) : NULL;
        if (decoded != NULL) path = cinder_arena_strndup(&pp->sources->arena, decoded, strlen(decoded));
        CinderLineDirective record = {file, next_offset, physical.line, (unsigned)number, path};
        cinder_vec_push((CinderVec *)&pp->sources->line_directives, &record);
        free(decoded);
    }
    pp_destroy_tokens(&expanded);
}

static void flush_pending(PP *pp, PPTokens *pending) {
    if (pending->len != 0U) pp_expand(pp, pending, &pp->output, 0U);
    pending->len = 0U;
}

static void require_empty(PP *pp, const PPTokens *tokens, CinderLoc loc, const char *directive) {
    if (tokens->len != 0U) cinder_diag(pp->diags, CINDER_ERROR, loc, "trailing tokens after #%s", directive);
}

int pp_process(PP *pp, const char *path, unsigned depth) {
    if (depth > 128U) {
        cinder_diag(pp->diags, CINDER_ERROR, (CinderLoc){0}, "include nesting limit exceeded");
        return 1;
    }
    char *canonical = realpath(path, NULL);
    CinderFileId id = cinder_source_load(pp->sources, canonical == NULL ? path : canonical, stderr);
    free(canonical);
    CinderSourceFile *file = cinder_source_get(pp->sources, id);
    if (file == NULL) {
        cinder_diag(pp->diags, CINDER_ERROR, (CinderLoc){0}, "cannot read input '%s'", path);
        return 1;
    }
    PPFile *state = file_state(pp, id);
    if (state->included && state->once) return 0;
    state->included = true;
    /* Keep heap-owned path/bytes stable when recursive includes grow the file vector. */
    const char *source_path = file->path;
    size_t file_size = file->size;
    PPTokens tokens = {0}, pending = {0};
    pp_scan(pp, file->bytes, file_size, id, 0U, &tokens);
    PPConditional conditions[128];
    size_t condition_count = 0U;
    size_t cursor = 0U;
    while (cursor < tokens.len && pp->diags->errors == 0U) {
        size_t end = cursor;
        while (end < tokens.len && tokens.data[end].kind != PP_NEWLINE) ++end;
        bool active = condition_count == 0U || conditions[condition_count - 1U].active;
        bool directive = cursor < end && pp_is(tokens.data[cursor], "#");
        if (!directive) {
            if (active) for (size_t i = cursor; i < end; ++i) pp_push(&pending, tokens.data[i]);
            if (end < tokens.len) {
                if (active) pp_push(&pending, tokens.data[end]);
                else pp_push(&pp->output, tokens.data[end]);
            }
            cursor = end < tokens.len ? end + 1U : end;
            continue;
        }
        flush_pending(pp, &pending);
        size_t next_offset = end < tokens.len ? tokens.data[end].loc.offset + tokens.data[end].loc.length : file_size;
        if (++cursor == end) { cursor = end < tokens.len ? end + 1U : end; continue; }
        PPToken word = tokens.data[cursor++];
        CinderLoc loc = word.loc;
        PPTokens line = {tokens.data + cursor, end - cursor, end - cursor};
        bool opening = pp_is(word, "if") || pp_is(word, "ifdef") || pp_is(word, "ifndef");
        if (opening) {
            bool value = false;
            if (condition_count == CINDER_ARRAY_LEN(conditions)) {
                cinder_diag(pp->diags, CINDER_ERROR, loc, "conditional nesting limit exceeded");
            } else {
                if (pp_is(word, "if")) {
                    if (active) pp_evaluate(pp, &line, &value);
                } else if (line.len != 1U || line.data[0].kind != PP_IDENT) {
                    cinder_diag(pp->diags, CINDER_ERROR, loc, "#ifdef/#ifndef requires exactly one identifier");
                } else {
                    value = pp_find(pp, line.data[0]) != NULL;
                    if (pp_is(line.data[0], "__FILE__") || pp_is(line.data[0], "__LINE__") || pp_is(line.data[0], "__DATE__") || pp_is(line.data[0], "__TIME__")) value = true;
                    if (pp_is(word, "ifndef")) value = !value;
                }
                conditions[condition_count++] = (PPConditional){active, active && value, active && value, false, loc};
            }
        } else if (pp_is(word, "elif") || pp_is(word, "else") || pp_is(word, "endif")) {
            if (condition_count == 0U) cinder_diag(pp->diags, CINDER_ERROR, loc, "unmatched #%s", word.text);
            else {
                PPConditional *condition = &conditions[condition_count - 1U];
                if (pp_is(word, "endif")) { require_empty(pp, &line, loc, "endif"); --condition_count; }
                else if (condition->seen_else) cinder_diag(pp->diags, CINDER_ERROR, loc, "#%s after #else", word.text);
                else if (pp_is(word, "else")) {
                    require_empty(pp, &line, loc, "else");
                    condition->active = condition->parent && !condition->taken;
                    condition->taken = true;
                    condition->seen_else = true;
                } else {
                    bool value = false;
                    if (condition->parent && !condition->taken) pp_evaluate(pp, &line, &value);
                    condition->active = condition->parent && !condition->taken && value;
                    condition->taken = condition->taken || condition->active;
                }
            }
        } else if (active) {
            if (pp_is(word, "define")) pp_define(pp, &line, false);
            else if (pp_is(word, "undef")) {
                if (line.len != 1U || line.data[0].kind != PP_IDENT) cinder_diag(pp->diags, CINDER_ERROR, loc, "#undef requires exactly one identifier");
                else pp_undef(pp, line.data[0]);
            } else if (pp_is(word, "include")) include_file(pp, source_path, &line, loc, depth);
            else if (pp_is(word, "line")) line_control(pp, &line, id, next_offset, loc);
            else if (pp_is(word, "pragma")) pp_pragma(pp, &line, loc);
            else if (pp_is(word, "error")) {
                CinderBytes message = {0};
                for (size_t i = 0U; i < line.len; ++i) {
                    if (i != 0U) cinder_bytes_put8(&message, ' ');
                    cinder_bytes_append(&message, (const unsigned char *)line.data[i].text, line.data[i].length);
                }
                cinder_bytes_put8(&message, 0U);
                cinder_diag(pp->diags, CINDER_ERROR, loc, "#error %s", (char *)message.data);
                free(message.data);
            } else cinder_diag(pp->diags, CINDER_ERROR, loc, "unknown preprocessing directive '%s'", word.text);
        }
        if (end < tokens.len) pp_push(&pp->output, tokens.data[end]);
        cursor = end < tokens.len ? end + 1U : end;
    }
    flush_pending(pp, &pending);
    if (condition_count != 0U) cinder_diag(pp->diags, CINDER_ERROR, conditions[condition_count - 1U].opening, "unterminated conditional directive");
    pp_destroy_tokens(&pending);
    pp_destroy_tokens(&tokens);
    return pp->diags->errors == 0U ? 0 : 1;
}

static void initialize_clock(PP *pp) {
    time_t epoch = time(NULL);
    const char *configured = getenv("SOURCE_DATE_EPOCH");
    if (configured != NULL) {
        char *end = NULL;
        errno = 0;
        unsigned long long value = strtoull(configured, &end, 10);
        if (errno != 0 || end == configured || *end != '\0' || configured[0] == '-' || value > INT64_MAX) {
            cinder_diag(pp->diags, CINDER_ERROR, (CinderLoc){0}, "invalid SOURCE_DATE_EPOCH");
            return;
        }
        epoch = (time_t)value;
        if ((unsigned long long)epoch != value) {
            cinder_diag(pp->diags, CINDER_ERROR, (CinderLoc){0}, "SOURCE_DATE_EPOCH is outside host time range");
            return;
        }
    }
    struct tm *calendar = gmtime(&epoch);
    if (calendar == NULL) {
        cinder_diag(pp->diags, CINDER_ERROR, (CinderLoc){0}, "cannot represent compilation source date");
        return;
    }
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char date[40], clock[40];
    int d = snprintf(date, sizeof(date), "\"%s %2d %04d\"", months[calendar->tm_mon], calendar->tm_mday, calendar->tm_year + 1900);
    int t = snprintf(clock, sizeof(clock), "\"%02d:%02d:%02d\"", calendar->tm_hour, calendar->tm_min, calendar->tm_sec);
    pp->date = cinder_arena_strndup(&pp->arena, date, d > 0 ? (size_t)d : 0U);
    pp->time = cinder_arena_strndup(&pp->arena, clock, t > 0 ? (size_t)t : 0U);
}

int cinder_preprocess(CinderSourceManager *sources, const char *path, const char *const *include_dirs, size_t include_count, const char *const *defines, size_t define_count, CinderDiagnostics *diags) {
    PP pp;
    memset(&pp, 0, sizeof(pp));
    pp.sources = sources;
    pp.diags = diags;
    cinder_arena_init(&pp.arena, 32768U);
    initialize_clock(&pp);
    static const char *const predefined[] = {
        "__STDC__ 1", "__STDC_VERSION__ 201710L", "__STDC_HOSTED__ 1", "__CINDER__ 1",
        "__x86_64__ 1", "__linux__ 1", "__unix__ 1", "__LP64__ 1", "__STDC_NO_ATOMICS__ 1",
        "__STDC_NO_COMPLEX__ 1", "__STDC_NO_THREADS__ 1", "__STDC_NO_VLA__ 1"
    };
    for (size_t i = 0U; i < CINDER_ARRAY_LEN(predefined); ++i) define_text(&pp, predefined[i], true);
    for (size_t i = 0U; i < include_count; ++i) cinder_vec_push((CinderVec *)&pp.include_dirs, &include_dirs[i]);
    for (size_t i = 0U; i < define_count; ++i) {
        const char *definition = defines[i];
        const char *equal = strchr(definition, '=');
        char *copy = NULL;
        if (equal != NULL) {
            copy = cinder_strndup(definition, strlen(definition));
            copy[equal - definition] = ' ';
        } else {
            size_t length = strlen(definition);
            copy = cinder_alloc(length + 3U);
            memcpy(copy, definition, length);
            memcpy(copy + length, " 1", 3U);
        }
        define_text(&pp, copy, false);
        free(copy);
    }
    if (diags->errors == 0U) pp_process(&pp, path, 0U);
    pp_render(&pp);
    for (size_t i = 0U; i < pp.macros.len; ++i) {
        free(pp.macros.data[i].params.data);
        pp_destroy_tokens(&pp.macros.data[i].replacement);
    }
    free(pp.macros.data);
    free(pp.include_dirs.data);
    free(pp.files.data);
    pp_destroy_tokens(&pp.output);
    cinder_arena_destroy(&pp.arena);
    return diags->errors == 0U ? 0 : 1;
}
