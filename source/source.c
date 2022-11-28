#include "cinder.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void source_error(FILE *err, CinderDiagnostics *diags, const char *path, const char *reason) {
    if (diags != NULL) cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "%s: %s", path, reason);
    else fprintf(err, "cinder: %s: %s\n", path, reason);
}

static char *read_file(const char *path, size_t *size, FILE *err, CinderDiagnostics *diags) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        if (diags != NULL) cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot open %s: %s", path, strerror(errno));
        else fprintf(err, "cinder: cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        source_error(err, diags, path, "cannot seek source file");
        fclose(file);
        return NULL;
    }
    long end = ftell(file);
    if (end < 0L) {
        source_error(err, diags, path, "cannot determine source size");
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        source_error(err, diags, path, "cannot rewind source file");
        fclose(file);
        return NULL;
    }
    size_t length = (size_t)end;
    if (length > 16U * 1024U * 1024U) {
        source_error(err, diags, path, "source file exceeds the 16 MiB input limit");
        fclose(file);
        return NULL;
    }
    char *bytes = cinder_alloc(length + 1U);
    size_t got = fread(bytes, 1U, length, file);
    if (got != length || ferror(file) != 0) {
        source_error(err, diags, path, "short read from source file");
        free(bytes);
        fclose(file);
        return NULL;
    }
    bytes[length] = '\0';
    fclose(file);
    *size = length;
    return bytes;
}

void cinder_sources_init(CinderSourceManager *sources) {
    cinder_arena_init(&sources->arena, 16384U);
    sources->files.data = NULL;
    sources->files.len = 0U;
    sources->files.cap = 0U;
    sources->spans.data = NULL; sources->spans.len = 0U; sources->spans.cap = 0U;
    sources->line_directives.data = NULL; sources->line_directives.len = 0U; sources->line_directives.cap = 0U;
    sources->frames.data = NULL; sources->frames.len = 0U; sources->frames.cap = 0U;
    sources->prefix_maps.data = NULL; sources->prefix_maps.len = 0U; sources->prefix_maps.cap = 0U;
    sources->display_names.data = NULL; sources->display_names.len = 0U; sources->display_names.cap = 0U;
    sources->preprocessed = NULL;
    sources->preprocessed_size = 0U;
}

void cinder_sources_destroy(CinderSourceManager *sources) {
    for (size_t i = 0U; i < sources->files.len; ++i) {
        free(sources->files.data[i].path);
        free(sources->files.data[i].bytes);
    }
    free(sources->files.data);
    free(sources->preprocessed);
    free(sources->spans.data);
    free(sources->line_directives.data);
    free(sources->frames.data);
    free(sources->prefix_maps.data); free(sources->display_names.data);
    cinder_arena_destroy(&sources->arena);
    sources->files.data = NULL;
    sources->files.len = 0U;
    sources->files.cap = 0U;
    sources->spans.data = NULL; sources->spans.len = 0U; sources->spans.cap = 0U;
    sources->line_directives.data = NULL; sources->line_directives.len = 0U; sources->line_directives.cap = 0U;
    sources->frames.data = NULL; sources->frames.len = 0U; sources->frames.cap = 0U;
    sources->prefix_maps.data = NULL; sources->prefix_maps.len = 0U; sources->prefix_maps.cap = 0U;
    sources->display_names.data = NULL; sources->display_names.len = 0U; sources->display_names.cap = 0U;
    sources->preprocessed = NULL;
    sources->preprocessed_size = 0U;
}

static CinderFileId load_source(CinderSourceManager *sources, const char *path, FILE *err, CinderDiagnostics *diags) {
    for (size_t i = 0U; i < sources->files.len; ++i) {
        if (strcmp(sources->files.data[i].path, path) == 0) {
            return sources->files.data[i].valid ? sources->files.data[i].id : CINDER_NO_FILE;
        }
    }
    size_t size = 0U;
    char *bytes = read_file(path, &size, err, diags);
    if (bytes == NULL) {
        return CINDER_NO_FILE;
    }
    size_t invalid = 0U;
    bool valid = cinder_utf8_validate(bytes, size, &invalid);
    CinderSourceFile file;
    file.id = (CinderFileId)(sources->files.len + 1U);
    file.path = cinder_strndup(path, strlen(path));
    file.bytes = bytes;
    file.size = size;
    file.valid = valid;
    cinder_vec_push((CinderVec *)&sources->files, &file);
    if (!valid) {
        if (diags != NULL) cinder_diag(diags, CINDER_ERROR, cinder_loc(file.id, invalid, 1U), "invalid UTF-8 or embedded NUL at byte %zu", invalid);
        else fprintf(err, "cinder: error: invalid UTF-8 or embedded NUL at byte %zu in %s\n", invalid, path);
        return CINDER_NO_FILE;
    }
    return file.id;
}

CinderFileId cinder_source_load(CinderSourceManager *sources, const char *path, FILE *err) { return load_source(sources, path, err, NULL); }
CinderFileId cinder_source_load_diagnostic(CinderSourceManager *sources, const char *path, CinderDiagnostics *diags) { return load_source(sources, path, NULL, diags); }

CinderSourceFile *cinder_source_get(CinderSourceManager *sources, CinderFileId id) {
    if (id == CINDER_NO_FILE || id > sources->files.len) {
        return NULL;
    }
    return &sources->files.data[id - 1U];
}

