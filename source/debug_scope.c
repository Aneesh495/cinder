#include "debug_private.h"
#include "opt_private.h"

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
    for (size_t h = 0U; h < object->debug_local_homes.len; ++h) {
        const CinderDebugLocalHome *home = &object->debug_local_homes.data[h];
        if (home->symbol >= module->functions.len) goto invalid;
        const CinderIRFunction *ir = &module->functions.data[home->symbol];
        if (home->local >= ir->debug_locals.len) goto invalid;
        const CinderIRLocal *local = &ir->debug_locals.data[home->local]; size_t cursor = 0U;
        if (local->type->size == 0U || local->type->size > 8U || local->type->kind == TYPE_STRUCT || local->type->kind == TYPE_UNION || local->type->kind == TYPE_ARRAY) goto invalid;
        for (size_t slot = 0U; slot <= (size_t)local->slot; ++slot) {
            const CinderType *type = ir->local_types.data[slot]; size_t align = cinder_ir_local_alignment(ir, slot);
            if (type == NULL || !cinder_object_alignment_valid(type, align)) goto invalid;
            if (align < 8U) align = 8U;
            size_t size = type->size < 8U ? 8U : type->size;
            if (size > 64U * 1024U * 1024U || cursor > 64U * 1024U * 1024U - size) goto invalid;
            cursor = (cursor + size + align - 1U) & ~(align - 1U);
            if (cursor > 64U * 1024U * 1024U) goto invalid;
        }
        if (home->cfa_offset != -(int)cursor - 16) goto invalid;
        for (size_t before = 0U; before < h; ++before)
            if (object->debug_local_homes.data[before].symbol == home->symbol && object->debug_local_homes.data[before].local == home->local) goto invalid;
        for (size_t b = 0U; b < ir->blocks.len; ++b)
            for (size_t i = 0U; i < ir->blocks.data[b].instructions.len; ++i) {
                const CinderIRInst *inst = &ir->blocks.data[b].instructions.data[i];
                if (inst->op == IR_LOCAL_ADDRESS && inst->slot == local->slot) goto invalid;
            }
    }
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

static bool initialized_after(const CinderIRInst *inst, int slot, bool initialized, const bool *defined) {
    if (inst->slot != slot) return initialized;
    if (inst->op == IR_LOCAL_BEGIN || inst->op == IR_LOCAL_RESET || inst->op == IR_LOCAL_END) return false;
    if (inst->op == IR_LOCAL_INIT || inst->op == IR_LOCAL_STORE) return defined[inst->left];
    return initialized;
}

static bool initialized_in(const CinderIRFunction *ir, const CinderCFGAnalysis *cfg, size_t block, const bool *output) {
    if (block == 0U || !cfg->reachable[block]) return false;
    const CinderIRBlock *entry = &ir->blocks.data[block]; bool found = false;
    for (size_t p = 0U; p < entry->predecessors.len; ++p) {
        size_t predecessor = entry->predecessors.data[p];
        if (!cfg->reachable[predecessor]) continue;
        found = true; if (!output[predecessor]) return false;
    }
    return found;
}

static ScopeRanges value_ranges(const CinderMachineObject *object, const CinderDebugFunction *function, const CinderIRFunction *ir,
                                const CinderIRLocal *local, const CinderCFGAnalysis *cfg, const bool *defined) {
    ScopeRanges ranges = {{NULL, 0U, 0U}};
    bool *output = cinder_alloc(ir->blocks.len * sizeof(*output));
    for (size_t b = 0U; b < ir->blocks.len; ++b) output[b] = cfg->reachable[b];
    /* The greatest fixed point is a must-initialized intersection. Starting
     * loop blocks at bottom would incorrectly lose initialization carried
     * unchanged around a backedge. Entry always starts without local values. */
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t r = 0U; r < cfg->rpo_count; ++r) {
            size_t b = cfg->rpo[r]; const CinderIRBlock *block = &ir->blocks.data[b];
            bool state = initialized_in(ir, cfg, b, output);
            for (size_t i = 0U; i < block->instructions.len; ++i) state = initialized_after(&block->instructions.data[i], local->slot, state, defined);
            if (output[b] != state) { output[b] = state; changed = true; }
        }
    }
    size_t block = SIZE_MAX; bool initialized = false;
    for (size_t p = function->first_point; p < function->end_point; ++p) {
        const CinderDebugPoint *point = &object->debug_points.data[p];
        if (point->block != block) { block = point->block; initialized = initialized_in(ir, cfg, block, output); }
        if (initialized && cfg->reachable[block]) {
            for (int scope = point->scope; scope >= 0; scope = ir->debug_scopes.data[scope].parent)
                if (scope == local->scope) { append_range(&ranges, point->begin, point->end); break; }
        }
        if (point->instruction != SIZE_MAX)
            initialized = initialized_after(&ir->blocks.data[block].instructions.data[point->instruction], local->slot, initialized, defined);
    }
    free(output); return ranges;
}

