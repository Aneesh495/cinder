#include "debug_private.h"

#include <stdlib.h>

typedef struct { size_t begin, end; } ScopeRange;
typedef struct { CINDER_VEC_TYPE(ScopeRange) ranges; } ScopeRanges;
typedef struct { size_t scope, next; bool entered; } ScopeFrame;

int cinder_debug_scopes_valid(const CinderMachineObject *object, const CinderIRModule *module, CinderSourceManager *sources, CinderDiagnostics *diags) {
    size_t previous = 0U;
    for (size_t f = 0U; f < module->functions.len; ++f) {
        const CinderIRFunction *ir = &module->functions.data[f];
        const CinderDebugFunction *function = &object->debug_functions.data[f];
        for (size_t s = 0U; s < ir->debug_scopes.len; ++s) {
            int parent = ir->debug_scopes.data[s].parent;
            if (parent < -1 || (parent >= 0 && (size_t)parent >= s) || !cinder_debug_location_valid(sources, ir->debug_scopes.data[s].loc)) goto invalid;
            unsigned depth = 0U;
            for (int scope = (int)s; scope >= 0; scope = ir->debug_scopes.data[scope].parent)
                if (++depth >= 1024U) goto invalid;
        }
        for (size_t l = 0U; l < ir->debug_locals.len; ++l) {
            const CinderIRLocal *local = &ir->debug_locals.data[l];
            if (local->name == NULL || local->name[0] == '\0' || local->type == NULL || local->slot < 0 ||
                (size_t)local->slot >= ir->local_types.len || ir->local_types.data[local->slot] != local->type ||
                local->scope < 0 || (size_t)local->scope >= ir->debug_scopes.len || !cinder_debug_location_valid(sources, local->loc)) goto invalid;
            for (size_t before = 0U; before < l; ++before) if (ir->debug_locals.data[before].slot == local->slot) goto invalid;
        }
        if (function->first_point != previous || function->end_point > object->debug_points.len) goto invalid;
        size_t point = previous, pc = function->prologue_end;
        for (size_t b = 0U; b < ir->blocks.len; ++b) {
            const CinderIRBlock *block = &ir->blocks.data[b];
            for (size_t i = 0U; i <= block->instructions.len; ++i) {
                if (point >= function->end_point) goto invalid;
                const CinderDebugPoint *entry = &object->debug_points.data[point++];
                int scope = i == block->instructions.len ? block->terminator.debug_scope : block->instructions.data[i].debug_scope;
                if (entry->symbol != f || entry->block != b || entry->instruction != (i == block->instructions.len ? SIZE_MAX : i) ||
                    entry->scope != scope || scope < -1 || (scope >= 0 && (size_t)scope >= ir->debug_scopes.len) ||
                    entry->begin != pc || entry->end < pc || entry->end > object->symbol_offsets.data[f] + object->symbol_sizes.data[f]) goto invalid;
                pc = entry->end;
            }
        }
        if (point != function->end_point || pc != object->symbol_offsets.data[f] + object->symbol_sizes.data[f]) goto invalid;
        previous = point;
    }
    if (previous != object->debug_points.len) goto invalid;
    return 0;
invalid:
    cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "debug lexical scopes, owned locals or instruction boundaries are invalid (nesting limit 1023)");
    return 1;
}

static void append_range(ScopeRanges *ranges, size_t begin, size_t end) {
    if (begin == end) return;
    if (ranges->ranges.len != 0U && ranges->ranges.data[ranges->ranges.len - 1U].end == begin)
        ranges->ranges.data[ranges->ranges.len - 1U].end = end;
    else {
        ScopeRange range = {begin, end}; cinder_vec_push((CinderVec *)&ranges->ranges, &range);
    }
}

static size_t range_list(CinderMachineObject *object, size_t section, const CinderDebugFunction *function, const ScopeRanges *ranges) {
    CinderBytes *bytes = &object->debug_sections.data[section].bytes; size_t offset = bytes->len;
    size_t start = object->symbol_offsets.data[function->symbol];
    cinder_bytes_put64(bytes, UINT64_MAX); cinder_debug_offset(object, section, object->defined_symbols.data[function->symbol], 1, 0);
    for (size_t r = 0U; r < ranges->ranges.len; ++r) {
        cinder_bytes_put64(bytes, ranges->ranges.data[r].begin - start); cinder_bytes_put64(bytes, ranges->ranges.data[r].end - start);
    }
    cinder_bytes_put64(bytes, 0U); cinder_bytes_put64(bytes, 0U); return offset;
}

