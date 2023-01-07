#define _DARWIN_C_SOURCE
#define _XOPEN_SOURCE 700
#include "debug_private.h"

#include <stdlib.h>
#include <string.h>


void cinder_debug_row(CinderMachineObject *object, CinderLoc loc) {
    if (!object->debug_enabled || loc.file == CINDER_NO_FILE) return;
    if (object->debug_rows.len != 0U && object->debug_rows.data[object->debug_rows.len - 1U].offset == object->text.len) {
        object->debug_rows.data[object->debug_rows.len - 1U].loc = loc; return;
    }
    CinderDebugRow row = {object->text.len, loc};
    cinder_vec_push((CinderVec *)&object->debug_rows, &row);
}

size_t cinder_debug_file(CinderDebugFiles *files, const char *name) {
    for (size_t i = 0U; i < files->names.len; ++i)
        if (strcmp(files->names.data[i], name) == 0) return i + 1U;
    cinder_vec_push((CinderVec *)&files->names, &name);
    return files->names.len;
}

void cinder_dwarf_string(CinderBytes *bytes, const char *value) {
    cinder_bytes_append(bytes, (const unsigned char *)value, strlen(value) + 1U);
}

void cinder_debug_offset(CinderMachineObject *object, size_t section, const char *symbol, int kind, int64_t addend) {
    CinderBytes *bytes = &object->debug_sections.data[section].bytes;
    CinderFixup fix = {bytes->len, cinder_strndup(symbol, strlen(symbol)), kind, addend, (unsigned)(4U + section)};
    cinder_vec_push((CinderVec *)&object->fixups, &fix);
    if (kind == 1) cinder_bytes_put64(bytes, 0U);
    else cinder_bytes_put32(bytes, 0U);
}

static void relocate(CinderMachineObject *object, size_t section, const char *symbol, int kind) {
    cinder_debug_offset(object, section, symbol, kind, 0);
}

static void attr(CinderBytes *bytes, unsigned name, unsigned form) {
    cinder_dwarf_uleb(bytes, name); cinder_dwarf_uleb(bytes, form);
}

static void abbreviation(CinderBytes *bytes, unsigned code, unsigned tag, bool children) {
    cinder_dwarf_uleb(bytes, code); cinder_dwarf_uleb(bytes, tag);
    cinder_bytes_put8(bytes, children ? 1U : 0U);
}

static void subprogram_abbreviation(CinderBytes *bytes, unsigned code, bool children) {
    abbreviation(bytes, code, 0x2EU, children);
    attr(bytes, 0x03U, 0x08U);
    attr(bytes, 0x3AU, 0x0FU); attr(bytes, 0x3BU, 0x0FU); attr(bytes, 0x39U, 0x0FU);
    attr(bytes, 0x11U, 0x01U); /* low_pc: address */
    attr(bytes, 0x12U, 0x07U); /* high_pc: byte extent */
    attr(bytes, 0x3FU, 0x0CU); /* external: flag */
    attr(bytes, 0x40U, 0x18U); /* frame_base: exprloc */
    attr(bytes, 0x49U, 0x13U); /* return type: ref4 */
    attr(bytes, 0x27U, 0x0CU); /* prototyped */
    attr(bytes, 0U, 0U);
}

