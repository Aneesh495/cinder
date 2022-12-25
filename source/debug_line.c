#define _DARWIN_C_SOURCE
#define _XOPEN_SOURCE 700
#include "debug_private.h"

#include <stdlib.h>
#include <string.h>

typedef struct { CINDER_VEC_TYPE(const char *) names; } DebugFiles;

void cinder_debug_row(CinderMachineObject *object, CinderLoc loc) {
    if (!object->debug_enabled || loc.file == CINDER_NO_FILE) return;
    if (object->debug_rows.len != 0U && object->debug_rows.data[object->debug_rows.len - 1U].offset == object->text.len) {
        object->debug_rows.data[object->debug_rows.len - 1U].loc = loc; return;
    }
    CinderDebugRow row = {object->text.len, loc};
    cinder_vec_push((CinderVec *)&object->debug_rows, &row);
}

static size_t file_number(DebugFiles *files, const char *name) {
    for (size_t i = 0U; i < files->names.len; ++i)
        if (strcmp(files->names.data[i], name) == 0) return i + 1U;
    cinder_vec_push((CinderVec *)&files->names, &name);
    return files->names.len;
}

static void string(CinderBytes *bytes, const char *value) {
    cinder_bytes_append(bytes, (const unsigned char *)value, strlen(value) + 1U);
}

static void relocate(CinderMachineObject *object, size_t section, const char *symbol, int kind) {
    CinderBytes *bytes = &object->debug_sections.data[section].bytes;
    CinderFixup fix = {bytes->len, cinder_strndup(symbol, strlen(symbol)), kind, 0, (unsigned)(4U + section)};
    cinder_vec_push((CinderVec *)&object->fixups, &fix);
    if (kind == 1) cinder_bytes_put64(bytes, 0U);
    else cinder_bytes_put32(bytes, 0U);
}

static void attr(CinderBytes *bytes, unsigned name, unsigned form) {
    cinder_dwarf_uleb(bytes, name); cinder_dwarf_uleb(bytes, form);
}

static void abbreviation(CinderBytes *bytes, unsigned code, unsigned tag, bool children) {
    cinder_dwarf_uleb(bytes, code); cinder_dwarf_uleb(bytes, tag);
    cinder_bytes_put8(bytes, children ? 1U : 0U);
}

static void abbreviations(CinderBytes *bytes) {
    abbreviation(bytes, 1U, 0x11U, true); /* compile_unit */
    attr(bytes, 0x25U, 0x08U); /* producer: string */
    attr(bytes, 0x03U, 0x08U); /* name */
    attr(bytes, 0x1BU, 0x08U); /* comp_dir */
    attr(bytes, 0x13U, 0x05U); /* language: data2 */
    attr(bytes, 0x10U, 0x17U); /* stmt_list: sec_offset */
    attr(bytes, 0U, 0U);
    abbreviation(bytes, 2U, 0x2EU, false); /* subprogram */
    attr(bytes, 0x03U, 0x08U);
    attr(bytes, 0x3AU, 0x0FU); attr(bytes, 0x3BU, 0x0FU); attr(bytes, 0x39U, 0x0FU);
    attr(bytes, 0x11U, 0x01U); /* low_pc: address */
    attr(bytes, 0x12U, 0x07U); /* high_pc: byte extent */
    attr(bytes, 0x3FU, 0x0CU); /* external: flag */
    attr(bytes, 0x40U, 0x18U); /* frame_base: exprloc */
    attr(bytes, 0U, 0U); cinder_dwarf_uleb(bytes, 0U);
}

static void line_header(CinderBytes *bytes, const DebugFiles *files) {
    cinder_bytes_put32(bytes, 0U); cinder_bytes_put16(bytes, 4U);
    size_t length_at = bytes->len; cinder_bytes_put32(bytes, 0U);
    size_t header_at = bytes->len;
    const unsigned char state[] = {1U, 1U, 1U, 251U, 14U, 13U,
                                  0U, 1U, 1U, 1U, 1U, 0U, 0U, 0U, 1U, 0U, 0U, 1U};
    cinder_bytes_append(bytes, state, CINDER_ARRAY_LEN(state));
    cinder_bytes_put8(bytes, 0U); /* no separate include-directory table */
    for (size_t i = 0U; i < files->names.len; ++i) {
        string(bytes, files->names.data[i]);
        cinder_dwarf_uleb(bytes, 0U); cinder_dwarf_uleb(bytes, 0U); cinder_dwarf_uleb(bytes, 0U);
    }
    cinder_bytes_put8(bytes, 0U);
    cinder_bytes_patch32(bytes, length_at, (uint32_t)(bytes->len - header_at));
}

