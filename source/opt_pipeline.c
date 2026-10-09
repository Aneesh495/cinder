#include "opt_private.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { const char *name, *preconditions; unsigned preserved; int level; } PassContract;
#define SHAPE (ANALYSIS_CFG | ANALYSIS_DOMINANCE | ANALYSIS_LOOPS)
#define ALL_ANALYSES (SHAPE | ANALYSIS_USES | ANALYSIS_EFFECTS | ANALYSIS_LIVENESS)
static const PassContract contracts[OPT_PASS_COUNT] = {
    {"constant-fold", "Exact integer width and valid arithmetic; retain pointers and floating operations", SHAPE, 1},
    {"cfg-simplify", "Proved integer edge; preserve indeterminate condition reads and phi edges", 0U, 1},
    {"mem2reg", "Unaddressed nonvolatile scalar; preserve write permission, lifetime and invalid reads", SHAPE, 0},
    {"sparse-constants", "Executable-edge lattice; no invented indeterminate or memory constants", 0U, 2},
    {"dead-code", "Unused result with total defined operands; retain effects and possible traps", SHAPE, 1},
    {"value-numbering", "Exact integer expression with dominating prior evaluation", SHAPE, 2},
    {"copy-cleanup", "Exact dominating value; phi transfer never becomes an eager undefined read", SHAPE, 1},
    {"local-memory", "Exact local integer address within one block; prior access guards reuse", SHAPE, 1},
    {"loop-motion", "Natural loop with one preheader; defined total integer invariants", SHAPE, 2},
    {"strength-reduction", "Unsigned power of two within width or read-preserving identity", SHAPE, 2},
};

static size_t instruction_count(const CinderIRFunction *function, bool active, bool local) {
    size_t count = 0U;
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            CinderIROp op = function->blocks.data[b].instructions.data[i].op;
            if (active && op == IR_NOP) continue;
            if (local && op != IR_LOCAL_LOAD && op != IR_LOCAL_STORE && op != IR_LOCAL_INIT && op != IR_LOCAL_BEGIN && op != IR_LOCAL_RESET) continue;
            ++count;
        }
    return count;
}

static void dimensions(const CinderIRModule *module, size_t *operations, size_t *blocks) {
    *operations = 0U; *blocks = 0U;
    for (size_t f = 0U; f < module->functions.len; ++f) {
        *operations += instruction_count(&module->functions.data[f], true, false);
        *blocks += module->functions.data[f].blocks.len;
    }
}

static unsigned apply(CinderIRFunction *function, CinderOptPass pass, CinderOptStats *stats, CinderDiagnostics *diags) {
    unsigned events = 0U;
    switch (pass) {
        case OPT_CONSTANT_FOLD: {
            unsigned folded = 0U; events = cinder_fold_constants(function, &folded); stats->constants_folded += folded; break;
        }
        case OPT_CFG_SIMPLIFY: events = cinder_simplify_cfg(function, diags); break;
        case OPT_MEM2REG: {
            size_t before = instruction_count(function, false, false), locals = instruction_count(function, false, true);
            if (cinder_insert_join_phis(function, diags) < 0) break;
            /* Count each rewritten local operation and each newly inserted
             * phi/entry definition. Rewritten begin/reset instructions already
             * occupy an existing position and are counted only once. */
            events = (unsigned)(locals - instruction_count(function, false, true) + instruction_count(function, false, false) - before);
            break;
        }
        case OPT_SPARSE_CONSTANTS: events = cinder_sparse_constants(function, diags); break;
        case OPT_DEAD_CODE: events = cinder_remove_dead_ir(function); stats->dead_instructions_removed += events; break;
        case OPT_VALUE_NUMBERING: events = cinder_number_values(function, diags); break;
        case OPT_COPY_CLEANUP: events = cinder_cleanup_copies(function, diags); break;
        case OPT_LOCAL_MEMORY: events = cinder_forward_local_memory(function); stats->memory_forwarded += events; break;
        case OPT_LOOP_MOTION: events = cinder_move_loop_invariants(function, diags); break;
        case OPT_STRENGTH_REDUCTION: events = cinder_reduce_strength(function); break;
        case OPT_PASS_COUNT: break;
    }
    return events;
}

