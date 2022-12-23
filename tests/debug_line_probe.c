#include "cinder.h"

#include <stdio.h>

static void object(CinderMachineObject *machine, CinderLoc loc) {
    cinder_machine_init(machine); machine->debug_enabled = true;
    for (unsigned i = 0U; i < 16U; ++i) cinder_bytes_put8(&machine->text, 0x90U);
    char *name = cinder_strndup("probe", 5U); size_t start = 0U, size = 16U; bool global = true;
    cinder_vec_push((CinderVec *)&machine->defined_symbols, &name);
    cinder_vec_push((CinderVec *)&machine->symbol_offsets, &start);
    cinder_vec_push((CinderVec *)&machine->symbol_sizes, &size);
    cinder_vec_push((CinderVec *)&machine->symbol_globals, &global);
    CinderDebugRow first = {0U, loc}, second = {4U, loc};
    cinder_vec_push((CinderVec *)&machine->debug_rows, &first);
    cinder_vec_push((CinderVec *)&machine->debug_rows, &second);
    CinderDebugFunction function = {0U, loc, 4U, 0U, 2U};
    cinder_vec_push((CinderVec *)&machine->debug_functions, &function);
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    CinderSourceManager sources; cinder_sources_init(&sources);
    CinderFileId file = cinder_source_load(&sources, argv[1], stderr);
    if (file == CINDER_NO_FILE) return 2;
    CinderLoc loc = cinder_loc(file, 0U, 1U);
    for (unsigned mutation = 0U; mutation < 15U; ++mutation) {
        CinderMachineObject machine; object(&machine, loc);
        CinderDebugFunction *function = &machine.debug_functions.data[0];
        CinderDebugRow *row = &machine.debug_rows.data[1];
        switch (mutation) {
            case 0U: function->symbol = 1U; break;
            case 1U: function->first_row = 1U; break;
            case 2U: function->end_row = 3U; break;
            case 3U: function->end_row = 0U; break;
            case 4U: function->prologue_end = 16U; break;
            case 5U: function->prologue_end = 5U; break;
            case 6U: row->offset = 0U; break;
            case 7U: row->offset = 16U; break;
            case 8U: row->loc.file = CINDER_NO_FILE; break;
            case 9U: row->loc.offset = cinder_source_get(&sources, file)->size + 1U; break;
            case 10U: row->loc.frame = (uint32_t)sources.frames.len + 1U; break;
            case 11U: function->loc.file = CINDER_NO_FILE; break;
            case 12U: machine.symbol_offsets.len = 0U; break;
            case 13U: machine.symbol_sizes.data[0] = 0U; break;
            case 14U: machine.symbol_globals.len = 0U; break;
            default: return 2;
        }
        CinderDiagnostics diags; cinder_diags_init(&diags);
        if (cinder_debug_finish(&machine, &sources, &diags) == 0 || diags.errors == 0U || machine.debug_sections.len != 0U || machine.fixups.len != 0U) return 1;
        cinder_diags_destroy(&diags); cinder_machine_destroy(&machine);
    }
    CinderMachineObject machine; object(&machine, loc);
    CinderDiagnostics diags; cinder_diags_init(&diags);
    if (cinder_debug_finish(&machine, &sources, &diags) != 0 || diags.errors != 0U) return 1;
    size_t sections = machine.debug_sections.len, fixups = machine.fixups.len;
    if (cinder_debug_finish(&machine, &sources, &diags) == 0 || diags.errors == 0U ||
        sections != machine.debug_sections.len || fixups != machine.fixups.len) return 1;
    cinder_diags_destroy(&diags); cinder_machine_destroy(&machine); cinder_sources_destroy(&sources);
    puts("Source debug: fifteen malformed maps and repeated finalization rejected before mutation");
    return 0;
}