static void line_program(CinderMachineObject *object, size_t section, const CinderDebugFunction *function, DebugFiles *files, CinderSourceManager *sources) {
    CinderBytes *bytes = &object->debug_sections.data[section].bytes;
    size_t start = object->symbol_offsets.data[function->symbol];
    size_t size = object->symbol_sizes.data[function->symbol];
    cinder_bytes_put8(bytes, 0U); cinder_dwarf_uleb(bytes, 9U); cinder_bytes_put8(bytes, 2U);
    relocate(object, section, object->defined_symbols.data[function->symbol], 1);
    size_t previous_pc = start, previous_file = 1U;
    int64_t previous_line = 1;
    unsigned previous_column = 0U;
    bool is_statement = true;
    for (size_t i = function->first_row; i < function->end_row; ++i) {
        const CinderDebugRow *row = &object->debug_rows.data[i];
        CinderLoc loc = row->loc; cinder_loc_linecol(sources, &loc);
        if (loc.line == 0U) continue;
        size_t file = file_number(files, cinder_loc_name(sources, loc));
        if (row->offset != previous_pc) {
            cinder_bytes_put8(bytes, 2U); cinder_dwarf_uleb(bytes, row->offset - previous_pc);
        }
        bool statement = file != previous_file || (int64_t)loc.line != previous_line || i == function->first_row || row->offset == function->prologue_end;
        if (statement != is_statement) { cinder_bytes_put8(bytes, 6U); is_statement = statement; }
        if (file != previous_file) { cinder_bytes_put8(bytes, 4U); cinder_dwarf_uleb(bytes, file); }
        if ((int64_t)loc.line != previous_line) { cinder_bytes_put8(bytes, 3U); cinder_dwarf_sleb(bytes, (int64_t)loc.line - previous_line); }
        if (loc.column != previous_column) { cinder_bytes_put8(bytes, 5U); cinder_dwarf_uleb(bytes, loc.column); }
        if (row->offset == function->prologue_end) cinder_bytes_put8(bytes, 10U);
        cinder_bytes_put8(bytes, 1U);
        previous_pc = row->offset; previous_file = file; previous_line = loc.line; previous_column = loc.column;
    }
    cinder_bytes_put8(bytes, 2U); cinder_dwarf_uleb(bytes, start + size - previous_pc);
    cinder_bytes_put8(bytes, 0U); cinder_dwarf_uleb(bytes, 1U); cinder_bytes_put8(bytes, 1U);
}

static bool location_valid(CinderSourceManager *sources, CinderLoc loc) {
    const CinderSourceFile *file = cinder_source_get(sources, loc.file);
    if (file == NULL || !file->valid || loc.offset > file->size || loc.frame > sources->frames.len) return false;
    return loc.frame == 0U || sources->frames.data[loc.frame - 1U].file == loc.file;
}

static bool source_map_valid(const CinderMachineObject *object, CinderSourceManager *sources, CinderDiagnostics *diags) {
    size_t previous_row = 0U;
    for (size_t i = 0U; i < object->debug_functions.len; ++i) {
        const CinderDebugFunction *function = &object->debug_functions.data[i];
        if (function->symbol != i || i >= object->symbol_offsets.len || i >= object->symbol_sizes.len ||
            function->first_row != previous_row || function->end_row <= function->first_row || function->end_row > object->debug_rows.len ||
            !location_valid(sources, function->loc)) goto invalid;
        size_t start = object->symbol_offsets.data[i], size = object->symbol_sizes.data[i];
        if (start > object->text.len || size == 0U || size > object->text.len - start ||
            function->prologue_end < start || function->prologue_end >= start + size ||
            object->debug_rows.data[function->first_row].offset != start) goto invalid;
        size_t previous_pc = start;
        bool has_prologue = false;
        for (size_t row = function->first_row; row < function->end_row; ++row) {
            const CinderDebugRow *entry = &object->debug_rows.data[row];
            if (!location_valid(sources, entry->loc) || (row > function->first_row && entry->offset <= previous_pc) || entry->offset >= start + size) goto invalid;
            if (entry->offset == function->prologue_end) has_prologue = true;
            previous_pc = entry->offset;
        }
        if (!has_prologue || object->defined_symbols.data[i] == NULL) goto invalid;
        previous_row = function->end_row;
    }
    if (previous_row != object->debug_rows.len) goto invalid;
    return true;
invalid:
    cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "source debug map has an invalid function, location or instruction range");
    return false;
}