static char *trace_path(const char *directory, size_t index, const char *name, const char *suffix) {
    size_t length = strlen(directory), tail = strlen(name) + strlen(suffix) + 32U;
    if (length > SIZE_MAX - tail) return NULL;
    char *path = cinder_alloc(length + tail);
    (void)snprintf(path, length + tail, "%s/%02zu-%s.%s", directory, index, name, suffix);
    return path;
}

static int snapshot(const CinderIRModule *module, const char *directory, size_t index, const char *name, const char *suffix, CinderDiagnostics *diags) {
    if (directory == NULL) return 0;
    char *path = trace_path(directory, index, name, suffix);
    if (path == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "pass trace path is too large"); return 1; }
    CinderOutput output; int failed = cinder_output_begin(&output, path, diags);
    if (failed) { free(path); return 1; }
    if (cinder_write_ir(module, output.stream, diags) != 0) { cinder_output_abort(&output); free(path); return 1; }
    failed = cinder_output_commit(&output, diags); free(path); return failed;
}

int cinder_write_pass_stats(const CinderOptStats *stats, FILE *out) {
    fprintf(out, "{\"schema\":1,\"timer\":\"process-cpu-clock\",\"analysis_epoch\":%u,\"functions_changed\":%u,\"passes\":[", stats->analysis_epoch, stats->functions_changed);
    for (size_t p = 0U; p < stats->pass_count; ++p) {
        const CinderPassRecord *record = &stats->passes[p];
        if (p != 0U) fputc(',', out);
        fprintf(out, "{\"id\":%u,\"name\":\"%s\",\"preconditions\":\"%s\",\"preserved_analyses\":%u,\"invalidated_analyses\":%u,\"epoch_before\":%u,\"epoch_after\":%u,\"functions_run\":%u,\"functions_changed\":%u,\"transformation_events\":%u,\"operations_before\":%zu,\"operations_after\":%zu,\"blocks_before\":%zu,\"blocks_after\":%zu,\"verified_before\":%s,\"verified_after\":%s,\"cpu_seconds\":", (unsigned)record->id, record->name, record->preconditions, record->preserved_analyses, record->invalidated_analyses, record->epoch_before, record->epoch_after, record->functions_run, record->functions_changed, record->transformation_events, record->operations_before, record->operations_after, record->blocks_before, record->blocks_after, record->verified_before ? "true" : "false", record->verified_after ? "true" : "false");
        if (record->timer_available) fprintf(out, "%.9f", record->cpu_seconds); else fputs("null", out);
        fputc('}', out);
    }
    fputs("]}\n", out); return ferror(out) ? 1 : 0;
}

int cinder_save_pass_stats(const CinderOptStats *stats, const char *path, CinderDiagnostics *diags) {
    CinderOutput output;
    if (cinder_output_begin(&output, path, diags) != 0) return 1;
    if (cinder_write_pass_stats(stats, output.stream) != 0) { cinder_output_abort(&output); return 1; }
    return cinder_output_commit(&output, diags);
}