CinderLoc cinder_loc(CinderFileId file, size_t offset, size_t length) {
    CinderLoc loc;
    loc.file = file;
    loc.offset = offset;
    loc.length = length;
    loc.line = 0U;
    loc.column = 0U;
    loc.frame = 0U; loc.origin = 0U;
    return loc;
}

void cinder_loc_physical_linecol(CinderSourceManager *sources, CinderLoc *loc) {
    CinderSourceFile *file = cinder_source_get(sources, loc->file);
    if (file == NULL) {
        loc->line = 0U;
        loc->column = 0U;
        return;
    }
    size_t end = loc->offset > file->size ? file->size : loc->offset;
    unsigned line = 1U;
    size_t line_start = 0U;
    for (size_t i = 0U; i < end; ++i) {
        if (file->bytes[i] == '\n') {
            line++;
            line_start = i + 1U;
        }
    }
    loc->line = line;
    loc->column = (unsigned)(end - line_start + 1U);
}

const char *cinder_source_name(CinderSourceManager *sources, CinderFileId id) {
    CinderSourceFile *file = cinder_source_get(sources, id);
    return file == NULL ? "<unknown>" : file->path;
}

static const CinderLineDirective *line_directive(const CinderSourceManager *sources, CinderLoc loc) {
    for (size_t i = sources->line_directives.len; i > 0U; --i) {
        const CinderLineDirective *record = &sources->line_directives.data[i - 1U];
        if (record->file == loc.file && record->frame == loc.frame && record->offset <= loc.offset) return record;
    }
    return NULL;
}

void cinder_loc_linecol(CinderSourceManager *sources, CinderLoc *loc) {
    cinder_loc_physical_linecol(sources, loc);
    const CinderLineDirective *record = line_directive(sources, *loc);
    if (record != NULL) {
        unsigned delta = loc->line - record->physical_line;
        loc->line = delta > UINT32_MAX - record->logical_line ? UINT32_MAX : record->logical_line + delta;
    }
}

static const char *physical_loc_name(CinderSourceManager *sources, CinderLoc loc) {
    const CinderLineDirective *record = line_directive(sources, loc);
    return record == NULL ? cinder_source_name(sources, loc.file) : record->path;
}

int cinder_source_prefix_map(CinderSourceManager *sources, const char *specification, bool macro_only, CinderDiagnostics *diags) {
    const char *equal = strchr(specification, '=');
    if (equal == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "prefix mapping requires OLD=NEW"); return 1; }
    if (sources->files.len != 0U || sources->display_names.len != 0U) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "prefix maps must be configured before source loading"); return 1; }
    CinderPrefixMap map = {cinder_arena_strndup(&sources->arena, specification, (size_t)(equal - specification)), cinder_arena_strndup(&sources->arena, equal + 1, strlen(equal + 1)), macro_only};
    cinder_vec_push((CinderVec *)&sources->prefix_maps, &map); return 0;
}

static const char *display_name(CinderSourceManager *sources, const char *physical, bool macro) {
    if (sources->prefix_maps.len == 0U) return physical;
    for (size_t i = 0U; i < sources->display_names.len; ++i) {
        CinderDisplayName *cached = &sources->display_names.data[i];
        if (cached->macro == macro && strcmp(cached->physical, physical) == 0) return cached->display;
    }
    const char *display = physical;
    for (size_t i = sources->prefix_maps.len; i > 0U; --i) {
        const CinderPrefixMap *map = &sources->prefix_maps.data[i - 1U]; size_t before = strlen(map->before);
        if ((!macro && map->macro_only) || strncmp(physical, map->before, before) != 0) continue;
        size_t after = strlen(map->after), suffix = strlen(physical + before);
        if (after > SIZE_MAX - suffix - 1U) break;
        char *copy = cinder_arena_alloc(&sources->arena, after + suffix + 1U, 1U);
        memcpy(copy, map->after, after); memcpy(copy + after, physical + before, suffix + 1U); display = copy; break;
    }
    CinderDisplayName cached = {physical, display, macro}; cinder_vec_push((CinderVec *)&sources->display_names, &cached); return display;
}

const char *cinder_loc_name(CinderSourceManager *sources, CinderLoc loc) { return display_name(sources, physical_loc_name(sources, loc), false); }
const char *cinder_macro_loc_name(CinderSourceManager *sources, CinderLoc loc) { return display_name(sources, physical_loc_name(sources, loc), true); }
const char *cinder_unmapped_loc_name(CinderSourceManager *sources, CinderLoc loc) { return physical_loc_name(sources, loc); }

const CinderSourceSpan *cinder_preprocessed_span(CinderSourceManager *sources, size_t offset) {
    size_t left = 0U, right = sources->spans.len;
    while (left < right) {
        size_t middle = left + (right - left) / 2U;
        const CinderSourceSpan *span = &sources->spans.data[middle];
        if (offset < span->begin) right = middle;
        else if (offset >= span->end) left = middle + 1U;
        else return span;
    }
    return NULL;
}

CinderLoc cinder_preprocessed_loc(CinderSourceManager *sources, size_t offset, size_t length) {
    const CinderSourceSpan *span = cinder_preprocessed_span(sources, offset);
    if (span != NULL) {
        CinderLoc loc = span->expansion; loc.origin = (uint32_t)(span - sources->spans.data + 1); return loc;
    }
    if (sources->spans.len != 0U && offset >= sources->preprocessed_size) {
        CinderLoc loc = sources->spans.data[sources->spans.len - 1U].expansion;
        loc.offset += loc.length;
        loc.length = 0U;
        return loc;
    }
    return cinder_loc(CINDER_NO_FILE, offset, length);
}
