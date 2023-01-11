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
    CinderDebugFunction function = {0U, loc, 4U, 0U, 2U, 0U, 1U};
    cinder_vec_push((CinderVec *)&machine->debug_functions, &function);
    CinderDebugPoint point = {0U, 0U, SIZE_MAX, 4U, 16U, 0};
    cinder_vec_push((CinderVec *)&machine->debug_points, &point);
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    CinderSourceManager sources; cinder_sources_init(&sources);
    CinderFileId file = cinder_source_load(&sources, argv[1], stderr);
    if (file == CINDER_NO_FILE) return 2;
    CinderLoc loc = cinder_loc(file, 0U, 1U);
    CinderTypeContext types; cinder_types_init(&types);
    CinderIRModule module; cinder_ir_init(&module, &types);
    CinderIRFunction ir = {0}; ir.name = cinder_strndup("probe", 5U);
    CinderParamVec parameters = {NULL, 0U, 0U}; ir.type = cinder_type_function(&types, types.int_type, &parameters);
    CinderParam type_parameter = {"value", types.int_type, 0U, loc};
    cinder_vec_push((CinderVec *)&ir.type->params, &type_parameter);
    CinderIRParameter *parameter = cinder_arena_alloc(&module.arena, sizeof(*parameter), _Alignof(CinderIRParameter));
    *parameter = (CinderIRParameter){"value", types.int_type, loc, true};
    cinder_vec_push((CinderVec *)&ir.params, &parameter);
    CinderIRScope scope = {-1, loc}; cinder_vec_push((CinderVec *)&ir.debug_scopes, &scope);
    CinderIRBlock block = {0}; block.terminator.debug_scope = 0; cinder_vec_push((CinderVec *)&ir.blocks, &block);
    cinder_vec_push((CinderVec *)&ir.local_types, &types.int_type); cinder_vec_push((CinderVec *)&ir.local_types, &types.int_type); ir.local_count = 2U;
    CinderIRLocal local = {"local", types.int_type, loc, 1, 0}; cinder_vec_push((CinderVec *)&ir.debug_locals, &local);
    cinder_vec_push((CinderVec *)&module.functions, &ir);
    CinderIRFunction *owned = &module.functions.data[0];
    for (unsigned mutation = 0U; mutation < 48U; ++mutation) {
        CinderMachineObject machine; object(&machine, loc);
        if (mutation >= 15U) {
            CinderDebugParameter entry = {0U, 0U, 5, -24}; cinder_vec_push((CinderVec *)&machine.debug_parameters, &entry);
        }
        if (mutation >= 43U) {
            CinderDebugLocalHome home = {0U, 0U, -32}; cinder_vec_push((CinderVec *)&machine.debug_local_homes, &home);
        }
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
            case 15U: machine.debug_parameters.data[0].symbol = 1U; break;
            case 16U: machine.debug_parameters.data[0].parameter = 1U; break;
            case 17U: parameter->unchanged = false; break;
            case 18U: machine.debug_parameters.data[0].reg = 3; break;
            case 19U: machine.debug_parameters.data[0].cfa_offset = -25; break;
            case 20U: machine.debug_parameters.data[0].cfa_offset = 0; break;
            case 21U: {
                CinderDebugParameter copy = machine.debug_parameters.data[0]; cinder_vec_push((CinderVec *)&machine.debug_parameters, &copy); break;
            }
            case 22U: owned->debug_scopes.data[0].parent = 0; break;
            case 23U: owned->debug_scopes.data[0].parent = -2; break;
            case 24U: owned->debug_scopes.data[0].loc.file = CINDER_NO_FILE; break;
            case 25U: owned->debug_locals.data[0].name = NULL; break;
            case 26U: owned->debug_locals.data[0].type = NULL; break;
            case 27U: owned->debug_locals.data[0].slot = -1; break;
            case 28U: owned->debug_locals.data[0].slot = 2; break;
            case 29U: owned->debug_locals.data[0].type = types.long_type; break;
            case 30U: owned->debug_locals.data[0].scope = 1; break;
            case 31U: owned->debug_locals.data[0].loc.file = CINDER_NO_FILE; break;
            case 32U: cinder_vec_push((CinderVec *)&owned->debug_locals, &local); break;
            case 33U: function->first_point = 1U; break;
            case 34U: function->end_point = 2U; break;
            case 35U: machine.debug_points.data[0].block = 1U; break;
            case 36U: machine.debug_points.data[0].instruction = 0U; break;
            case 37U: machine.debug_points.data[0].symbol = 1U; break;
            case 38U: machine.debug_points.data[0].scope = 1; break;
            case 39U: machine.debug_points.data[0].begin = 5U; break;
            case 40U: machine.debug_points.data[0].end = 17U; break;
            case 41U: machine.debug_points.len = 0U; break;
            case 42U: {
                CinderDebugPoint point = machine.debug_points.data[0]; cinder_vec_push((CinderVec *)&machine.debug_points, &point); break;
            }
            case 43U: machine.debug_local_homes.data[0].symbol = 1U; break;
            case 44U: machine.debug_local_homes.data[0].local = 1U; break;
            case 45U: machine.debug_local_homes.data[0].cfa_offset = -17; break;
            case 46U: machine.debug_local_homes.data[0].cfa_offset = -24; break;
            case 47U: {
                CinderDebugLocalHome home = machine.debug_local_homes.data[0]; cinder_vec_push((CinderVec *)&machine.debug_local_homes, &home); break;
            }
            default: return 2;
        }
        CinderDiagnostics diags; cinder_diags_init(&diags);
        if (cinder_debug_finish(&machine, &module, &sources, &diags) == 0 || diags.errors == 0U || machine.debug_sections.len != 0U || machine.fixups.len != 0U) return 1;
        cinder_diags_destroy(&diags); cinder_machine_destroy(&machine);
        parameter->unchanged = true;
        owned->debug_scopes.data[0] = scope; owned->debug_locals.data[0] = local; owned->debug_locals.len = 1U;
    }
    CinderMachineObject machine; object(&machine, loc);
    CinderDiagnostics diags; cinder_diags_init(&diags);
    if (cinder_debug_finish(&machine, &module, &sources, &diags) != 0 || diags.errors != 0U) return 1;
    size_t sections = machine.debug_sections.len, fixups = machine.fixups.len;
    if (cinder_debug_finish(&machine, &module, &sources, &diags) == 0 || diags.errors == 0U ||
        sections != machine.debug_sections.len || fixups != machine.fixups.len) return 1;
    cinder_diags_destroy(&diags); cinder_machine_destroy(&machine); cinder_sources_destroy(&sources);
    cinder_ir_destroy(&module); cinder_types_destroy(&types);
    puts("Source debug: forty-eight malformed maps and repeated finalization rejected before mutation");
    return 0;
}