static int execute(CinderIRModule *module, CinderOptPass id, const char *directory, CinderOptStats *stats, bool *changed_functions, CinderDiagnostics *diags) {
    size_t index = stats->pass_count;
    CinderPassRecord *record = &stats->passes[stats->pass_count++];
    record->id = id; record->name = contracts[id].name; record->preconditions = contracts[id].preconditions;
    record->epoch_before = stats->analysis_epoch;
    dimensions(module, &record->operations_before, &record->blocks_before);
    record->verified_before = cinder_verify_ir(module, diags) == 0;
    if (!record->verified_before || snapshot(module, directory, index, record->name, "before.cir", diags) != 0) return 1;
    clock_t begin = clock();
    for (size_t f = 0U; f < module->functions.len; ++f) {
        unsigned events = apply(&module->functions.data[f], id, stats, diags);
        ++record->functions_run;
        if (events != 0U) { changed_functions[f] = true; ++record->functions_changed; record->transformation_events += events; }
        if (diags->errors != 0U) return 1;
    }
    clock_t end = clock();
    record->timer_available = begin != (clock_t)-1 && end != (clock_t)-1 && (double)begin >= 0.0 && end >= begin;
    if (record->timer_available) record->cpu_seconds = (double)(end - begin) / (double)CLOCKS_PER_SEC;
    dimensions(module, &record->operations_after, &record->blocks_after);
    stats->instructions_changed += record->transformation_events;
    if (record->blocks_before > record->blocks_after) stats->blocks_removed += (unsigned)(record->blocks_before - record->blocks_after);
    record->preserved_analyses = record->transformation_events == 0U ? ALL_ANALYSES : contracts[id].preserved;
    record->invalidated_analyses = record->transformation_events == 0U ? 0U : ALL_ANALYSES & ~record->preserved_analyses;
    if (record->invalidated_analyses != 0U) ++stats->analysis_epoch;
    record->epoch_after = stats->analysis_epoch;
    record->verified_after = cinder_verify_ir(module, diags) == 0;
    if (!record->verified_after) return 1;
    return snapshot(module, directory, index, record->name, "after.cir", diags);
}

static int pipeline(CinderIRModule *module, int level, bool source_pipeline, const char *only, const char *directory, CinderOptStats *stats, CinderDiagnostics *diags) {
    static const CinderOptPass order[] = {OPT_MEM2REG, OPT_CONSTANT_FOLD, OPT_CFG_SIMPLIFY, OPT_SPARSE_CONSTANTS, OPT_VALUE_NUMBERING, OPT_STRENGTH_REDUCTION, OPT_COPY_CLEANUP, OPT_LOCAL_MEMORY, OPT_LOOP_MOTION, OPT_DEAD_CODE};
    memset(stats, 0, sizeof(*stats));
    CinderOptPass selected = OPT_PASS_COUNT;
    if (only != NULL) {
        for (unsigned p = 0U; p < OPT_PASS_COUNT; ++p) if (strcmp(contracts[p].name, only) == 0) selected = (CinderOptPass)p;
        if (selected == OPT_PASS_COUNT) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "unknown isolated optimizer pass '%s'", only); return 1; }
    }
    bool *changed = cinder_alloc((module->functions.len == 0U ? 1U : module->functions.len) * sizeof(*changed));
    memset(changed, 0, module->functions.len * sizeof(*changed)); int failed = 0;
    for (size_t p = 0U; p < sizeof(order) / sizeof(order[0]); ++p) {
        CinderOptPass id = order[p];
        if (only != NULL ? id != selected : (level < contracts[id].level || (level <= 0 && !source_pipeline))) continue;
        if (execute(module, id, directory, stats, changed, diags) != 0) { failed = 1; break; }
    }
    for (size_t f = 0U; f < module->functions.len; ++f) stats->functions_changed += changed[f];
    free(changed);
    if (!failed && directory != NULL) {
        char *path = trace_path(directory, stats->pass_count, "pipeline", "json");
        if (path == NULL) return 1;
        failed = cinder_save_pass_stats(stats, path, diags); free(path);
    }
    return failed;
}

int cinder_optimize(CinderIRModule *module, int level, CinderOptStats *stats, CinderDiagnostics *diags) { return pipeline(module, level, false, NULL, NULL, stats, diags); }
int cinder_optimize_trace(CinderIRModule *module, int level, const char *directory, CinderOptStats *stats, CinderDiagnostics *diags) { return pipeline(module, level, false, NULL, directory, stats, diags); }
int cinder_optimize_only(CinderIRModule *module, const char *name, CinderOptStats *stats, CinderDiagnostics *diags) { return pipeline(module, 0, false, name, NULL, stats, diags); }
int cinder_optimize_only_trace(CinderIRModule *module, const char *name, const char *directory, CinderOptStats *stats, CinderDiagnostics *diags) { return pipeline(module, 0, false, name, directory, stats, diags); }

int cinder_optimize_source(CinderIRModule *module, int level, const char *directory, CinderOptStats *stats, CinderDiagnostics *diags) { return pipeline(module, level, true, NULL, directory, stats, diags); }
