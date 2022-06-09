#include "cinder.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *size, FILE *err) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(err, "cinder: cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fprintf(err, "cinder: cannot seek %s\n", path);
        fclose(file);
        return NULL;
    }
    long end = ftell(file);
    if (end < 0L) {
        fprintf(err, "cinder: cannot determine size of %s\n", path);
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fprintf(err, "cinder: cannot rewind %s\n", path);
        fclose(file);
        return NULL;
    }
    size_t length = (size_t)end;
    if (length > 16U * 1024U * 1024U) {
        fprintf(err, "cinder: source file exceeds the 16 MiB input limit: %s\n", path);
        fclose(file);
        return NULL;
    }
    char *bytes = cinder_alloc(length + 1U);
    size_t got = fread(bytes, 1U, length, file);
    if (got != length || ferror(file) != 0) {
        fprintf(err, "cinder: short read from %s\n", path);
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
    cinder_arena_destroy(&sources->arena);
    sources->files.data = NULL;
    sources->files.len = 0U;
    sources->files.cap = 0U;
    sources->spans.data = NULL; sources->spans.len = 0U; sources->spans.cap = 0U;
    sources->line_directives.data = NULL; sources->line_directives.len = 0U; sources->line_directives.cap = 0U;
    sources->preprocessed = NULL;
    sources->preprocessed_size = 0U;
}

CinderFileId cinder_source_load(CinderSourceManager *sources, const char *path, FILE *err) {
    for (size_t i = 0U; i < sources->files.len; ++i) {
        if (strcmp(sources->files.data[i].path, path) == 0) {
            return sources->files.data[i].id;
        }
    }
    size_t size = 0U;
    char *bytes = read_file(path, &size, err);
    if (bytes == NULL) {
        return CINDER_NO_FILE;
    }
    size_t invalid = 0U;
    if (!cinder_utf8_validate(bytes, size, &invalid)) {
        fprintf(err, "cinder: error: invalid UTF-8 or embedded NUL at byte %zu in %s\n", invalid, path);
        free(bytes); return CINDER_NO_FILE;
    }
    CinderSourceFile file;
    file.id = (CinderFileId)(sources->files.len + 1U);
    file.path = cinder_strndup(path, strlen(path));
    file.bytes = bytes;
    file.size = size;
    cinder_vec_push((CinderVec *)&sources->files, &file);
    return file.id;
}

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
        if (record->file == loc.file && record->offset <= loc.offset) return record;
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

const char *cinder_loc_name(CinderSourceManager *sources, CinderLoc loc) {
    const CinderLineDirective *record = line_directive(sources, loc);
    return record == NULL ? cinder_source_name(sources, loc.file) : record->path;
}

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
    if (span != NULL) return span->expansion;
    if (sources->spans.len != 0U && offset >= sources->preprocessed_size) {
        CinderLoc loc = sources->spans.data[sources->spans.len - 1U].expansion;
        loc.offset += loc.length;
        loc.length = 0U;
        return loc;
    }
    return cinder_loc(CINDER_NO_FILE, offset, length);
}