int cinder_debug_finish(CinderMachineObject *object, CinderSourceManager *sources, CinderDiagnostics *diags) {
    if (!object->debug_enabled) return 0;
    if (sources->files.len == 0U || object->debug_functions.len != object->defined_symbols.len ||
        object->symbol_offsets.len != object->defined_symbols.len || object->symbol_sizes.len != object->defined_symbols.len ||
        object->symbol_globals.len != object->defined_symbols.len) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "source debugging requires a complete source-function map"); return 1;
    }
    for (size_t i = 0U; i < object->debug_sections.len; ++i) {
        const char *name = object->debug_sections.data[i].name;
        if (name == NULL || strcmp(name, ".debug_line") == 0 || strcmp(name, ".debug_info") == 0 || strcmp(name, ".debug_abbrev") == 0) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "source debug sections have already been finalized or are invalid"); return 1;
        }
    }
    if (!source_map_valid(object, sources, diags)) return 1;
    DebugFiles files = {{NULL, 0U, 0U}};
    const char *unit_name = cinder_source_display_path(sources, cinder_source_name(sources, 1U));
    (void)file_number(&files, unit_name);
    for (size_t i = 0U; i < object->debug_functions.len; ++i)
        (void)file_number(&files, cinder_loc_name(sources, object->debug_functions.data[i].loc));
    for (size_t i = 0U; i < object->debug_rows.len; ++i)
        (void)file_number(&files, cinder_loc_name(sources, object->debug_rows.data[i].loc));
    char *physical_directory = realpath(".", NULL);
    if (physical_directory == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot resolve debug compilation directory"); free(files.names.data); return 1; }
    const char *directory = cinder_source_display_path(sources, physical_directory); free(physical_directory);
    size_t line = cinder_debug_section(object, ".debug_line", 1U);
    size_t abbrev = cinder_debug_section(object, ".debug_abbrev", 1U);
    size_t info = cinder_debug_section(object, ".debug_info", 1U);
    abbreviations(&object->debug_sections.data[abbrev].bytes);
    CinderBytes *line_bytes = &object->debug_sections.data[line].bytes;
    line_header(line_bytes, &files);
    for (size_t i = 0U; i < object->debug_functions.len; ++i)
        line_program(object, line, &object->debug_functions.data[i], &files, sources);
    cinder_bytes_patch32(line_bytes, 0U, (uint32_t)(line_bytes->len - 4U));
    CinderBytes *bytes = &object->debug_sections.data[info].bytes;
    cinder_bytes_put32(bytes, 0U); cinder_bytes_put16(bytes, 4U);
    relocate(object, info, ".debug_abbrev", 10); cinder_bytes_put8(bytes, 8U);
    cinder_dwarf_uleb(bytes, 1U); string(bytes, "Cinder " CINDER_VERSION); string(bytes, unit_name); string(bytes, directory);
    cinder_bytes_put16(bytes, 0x0CU); /* DW_LANG_C99: DWARF4's C language profile */
    relocate(object, info, ".debug_line", 10);
    for (size_t i = 0U; i < object->debug_functions.len; ++i) {
        const CinderDebugFunction *function = &object->debug_functions.data[i];
        CinderLoc loc = function->loc; cinder_loc_linecol(sources, &loc);
        cinder_dwarf_uleb(bytes, 2U); string(bytes, object->defined_symbols.data[function->symbol]);
        cinder_dwarf_uleb(bytes, file_number(&files, cinder_loc_name(sources, loc)));
        cinder_dwarf_uleb(bytes, loc.line); cinder_dwarf_uleb(bytes, loc.column);
        relocate(object, info, object->defined_symbols.data[function->symbol], 1);
        cinder_bytes_put64(bytes, object->symbol_sizes.data[function->symbol]);
        cinder_bytes_put8(bytes, object->symbol_globals.data[function->symbol] ? 1U : 0U);
        cinder_dwarf_uleb(bytes, 1U); cinder_bytes_put8(bytes, 0x9CU); /* call_frame_cfa */
    }
    cinder_dwarf_uleb(bytes, 0U);
    free(files.names.data);
    if (bytes->len > UINT32_MAX || line_bytes->len > UINT32_MAX) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "source debug data exceeds the DWARF32 range"); return 1;
    }
    cinder_bytes_patch32(bytes, 0U, (uint32_t)(bytes->len - 4U));
    return 0;
}