static void local_entry(CinderMachineObject *object, size_t info, size_t locations, const CinderDebugFunction *function,
                        const CinderIRLocal *local, CinderDebugFiles *files, CinderSourceManager *sources, CinderDebugTypes *types) {
    CinderBytes *bytes = &object->debug_sections.data[info].bytes;
    cinder_dwarf_uleb(bytes, 8U); cinder_dwarf_string(bytes, local->name); cinder_debug_type_reference(types, local->type);
    CinderLoc loc = local->loc; cinder_loc_linecol(sources, &loc);
    cinder_dwarf_uleb(bytes, cinder_debug_file(files, cinder_loc_name(sources, loc)));
    cinder_dwarf_uleb(bytes, loc.line); cinder_dwarf_uleb(bytes, loc.column);
    /* A frame slot alone does not establish a current value. The first scope
     * increment deliberately publishes an empty location list. */
    CinderBytes *list = &object->debug_sections.data[locations].bytes; size_t offset = list->len;
    cinder_bytes_put64(list, UINT64_MAX); cinder_debug_offset(object, locations, object->defined_symbols.data[function->symbol], 1, 0);
    cinder_bytes_put64(list, 0U); cinder_bytes_put64(list, 0U);
    cinder_debug_offset(object, info, ".debug_loc", 10, (int64_t)offset);
}

void cinder_debug_scope_entries(CinderMachineObject *object, size_t info, size_t ranges_section, size_t locations, const CinderDebugFunction *function,
                                const CinderIRFunction *ir, CinderDebugFiles *files, CinderSourceManager *sources, CinderDebugTypes *types) {
    size_t count = ir->debug_scopes.len;
    if (ir->debug_locals.len == 0U) return;
    ScopeRanges *ranges = cinder_alloc(count * sizeof(*ranges));
    size_t *children = cinder_alloc(count * sizeof(*children)), *next = cinder_alloc(count * sizeof(*next));
    bool *active = cinder_alloc(count * sizeof(*active));
    for (size_t s = 0U; s < count; ++s) { ranges[s] = (ScopeRanges){{NULL, 0U, 0U}}; children[s] = SIZE_MAX; next[s] = SIZE_MAX; active[s] = false; }
    for (size_t l = 0U; l < ir->debug_locals.len; ++l) active[ir->debug_locals.data[l].scope] = true;
    for (size_t s = count; s > 0U; --s) {
        int parent = ir->debug_scopes.data[s - 1U].parent;
        if (parent >= 0 && active[s - 1U]) active[parent] = true;
    }
    size_t first = SIZE_MAX;
    for (size_t s = count; s > 0U; --s) {
        size_t id = s - 1U; int parent = ir->debug_scopes.data[id].parent;
        if (!active[id]) continue;
        if (parent < 0) { next[id] = first; first = id; }
        else { next[id] = children[parent]; children[parent] = id; }
    }
    for (size_t p = function->first_point; p < function->end_point; ++p) {
        const CinderDebugPoint *point = &object->debug_points.data[p];
        for (int scope = point->scope; scope >= 0; scope = ir->debug_scopes.data[scope].parent)
            if (active[scope]) append_range(&ranges[scope], point->begin, point->end);
    }
    ScopeFrame *stack = cinder_alloc((count + 1U) * sizeof(*stack)); size_t depth = 0U;
    if (first != SIZE_MAX) stack[depth++] = (ScopeFrame){first, SIZE_MAX, false};
    CinderBytes *bytes = &object->debug_sections.data[info].bytes;
    while (depth != 0U) {
        ScopeFrame *frame = &stack[depth - 1U]; size_t scope = frame->scope;
        if (!frame->entered) {
            frame->entered = true; frame->next = children[scope];
            cinder_dwarf_uleb(bytes, 7U);
            cinder_debug_offset(object, info, ".debug_ranges", 10, (int64_t)range_list(object, ranges_section, function, &ranges[scope]));
            for (size_t l = 0U; l < ir->debug_locals.len; ++l)
                if ((size_t)ir->debug_locals.data[l].scope == scope) local_entry(object, info, locations, function, &ir->debug_locals.data[l], files, sources, types);
        }
        if (frame->next != SIZE_MAX) {
            size_t child = frame->next; frame->next = next[child]; stack[depth++] = (ScopeFrame){child, SIZE_MAX, false};
        } else {
            cinder_dwarf_uleb(bytes, 0U); --depth;
            if (depth == 0U && next[scope] != SIZE_MAX) stack[depth++] = (ScopeFrame){next[scope], SIZE_MAX, false};
        }
    }
    for (size_t s = 0U; s < count; ++s) free(ranges[s].ranges.data);
    free(stack); free(active); free(next); free(children); free(ranges);
}