static void abbreviations(CinderBytes *bytes) {
    abbreviation(bytes, 1U, 0x11U, true); /* compile_unit */
    attr(bytes, 0x25U, 0x08U); /* producer: string */
    attr(bytes, 0x03U, 0x08U); /* name */
    attr(bytes, 0x1BU, 0x08U); /* comp_dir */
    attr(bytes, 0x13U, 0x05U); /* language: data2 */
    attr(bytes, 0x10U, 0x17U); /* stmt_list: sec_offset */
    attr(bytes, 0U, 0U);
    subprogram_abbreviation(bytes, 2U, true); subprogram_abbreviation(bytes, 5U, false);
    for (unsigned i = 0U; i < 2U; ++i) {
        abbreviation(bytes, i == 0U ? 3U : 6U, 5U, false); /* formal_parameter */
        attr(bytes, 3U, 8U); attr(bytes, 0x49U, 0x13U);
        attr(bytes, 0x3AU, 0x0FU); attr(bytes, 0x3BU, 0x0FU); attr(bytes, 0x39U, 0x0FU);
        if (i != 0U) attr(bytes, 2U, 0x17U); /* location list */
        attr(bytes, 0U, 0U);
    }
    abbreviation(bytes, 4U, 0x18U, false); attr(bytes, 0U, 0U);
    abbreviation(bytes, 7U, 0x0BU, true); attr(bytes, 0x55U, 0x17U); attr(bytes, 0U, 0U);
    abbreviation(bytes, 8U, 0x34U, false);
    attr(bytes, 3U, 8U); attr(bytes, 0x49U, 0x13U);
    attr(bytes, 0x3AU, 0x0FU); attr(bytes, 0x3BU, 0x0FU); attr(bytes, 0x39U, 0x0FU);
    attr(bytes, 2U, 0x17U); attr(bytes, 0U, 0U);
    cinder_debug_type_abbreviations(bytes); cinder_dwarf_uleb(bytes, 0U);
}

static void line_header(CinderBytes *bytes, const CinderDebugFiles *files) {
    cinder_bytes_put32(bytes, 0U); cinder_bytes_put16(bytes, 4U);
    size_t length_at = bytes->len; cinder_bytes_put32(bytes, 0U);
    size_t header_at = bytes->len;
    const unsigned char state[] = {1U, 1U, 1U, 251U, 14U, 13U,
                                  0U, 1U, 1U, 1U, 1U, 0U, 0U, 0U, 1U, 0U, 0U, 1U};
    cinder_bytes_append(bytes, state, CINDER_ARRAY_LEN(state));
    cinder_bytes_put8(bytes, 0U); /* no separate include-directory table */
    for (size_t i = 0U; i < files->names.len; ++i) {
        cinder_dwarf_string(bytes, files->names.data[i]);
        cinder_dwarf_uleb(bytes, 0U); cinder_dwarf_uleb(bytes, 0U); cinder_dwarf_uleb(bytes, 0U);
    }
    cinder_bytes_put8(bytes, 0U);
    cinder_bytes_patch32(bytes, length_at, (uint32_t)(bytes->len - header_at));
}