static void local_entry(CinderMachineObject *object, size_t info, size_t locations, const CinderDebugFunction *function,
                        const CinderIRFunction *ir, size_t index, const CinderCFGAnalysis *cfg, const bool *defined,
                        CinderDebugFiles *files, CinderSourceManager *sources, CinderDebugTypes *types) {
    const CinderIRLocal *local = &ir->debug_locals.data[index];
    CinderBytes *bytes = &object->debug_sections.data[info].bytes;
    cinder_dwarf_uleb(bytes, 8U); cinder_dwarf_string(bytes, local->name); cinder_debug_type_reference(types, local->type);
    CinderLoc loc = local->loc; cinder_loc_linecol(sources, &loc);
    cinder_dwarf_uleb(bytes, cinder_debug_file(files, cinder_loc_name(sources, loc)));
    cinder_dwarf_uleb(bytes, loc.line); cinder_dwarf_uleb(bytes, loc.column);
    CinderBytes *list = &object->debug_sections.data[locations].bytes; size_t offset = list->len;
    cinder_bytes_put64(list, UINT64_MAX); cinder_debug_offset(object, locations, object->defined_symbols.data[function->symbol], 1, 0);
    const CinderDebugLocalHome *home = NULL;
    for (size_t h = 0U; h < object->debug_local_homes.len; ++h)
        if (object->debug_local_homes.data[h].symbol == function->symbol && object->debug_local_homes.data[h].local == index) home = &object->debug_local_homes.data[h];
    if (home != NULL) {
        ScopeRanges ranges = value_ranges(object, function, ir, local, cfg, defined);
        size_t start = object->symbol_offsets.data[function->symbol];
        CinderBytes expression = {NULL, 0U, 0U}; cinder_bytes_put8(&expression, 0x91U); cinder_dwarf_sleb(&expression, home->cfa_offset);
        for (size_t r = 0U; r < ranges.ranges.len; ++r) {
            cinder_bytes_put64(list, ranges.ranges.data[r].begin - start); cinder_bytes_put64(list, ranges.ranges.data[r].end - start);
            cinder_bytes_put16(list, (uint16_t)expression.len); cinder_bytes_append(list, expression.data, expression.len);
        }
        free(expression.data); free(ranges.ranges.data);
    }
    cinder_bytes_put64(list, 0U); cinder_bytes_put64(list, 0U);
    cinder_debug_offset(object, info, ".debug_loc", 10, (int64_t)offset);
}

int cinder_debug_scope_entries(CinderMachineObject *object, size_t info, size_t ranges_section, size_t locations, const CinderDebugFunction *function,
                               const CinderIRFunction *ir, CinderDebugFiles *files, CinderSourceManager *sources, CinderDebugTypes *types, CinderDiagnostics *diags) {
    size_t count = ir->debug_scopes.len;
    if (ir->debug_locals.len == 0U) return 0;
    CinderCFGAnalysis cfg; cinder_cfg_init(&cfg);
    if (cinder_analyze_cfg(ir, &cfg, diags) != 0) { cinder_cfg_destroy(&cfg); return 1; }
    bool *defined = cinder_opt_defined_values(ir);
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
                if ((size_t)ir->debug_locals.data[l].scope == scope) local_entry(object, info, locations, function, ir, l, &cfg, defined, files, sources, types);
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
    free(defined); cinder_cfg_destroy(&cfg); return 0;
}
