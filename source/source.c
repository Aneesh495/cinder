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
    cinder_arena_destroy(&sources->arena);
    sources->files.data = NULL;
    sources->files.len = 0U;
    sources->files.cap = 0U;
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

void cinder_loc_linecol(CinderSourceManager *sources, CinderLoc *loc) {
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