static void line_program(CinderMachineObject *object, size_t section, const CinderDebugFunction *function, CinderDebugFiles *files, CinderSourceManager *sources) {
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
        size_t file = cinder_debug_file(files, cinder_loc_name(sources, loc));
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

bool cinder_debug_location_valid(CinderSourceManager *sources, CinderLoc loc) {
    const CinderSourceFile *file = cinder_source_get(sources, loc.file);
    if (file == NULL || !file->valid || loc.offset > file->size || loc.frame > sources->frames.len) return false;
    return loc.frame == 0U || sources->frames.data[loc.frame - 1U].file == loc.file;
}

static const CinderDebugParameter *parameter_location(const CinderMachineObject *object, size_t function, size_t parameter) {
    for (size_t i = 0U; i < object->debug_parameters.len; ++i) {
        const CinderDebugParameter *entry = &object->debug_parameters.data[i];
        if (entry->symbol == function && entry->parameter == parameter) return entry;
    }
    return NULL;
}

static void location_entry(CinderMachineObject *object, size_t section, size_t begin, size_t end, int reg, int offset) {
    CinderBytes *bytes = &object->debug_sections.data[section].bytes;
    cinder_bytes_put64(bytes, begin); cinder_bytes_put64(bytes, end);
    CinderBytes expression = {NULL, 0U, 0U};
    if (reg >= 0) cinder_bytes_put8(&expression, (uint8_t)(0x50U + (unsigned)reg));
    else { cinder_bytes_put8(&expression, 0x91U); cinder_dwarf_sleb(&expression, offset); }
    cinder_bytes_put16(bytes, (uint16_t)expression.len); cinder_bytes_append(bytes, expression.data, expression.len);
    free(expression.data);
}

static size_t location_list(CinderMachineObject *object, size_t section, const CinderDebugFunction *function, const CinderDebugParameter *parameter) {
    const char *symbol = object->defined_symbols.data[function->symbol];
    CinderBytes *bytes = &object->debug_sections.data[section].bytes; size_t offset = bytes->len;
    cinder_bytes_put64(bytes, UINT64_MAX); relocate(object, section, symbol, 1);
    size_t begin = function->prologue_end - object->symbol_offsets.data[function->symbol];
    if (parameter->reg >= 0) location_entry(object, section, 0U, begin, parameter->reg, 0);
    else begin = 0U;
    location_entry(object, section, begin, object->symbol_sizes.data[function->symbol], -1, parameter->cfa_offset);
    cinder_bytes_put64(bytes, 0U); cinder_bytes_put64(bytes, 0U);
    return offset;
}

static bool source_map_valid(const CinderMachineObject *object, CinderSourceManager *sources, CinderDiagnostics *diags) {
    size_t previous_row = 0U;
    for (size_t i = 0U; i < object->debug_functions.len; ++i) {
        const CinderDebugFunction *function = &object->debug_functions.data[i];
        if (function->symbol != i || i >= object->symbol_offsets.len || i >= object->symbol_sizes.len ||
            function->first_row != previous_row || function->end_row <= function->first_row || function->end_row > object->debug_rows.len ||
            !cinder_debug_location_valid(sources, function->loc)) goto invalid;
        size_t start = object->symbol_offsets.data[i], size = object->symbol_sizes.data[i];
        if (start > object->text.len || size == 0U || size > object->text.len - start ||
            function->prologue_end < start || function->prologue_end >= start + size ||
            object->debug_rows.data[function->first_row].offset != start) goto invalid;
        size_t previous_pc = start;
        bool has_prologue = false;
        for (size_t row = function->first_row; row < function->end_row; ++row) {
            const CinderDebugRow *entry = &object->debug_rows.data[row];
            if (!cinder_debug_location_valid(sources, entry->loc) || (row > function->first_row && entry->offset <= previous_pc) || entry->offset >= start + size) goto invalid;
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

int cinder_debug_finish(CinderMachineObject *object, const CinderIRModule *module, CinderSourceManager *sources, CinderDiagnostics *diags) {
    if (!object->debug_enabled) return 0;
    if (module == NULL || module->types == NULL || module->functions.len != object->defined_symbols.len ||
        sources->files.len == 0U || object->debug_functions.len != object->defined_symbols.len ||
        object->symbol_offsets.len != object->defined_symbols.len || object->symbol_sizes.len != object->defined_symbols.len ||
        object->symbol_globals.len != object->defined_symbols.len) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "source debugging requires a complete source-function map"); return 1;
    }
    for (size_t i = 0U; i < module->functions.len; ++i) {
        const CinderIRFunction *function = &module->functions.data[i];
        if (function->name == NULL || object->defined_symbols.data[i] == NULL || strcmp(function->name, object->defined_symbols.data[i]) != 0 ||
            function->type == NULL || function->type->kind != TYPE_FUNCTION || function->params.len != function->type->params.len) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "source debug function has no matching typed IR declaration"); return 1;
        }
        for (size_t p = 0U; p < function->params.len; ++p) if (function->params.data[p] == NULL ||
            function->params.data[p]->type == NULL || !cinder_debug_location_valid(sources, function->params.data[p]->loc)) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "source debug parameter has no valid type or location"); return 1;
        }
    }
    for (size_t i = 0U; i < object->debug_parameters.len; ++i) {
        const CinderDebugParameter *entry = &object->debug_parameters.data[i];
        if (entry->symbol >= module->functions.len || entry->parameter >= module->functions.data[entry->symbol].params.len ||
            !module->functions.data[entry->symbol].params.data[entry->parameter]->unchanged ||
            module->functions.data[entry->symbol].params.data[entry->parameter]->type->kind == TYPE_STRUCT ||
            module->functions.data[entry->symbol].params.data[entry->parameter]->type->kind == TYPE_UNION ||
            module->functions.data[entry->symbol].params.data[entry->parameter]->type->size == 0U ||
            module->functions.data[entry->symbol].params.data[entry->parameter]->type->size > 8U ||
            (entry->reg != -1 && entry->reg != 1 && entry->reg != 2 && entry->reg != 4 && entry->reg != 5 && entry->reg != 8 && entry->reg != 9 && (entry->reg < 17 || entry->reg > 24)) ||
            entry->cfa_offset % 8 != 0 || (entry->reg < 0 ? entry->cfa_offset < 0 : entry->cfa_offset > -24)) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "debug parameter has an invalid ABI value location"); return 1;
        }
        for (size_t p = 0U; p < i; ++p) if (object->debug_parameters.data[p].symbol == entry->symbol && object->debug_parameters.data[p].parameter == entry->parameter) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "debug parameter has duplicate ABI locations"); return 1;
        }
    }
    for (size_t i = 0U; i < object->debug_sections.len; ++i) {
        const char *name = object->debug_sections.data[i].name;
        if (name == NULL || strcmp(name, ".debug_line") == 0 || strcmp(name, ".debug_info") == 0 || strcmp(name, ".debug_abbrev") == 0 || strcmp(name, ".debug_loc") == 0 || strcmp(name, ".debug_ranges") == 0) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "source debug sections have already been finalized or are invalid"); return 1;
        }
    }
    if (!source_map_valid(object, sources, diags) || cinder_debug_scopes_valid(object, module, sources, diags) != 0) return 1;
    CinderDebugFiles files = {{NULL, 0U, 0U}};
    const char *unit_name = cinder_source_display_path(sources, cinder_source_name(sources, 1U));
    (void)cinder_debug_file(&files, unit_name);
    for (size_t i = 0U; i < object->debug_functions.len; ++i)
        (void)cinder_debug_file(&files, cinder_loc_name(sources, object->debug_functions.data[i].loc));
    for (size_t i = 0U; i < object->debug_rows.len; ++i)
        (void)cinder_debug_file(&files, cinder_loc_name(sources, object->debug_rows.data[i].loc));
    for (size_t f = 0U; f < module->functions.len; ++f)
        for (size_t p = 0U; p < module->functions.data[f].params.len; ++p)
            (void)cinder_debug_file(&files, cinder_loc_name(sources, module->functions.data[f].params.data[p]->loc));
    for (size_t f = 0U; f < module->functions.len; ++f)
        for (size_t l = 0U; l < module->functions.data[f].debug_locals.len; ++l)
            (void)cinder_debug_file(&files, cinder_loc_name(sources, module->functions.data[f].debug_locals.data[l].loc));
    char *physical_directory = realpath(".", NULL);
    if (physical_directory == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot resolve debug compilation directory"); free(files.names.data); return 1; }
    const char *directory = cinder_source_display_path(sources, physical_directory); free(physical_directory);
    size_t line = cinder_debug_section(object, ".debug_line", 1U);
    size_t abbrev = cinder_debug_section(object, ".debug_abbrev", 1U);
    size_t info = cinder_debug_section(object, ".debug_info", 1U);
    size_t locations = cinder_debug_section(object, ".debug_loc", 1U);
    size_t ranges = cinder_debug_section(object, ".debug_ranges", 1U);
    abbreviations(&object->debug_sections.data[abbrev].bytes);
    CinderBytes *line_bytes = &object->debug_sections.data[line].bytes;
    line_header(line_bytes, &files);
    for (size_t i = 0U; i < object->debug_functions.len; ++i)
        line_program(object, line, &object->debug_functions.data[i], &files, sources);
    cinder_bytes_patch32(line_bytes, 0U, (uint32_t)(line_bytes->len - 4U));
    CinderBytes *bytes = &object->debug_sections.data[info].bytes;
    CinderDebugTypes types; memset(&types, 0, sizeof(types)); types.bytes = bytes;
    types.index_type = module->types->ulong_type;
    cinder_bytes_put32(bytes, 0U); cinder_bytes_put16(bytes, 4U);
    relocate(object, info, ".debug_abbrev", 10); cinder_bytes_put8(bytes, 8U);
    cinder_dwarf_uleb(bytes, 1U); cinder_dwarf_string(bytes, "Cinder " CINDER_VERSION); cinder_dwarf_string(bytes, unit_name); cinder_dwarf_string(bytes, directory);
    cinder_bytes_put16(bytes, 0x0CU); /* DW_LANG_C99: DWARF4's C language profile */
    relocate(object, info, ".debug_line", 10);
    for (size_t i = 0U; i < object->debug_functions.len; ++i) {
        const CinderDebugFunction *function = &object->debug_functions.data[i];
        CinderLoc loc = function->loc; cinder_loc_linecol(sources, &loc);
        const CinderIRFunction *ir = &module->functions.data[i];
        bool has_parameters = ir->params.len != 0U || ir->type->variadic || ir->debug_locals.len != 0U;
        cinder_dwarf_uleb(bytes, has_parameters ? 2U : 5U); cinder_dwarf_string(bytes, object->defined_symbols.data[function->symbol]);
        cinder_dwarf_uleb(bytes, cinder_debug_file(&files, cinder_loc_name(sources, loc)));
        cinder_dwarf_uleb(bytes, loc.line); cinder_dwarf_uleb(bytes, loc.column);
        relocate(object, info, object->defined_symbols.data[function->symbol], 1);
        cinder_bytes_put64(bytes, object->symbol_sizes.data[function->symbol]);
        cinder_bytes_put8(bytes, object->symbol_globals.data[function->symbol] ? 1U : 0U);
        cinder_dwarf_uleb(bytes, 1U); cinder_bytes_put8(bytes, 0x9CU); /* call_frame_cfa */
        cinder_debug_type_reference(&types, ir->type->return_type); cinder_bytes_put8(bytes, 1U);
        for (size_t p = 0U; p < ir->params.len; ++p) {
            const CinderIRParameter *parameter = ir->params.data[p];
            const CinderDebugParameter *location = parameter_location(object, i, p);
            CinderLoc declared = parameter->loc; cinder_loc_linecol(sources, &declared);
            cinder_dwarf_uleb(bytes, location == NULL ? 3U : 6U); cinder_dwarf_string(bytes, parameter->name == NULL ? "" : parameter->name);
            cinder_debug_type_reference(&types, parameter->type);
            cinder_dwarf_uleb(bytes, cinder_debug_file(&files, cinder_loc_name(sources, declared)));
            cinder_dwarf_uleb(bytes, declared.line); cinder_dwarf_uleb(bytes, declared.column);
            if (location != NULL) {
                size_t offset = location_list(object, locations, function, location);
                cinder_debug_offset(object, info, ".debug_loc", 10, (int64_t)offset);
            }
        }
        if (ir->type->variadic) cinder_dwarf_uleb(bytes, 4U);
        cinder_debug_scope_entries(object, info, ranges, locations, function, ir, &files, sources, &types);
        if (has_parameters) cinder_dwarf_uleb(bytes, 0U);
    }
    int types_failed = cinder_debug_types_finish(&types, diags);
    cinder_dwarf_uleb(bytes, 0U);
    free(files.names.data);
    if (types_failed) return 1;
    if (bytes->len > UINT32_MAX || line_bytes->len > UINT32_MAX) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "source debug data exceeds the DWARF32 range"); return 1;
    }
    cinder_bytes_patch32(bytes, 0U, (uint32_t)(bytes->len - 4U));
    return 0;
}
