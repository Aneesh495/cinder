#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "regalloc_private.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct CinderExplorer {
    char *directory, *trace;
    bool owns_trace, has_passes;
    size_t functions;
    FILE *stages[EXP_STAGE_COUNT];
    CinderOptStats stats;
};
static const char *stage_names[EXP_STAGE_COUNT] = {"Tokens", "Typed AST", "Typed HIR", "IR before optimization", "Optimized CFG and SSA", "Selected machine programs", "Physical allocation", "CFG liveness", "Control-flow graphs", "Source to machine", "Assembly", "Relocations"};

static void escaped(FILE *out, const char *text, size_t size) {
    for (size_t i = 0U; i < size; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '&') fputs("&amp;", out);
        else if (ch == '<') fputs("&lt;", out);
        else if (ch == '>') fputs("&gt;", out);
        else if (ch == '"') fputs("&quot;", out);
        else if (ch == '\0') fputs("&#xfffd;", out);
        else fputc(ch, out);
    }
}
static void string(FILE *out, const char *text) { escaped(out, text, strlen(text)); }
static char *joined(const char *directory, const char *name) {
    size_t a = strlen(directory), b = strlen(name);
    if (a > SIZE_MAX - b - 2U) return NULL;
    char *path = cinder_alloc(a + b + 2U); (void)snprintf(path, a + b + 2U, "%s/%s", directory, name); return path;
}
CinderExplorer *cinder_explorer_create(const char *directory) {
    CinderExplorer *explorer = cinder_alloc(sizeof(*explorer)); memset(explorer, 0, sizeof(*explorer));
    explorer->directory = cinder_strndup(directory, strlen(directory)); return explorer;
}
FILE *cinder_explorer_stream(CinderExplorer *explorer, CinderExplorerStage stage, CinderDiagnostics *diags) {
    if (explorer == NULL) return NULL;
    if ((unsigned)stage >= EXP_STAGE_COUNT) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "invalid explorer stage"); return NULL; }
    if (explorer->stages[stage] == NULL) {
        explorer->stages[stage] = tmpfile();
        if (explorer->stages[stage] == NULL) cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot capture explorer stage: %s", strerror(errno));
    }
    return explorer->stages[stage];
}
const char *cinder_explorer_trace(CinderExplorer *explorer, const char *requested, CinderDiagnostics *diags) {
    if (requested != NULL) { explorer->trace = cinder_strndup(requested, strlen(requested)); return explorer->trace; }
    const char *temporary = getenv("TMPDIR"); if (temporary == NULL || temporary[0] == '\0') temporary = "/tmp";
    explorer->trace = joined(temporary, "cinder-explorer-XXXXXX");
    if (explorer->trace == NULL || mkdtemp(explorer->trace) == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot create owned explorer trace directory: %s", strerror(errno)); return NULL; }
    explorer->owns_trace = true; return explorer->trace;
}
void cinder_explorer_passes(CinderExplorer *explorer, const CinderOptStats *stats) {
    if (explorer != NULL) { explorer->stats = *stats; explorer->has_passes = true; }
}
static char *pass_path(const CinderExplorer *explorer, size_t index, const char *name, const char *suffix) {
    char leaf[256]; int n = snprintf(leaf, sizeof(leaf), "%02zu-%s.%s", index, name, suffix);
    return n < 0 || (size_t)n >= sizeof(leaf) ? NULL : joined(explorer->trace, leaf);
}
void cinder_explorer_destroy(CinderExplorer *explorer) {
    if (explorer == NULL) return;
    for (unsigned s = 0U; s < EXP_STAGE_COUNT; ++s) if (explorer->stages[s] != NULL) fclose(explorer->stages[s]);
    if (explorer->owns_trace) {
        for (size_t p = 0U; p < explorer->stats.pass_count; ++p) for (unsigned version = 0U; version < 2U; ++version) {
            char *path = pass_path(explorer, p, explorer->stats.passes[p].name, version == 0U ? "before.cir" : "after.cir");
            if (path != NULL) { (void)unlink(path); free(path); }
        }
        char *path = pass_path(explorer, explorer->stats.pass_count, "pipeline", "json");
        if (path != NULL) { (void)unlink(path); free(path); }
        (void)rmdir(explorer->trace);
    }
    free(explorer->trace); free(explorer->directory); free(explorer);
}
int cinder_explorer_snapshot(CinderExplorer *explorer, CinderExplorerStage stage, const void *object, CinderSourceManager *sources, CinderDiagnostics *diags) {
    if (explorer == NULL) return 0;
    FILE *out = cinder_explorer_stream(explorer, stage, diags); if (out == NULL) return 1;
    if (stage == EXP_TOKENS) cinder_dump_tokens(object, sources, out);
    else if (stage == EXP_AST) cinder_dump_ast(object, sources, out);
    else if (stage == EXP_HIR) cinder_dump_hir(object, sources, out);
    else if (stage == EXP_IR_BEFORE || stage == EXP_IR_AFTER) cinder_dump_ir(object, out);
    else if (stage == EXP_ASSEMBLY) return cinder_write_assembly(object, out, diags);
    else { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "invalid generic explorer snapshot"); return 1; }
    if (ferror(out)) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot capture explorer stage"); return 1; }
    return 0;
}
static void location(FILE *out, CinderSourceManager *sources, CinderLoc loc) {
    CinderSourceFile *file = cinder_source_get(sources, loc.file);
    if (file == NULL || !file->valid || loc.offset > file->size) { fputs("unavailable", out); return; }
    size_t physical = 1U; for (size_t i = 0U; i < loc.offset; ++i) if (file->bytes[i] == '\n') ++physical;
    CinderLoc logical = loc; cinder_loc_linecol(sources, &logical);
    fprintf(out, "<a href='#source-%u-%zu'>", loc.file, physical); string(out, cinder_loc_name(sources, logical)); fprintf(out, ":%u:%u</a>", logical.line, logical.column);
    if (loc.origin != 0U && loc.origin <= sources->spans.len) fprintf(out, " <a href='#origin-%u'>origin %u</a>", loc.origin, loc.origin);
}
int cinder_explorer_allocation(CinderExplorer *explorer, const CinderAllocation *allocation, CinderDiagnostics *diags) {
    if (explorer == NULL) return 0;
    FILE *mir = cinder_explorer_stream(explorer, EXP_MIR, diags), *physical = cinder_explorer_stream(explorer, EXP_ALLOCATION, diags);
    FILE *live_out = cinder_explorer_stream(explorer, EXP_LIVENESS, diags), *graph = cinder_explorer_stream(explorer, EXP_CFG, diags);
    if (mir == NULL || physical == NULL || live_out == NULL || graph == NULL) return 1;
    cinder_dump_selected_mir(&allocation->machine, mir); cinder_dump_regalloc(allocation, physical);
    const CinderIRFunction *function = allocation->ir; CinderLiveness live;
    if (cinder_liveness_build(function, &live, diags) != 0) return 1;
    fprintf(live_out, "liveness %s values=%zu\n", function->name, function->value_count);
    for (size_t b = 0U; b < live.blocks; ++b) {
        fprintf(live_out, "  block %zu positions=%zu:%zu in=", b, live.begin[b], live.end[b]);
        for (size_t v = 0U; v < function->value_count; ++v) if (cinder_live_has(live.in + b * live.words, (CinderValueId)v)) fprintf(live_out, " %%v%zu", v);
        fputs(" out=", live_out);
        for (size_t v = 0U; v < function->value_count; ++v) if (cinder_live_has(live.out + b * live.words, (CinderValueId)v)) fprintf(live_out, " %%v%zu", v);
        fputc('\n', live_out);
    }
    cinder_liveness_destroy(&live);
    fputs("<h3>", graph); string(graph, function->name); fputs("</h3>", graph);
    size_t function_index = explorer->functions++;
    size_t height = function->blocks.len * 90U + 20U;
    fprintf(graph, "<svg class='cfg' role='group' aria-label='Actual control-flow graph' viewBox='0 0 640 %zu'>", height);
    fprintf(graph, "<defs><marker id='cfg-arrow-%zu' viewBox='0 0 10 10' refX='9' refY='5' markerWidth='6' markerHeight='6' orient='auto-start-reverse'><path d='M 0 0 L 10 5 L 0 10 z' fill='currentColor'/></marker></defs>", function_index);
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        const CinderIRBlock *block = &function->blocks.data[b]; size_t y = b * 90U + 10U;
        for (size_t s = 0U; s < block->successors.len; ++s) {
            size_t target = block->successors.data[s]; if (target >= function->blocks.len) continue;
            size_t bend = 290U + (b % 8U) * 28U;
            fprintf(graph, "<path d='M 250 %zu C %zu %zu, %zu %zu, 250 %zu' fill='none' stroke='currentColor' marker-end='url(#cfg-arrow-%zu)'/>", y + 25U, bend, y + 25U, bend, target * 90U + 35U, target * 90U + 35U, function_index);
        }
        fprintf(graph, "<a href='#ir-%zu-%zu'><rect x='10' y='%zu' width='240' height='50' rx='6'/><text x='22' y='%zu'>block %zu: %zu operations</text></a>", function_index, b, y, y + 30U, b, block->instructions.len);
    }
    fputs("</svg>", graph); return ferror(mir) || ferror(physical) || ferror(live_out) || ferror(graph) ? 1 : 0;
}
int cinder_explorer_machine(CinderExplorer *explorer, const CinderIRModule *module, const CinderMachineObject *machine, CinderSourceManager *sources, CinderDiagnostics *diags) {
    if (explorer == NULL) return 0;
    if (cinder_explorer_snapshot(explorer, EXP_ASSEMBLY, machine, sources, diags) != 0) return 1;
    FILE *links = cinder_explorer_stream(explorer, EXP_LINKS, diags), *relocs = cinder_explorer_stream(explorer, EXP_RELOCATIONS, diags);
    if (links == NULL || relocs == NULL) return 1;
    for (size_t f = 0U; f < module->functions.len; ++f) {
        const CinderIRFunction *function = &module->functions.data[f]; fputs("<h3>", links); string(links, function->name); fputs("</h3><table><tr><th>IR position</th><th>Operation</th><th>Source / macro origin</th><th>Actual text range</th></tr>", links);
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            fprintf(links, "<tbody id='ir-%zu-%zu'>", f, b);
            const CinderIRBlock *block = &function->blocks.data[b];
            for (size_t i = 0U; i <= block->instructions.len; ++i) {
                CinderLoc loc = i == block->instructions.len ? block->terminator.loc : block->instructions.data[i].loc;
                fprintf(links, "<tr class='searchable'><td>b%zu / ", b);
                if (i == block->instructions.len) fputs("term", links); else fprintf(links, "i%zu", i);
                fputs("</td><td>", links); string(links, i == block->instructions.len ? "terminator" : cinder_ir_op_name(block->instructions.data[i].op)); fputs("</td><td>", links); location(links, sources, loc); fputs("</td><td>", links);
                bool found = false;
                for (size_t p = 0U; p < machine->debug_points.len; ++p) {
                    const CinderDebugPoint *point = &machine->debug_points.data[p];
                    if (point->symbol == f && point->block == b && point->instruction == (i == block->instructions.len ? SIZE_MAX : i)) {
                        fprintf(links, "0x%zx..0x%zx <code data-pc-begin='%zu' data-pc-end='%zu'>", point->begin, point->end, point->begin, point->end);
                        for (size_t pc = point->begin; pc < point->end; ++pc) fprintf(links, "%02x", machine->text.data[pc]);
                        fputs("</code>", links); found = true; break;
                    }
                }
                if (!found) fputs("no emitted instruction", links);
                fputs("</td></tr>", links);
            }
            fputs("</tbody>", links);
        }
        fputs("</table>", links);
    }
    for (size_t f = 0U; f < machine->fixups.len; ++f) {
        const CinderFixup *fix = &machine->fixups.data[f]; fprintf(relocs, "section=%u offset=0x%zx type=%d symbol=", fix->section_kind, fix->offset, fix->type); fputs(fix->symbol, relocs); fprintf(relocs, " addend=%" PRId64 "\n", fix->addend);
    }
    return ferror(links) || ferror(relocs) ? 1 : 0;
}
static void source_view(FILE *out, CinderSourceManager *sources) {
    for (size_t f = 0U; f < sources->files.len; ++f) {
        const CinderSourceFile *file = &sources->files.data[f]; fputs("<details open><summary>", out); string(out, cinder_source_display_path(sources, file->path)); fputs("</summary><pre>", out);
        if (!file->valid) fputs("Source read failed", out);
        size_t start = 0U, line = 1U;
        if (file->valid) for (;;) {
            size_t end = start; while (end < file->size && file->bytes[end] != '\n') ++end;
            fprintf(out, "<span class='searchable line' id='source-%u-%zu'><a href='#source-%u-%zu'>%4zu</a> ", file->id, line, file->id, line, line);
            escaped(out, file->bytes + start, end - start); fputs("</span>", out);
            if (end == file->size) break;
            start = end + 1U; ++line;
        }
        fputs("</pre></details>", out);
    }
}
static void origins_view(FILE *out, CinderSourceManager *sources) {
    fputs("<h3>Include instances</h3><table><tr><th>Frame</th><th>File</th><th>Parent</th><th>Include site</th></tr>", out);
    for (size_t f = 0U; f < sources->frames.len; ++f) {
        const CinderSourceFrame *frame = &sources->frames.data[f]; fprintf(out, "<tr class='searchable' id='frame-%zu'><td>%zu</td><td>", f + 1U, f + 1U); string(out, cinder_source_name(sources, frame->file)); fprintf(out, "</td><td>%u</td><td>", frame->parent); location(out, sources, frame->included_from); fputs("</td></tr>", out);
    }
    fputs("</table><h3>Expansion, spelling and definition chain</h3><table><tr><th>Origin / output bytes</th><th>Expansion</th><th>Spelling</th><th>Definition</th></tr>", out);
    for (size_t i = 0U; i < sources->spans.len; ++i) {
        const CinderSourceSpan *span = &sources->spans.data[i]; fprintf(out, "<tr class='searchable' id='origin-%zu'><td>%zu / %zu..%zu</td><td>", i + 1U, i + 1U, span->begin, span->end); location(out, sources, span->expansion); fputs("</td><td>", out); location(out, sources, span->spelling); fputs("</td><td>", out); location(out, sources, span->definition); fputs("</td></tr>", out);
    }
    fputs("</table><h3>Preprocessed source</h3><pre class='searchable'>", out); if (sources->preprocessed != NULL) escaped(out, sources->preprocessed, sources->preprocessed_size); fputs("</pre>", out);
}
static void base64(FILE *out, const unsigned char *bytes, size_t size) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0U; i < size; i += 3U) {
        unsigned value = (unsigned)bytes[i] << 16U; if (i + 1U < size) value |= (unsigned)bytes[i + 1U] << 8U; if (i + 2U < size) value |= bytes[i + 2U];
        fputc(alphabet[(value >> 18U) & 63U], out); fputc(alphabet[(value >> 12U) & 63U], out); fputc(i + 1U < size ? alphabet[(value >> 6U) & 63U] : '=', out); fputc(i + 2U < size ? alphabet[value & 63U] : '=', out);
    }
}
static int read_trace(const char *path, CinderBytes *bytes) {
    FILE *file = path == NULL ? NULL : fopen(path, "rb"); if (file == NULL) return 1;
    unsigned char buffer[4096]; size_t count;
    while ((count = fread(buffer, 1U, sizeof(buffer), file)) != 0U) { if (bytes->len > 16U * 1024U * 1024U - count) { fclose(file); return 1; } cinder_bytes_append(bytes, buffer, count); }
    int failed = ferror(file) != 0 || bytes->len == 0U; fclose(file); return failed;
}
static int passes_view(FILE *out, const CinderExplorer *explorer, CinderDiagnostics *diags) {
    if (!explorer->has_passes) { fputs("<p>Optimization was not reached.</p>", out); return 0; }
    for (size_t p = 0U; p < explorer->stats.pass_count; ++p) {
        const CinderPassRecord *record = &explorer->stats.passes[p]; CinderBytes before = {0}, after = {0};
        char *a = pass_path(explorer, p, record->name, "before.cir"), *b = pass_path(explorer, p, record->name, "after.cir");
        int bad_before = read_trace(a, &before), bad_after = read_trace(b, &after); free(a); free(b);
        fputs("<details class='pass'><summary class='searchable'>", out); string(out, record->name); fprintf(out, " / events=%u / operations=%zu to %zu / verified=%s,%s</summary><p>", record->transformation_events, record->operations_before, record->operations_after, record->verified_before ? "yes" : "no", record->verified_after ? "yes" : "no"); string(out, record->preconditions); fputs("</p>", out);
        if (!bad_before && !bad_after) {
            size_t prefix = 0U, suffix = 0U;
            while (prefix < before.len && prefix < after.len && before.data[prefix] == after.data[prefix]) ++prefix;
            if (prefix == before.len && prefix == after.len) fputs("<p>Canonical IR unchanged.</p>", out);
            else {
                while (prefix != 0U && before.data[prefix - 1U] != '\n') --prefix;
                while (suffix < before.len - prefix && suffix < after.len - prefix && before.data[before.len - suffix - 1U] == after.data[after.len - suffix - 1U]) ++suffix;
                while (suffix != 0U && ((before.len != suffix && before.data[before.len - suffix - 1U] != '\n') || (after.len != suffix && after.data[after.len - suffix - 1U] != '\n'))) --suffix;
                fprintf(out, "<p>Unchanged context: %zu prefix bytes, %zu suffix bytes.</p><div class='diff'><pre class='removed searchable'>", prefix, suffix); escaped(out, (const char *)before.data + prefix, before.len - prefix - suffix); fputs("</pre><pre class='added searchable'>", out); escaped(out, (const char *)after.data + prefix, after.len - prefix - suffix); fputs("</pre></div>", out);
            }
            fputs("<a download='before.cir' href='data:text/plain;base64,", out); base64(out, before.data, before.len); fputs("'>Download complete before IR</a> <a download='after.cir' href='data:text/plain;base64,", out); base64(out, after.data, after.len); fputs("'>Download complete after IR</a>", out);
        } else {
            fputs("<p>Snapshot unavailable after an interrupted or failed pass.</p>", out);
            if (record->verified_after) cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot read completed explorer pass snapshots");
        }
        fputs("</details>", out); free(before.data); free(after.data);
    }
    return diags->errors == 0U ? 0 : 1;
}
static int stream_view(FILE *out, FILE *input, bool html) {
    if (fflush(input) != 0 || fseek(input, 0L, SEEK_SET) != 0) return 1;
    if (!html) fputs("<pre><span class='searchable code-line'>", out);
    char buffer[4096]; size_t count;
    while ((count = fread(buffer, 1U, sizeof(buffer), input)) != 0U) {
        if (html) fwrite(buffer, 1U, count, out);
        else for (size_t i = 0U; i < count; ++i) {
            if (buffer[i] == '\n') fputs("</span>\n<span class='searchable code-line'>", out);
            else escaped(out, buffer + i, 1U);
        }
    }
    if (!html) fputs("</span></pre>", out);
    return ferror(input) || ferror(out) ? 1 : 0;
}
int cinder_explorer_render(CinderExplorer *explorer, CinderSourceManager *sources, bool successful, CinderDiagnostics *diags) {
    if (mkdir(explorer->directory, 0755U) != 0 && errno != EEXIST) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot create explorer directory: %s", strerror(errno)); return 1; }
    char *path = joined(explorer->directory, "index.html"); if (path == NULL) return 1;
    for (size_t f = 0U; f < sources->files.len; ++f) if (cinder_output_same_path(path, sources->files.data[f].path)) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "explorer output would replace an input source"); free(path); return 1; }
    CinderOutput publication; if (cinder_output_begin(&publication, path, diags) != 0) { free(path); return 1; } FILE *out = publication.stream;
    fputs("<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='Content-Security-Policy' content=\"default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; img-src data:; base-uri 'none'; form-action 'none'\"><title>Cinder compilation explorer</title><style>body{margin:0;color:#243245;background:#f3f5f8;font:15px system-ui}header{padding:24px;background:#15273e;color:white}main{max-width:1500px;margin:auto;padding:24px}nav{display:flex;flex-wrap:wrap;gap:7px;margin:20px 0}button,input{font:inherit;padding:8px 12px;border:1px solid #90a0b5;border-radius:5px}button{cursor:pointer;background:white}button[aria-selected=true]{background:#165c92;color:white}section{background:white;border:1px solid #dbe2eb;padding:20px;overflow:auto;border-radius:8px}pre,code{font:12px/1.6 ui-monospace,monospace}pre{background:#f4f7fa;padding:14px;overflow:auto}table{border-collapse:collapse;width:100%;font-size:13px}th,td{padding:9px;text-align:left;border-bottom:1px solid #dbe2eb;vertical-align:top}td code{display:block;max-width:600px;overflow-wrap:anywhere}a{color:#07599a}summary{cursor:pointer;padding:12px}details{border:1px solid #dbe2eb;margin:12px 0}.line{display:block;scroll-margin:80px}.line>a{color:#52677e;text-decoration:none;margin-right:12px}.hit{outline:2px solid #e9ac22;background:#fff1be}.diff{display:grid;grid-template-columns:1fr 1fr;gap:10px}.removed{background:#fff0ed}.added{background:#edfaf1}.cfg{max-height:700px;width:100%;color:#316796}.cfg rect{fill:#eef5fb;stroke:#7796b4}.cfg text{font:13px monospace;fill:#15273e}section[hidden]{display:none}@media(max-width:800px){.diff{grid-template-columns:1fr}main{padding:10px}}</style></head><body><header><h1>Cinder compilation explorer</h1><p>", out);
    fputs(successful ? "Compilation completed. Views below contain captured compiler stages." : "Compilation failed. Only stages actually reached are available.", out);
    fputs("</p></header><main><label>Search source, stages and pass changes <input id='search' type='search'></label> <button id='next'>Next match</button> <output id='matches' aria-live='polite'></output><nav role='tablist' aria-label='Compiler stages'><button role='tab' aria-controls='source' data-tab='source' aria-selected='true'>Source</button><button role='tab' aria-controls='origins' data-tab='origins'>Macro and include provenance</button><button role='tab' aria-controls='passes' data-tab='passes'>Pass changes</button>", out);
    for (unsigned s = 0U; s < EXP_STAGE_COUNT; ++s) { fprintf(out, "<button role='tab' aria-controls='stage-%u' data-tab='stage-%u'>", s, s); string(out, stage_names[s]); fputs("</button>", out); }
    fputs("<button role='tab' aria-controls='diagnostics' data-tab='diagnostics'>Diagnostics</button></nav><section role='tabpanel' data-pane='source' id='source'><h2>Original source and includes</h2>", out); source_view(out, sources); fputs("</section><section role='tabpanel' data-pane='origins' id='origins' hidden><h2>Macro and include provenance</h2>", out); origins_view(out, sources); fputs("</section><section role='tabpanel' data-pane='passes' id='passes' hidden><h2>Actual optimizer snapshots</h2>", out);
    unsigned initial_errors = diags->errors; (void)passes_view(out, explorer, diags);
    fputs("</section>", out); bool failed = diags->errors != initial_errors;
    for (unsigned s = 0U; s < EXP_STAGE_COUNT; ++s) {
        fprintf(out, "<section role='tabpanel' data-pane='stage-%u' id='stage-%u' hidden><h2>", s, s); string(out, stage_names[s]); fputs("</h2>", out);
        if (explorer->stages[s] == NULL) fputs("<p>Stage not reached in this compilation.</p>", out);
        else if (stream_view(out, explorer->stages[s], s == EXP_CFG || s == EXP_LINKS) != 0) failed = true;
        fputs("</section>", out);
    }
    fputs("<section role='tabpanel' data-pane='diagnostics' id='diagnostics' hidden><h2>Compiler diagnostics</h2><table><tr><th>Severity</th><th>Source</th><th>Message</th></tr>", out);
    for (size_t d = 0U; d < diags->items.len; ++d) { const CinderDiagnostic *item = &diags->items.data[d]; fprintf(out, "<tr class='searchable'><td>%u</td><td>", (unsigned)item->severity); location(out, sources, item->loc); fputs("</td><td>", out); string(out, item->message); fputs("</td></tr>", out); }
    fputs("</table></section></main><script>const panes=[...document.querySelectorAll('[data-pane]')],buttons=[...document.querySelectorAll('[data-tab]')];function show(id){for(const p of panes)p.hidden=p.dataset.pane!==id;for(const b of buttons){const active=b.dataset.tab===id;b.setAttribute('aria-selected',String(active));b.tabIndex=active?0:-1}}show('source');for(const b of buttons)b.onclick=()=>show(b.dataset.tab);document.querySelector('nav').onkeydown=e=>{let i=buttons.indexOf(e.target);if(i<0)return;if(e.key==='ArrowRight')i=(i+1)%buttons.length;else if(e.key==='ArrowLeft')i=(i+buttons.length-1)%buttons.length;else if(e.key==='Home')i=0;else if(e.key==='End')i=buttons.length-1;else return;e.preventDefault();show(buttons[i].dataset.tab);buttons[i].focus()};document.addEventListener('click',e=>{const a=e.target.closest('a[href^=\"#\"]');if(!a)return;const target=document.getElementById(decodeURIComponent(a.getAttribute('href').slice(1)));if(!target)return;const pane=target.closest('[data-pane]');if(pane)show(pane.dataset.pane);for(let d=target.closest('details');d;d=d.parentElement.closest('details'))d.open=true});let hits=[],position=-1;const search=document.getElementById('search'),status=document.getElementById('matches');search.oninput=()=>{for(const h of hits)h.classList.remove('hit');const q=search.value.toLowerCase();hits=q?[...document.querySelectorAll('.searchable')].filter(n=>n.textContent.toLowerCase().includes(q)):[];position=-1;status.textContent=hits.length+' matching entries'};document.getElementById('next').onclick=()=>{if(!hits.length)return;for(const h of hits)h.classList.remove('hit');const h=hits[++position%hits.length];h.classList.add('hit');show(h.closest('[data-pane]').dataset.pane);for(let d=h.closest('details');d;d=d.parentElement.closest('details'))d.open=true;h.scrollIntoView({block:'center'});status.textContent=(position%hits.length+1)+' / '+hits.length};</script></body></html>", out);
    long size = ftell(out); if (size < 0L || size > 64L * 1024L * 1024L) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "explorer report exceeds the 64 MiB view limit"); failed = true; }
    if (failed || ferror(out)) { if (!failed) cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot write complete explorer report"); cinder_output_abort(&publication); free(path); return 1; }
    CinderDiagnostics publication_diags; cinder_diags_init(&publication_diags);
    int result = cinder_output_commit(&publication, &publication_diags);
    for (size_t d = 0U; d < publication_diags.items.len; ++d) {
        const CinderDiagnostic *item = &publication_diags.items.data[d];
        cinder_diag(diags, item->severity, item->loc, "%s", item->message);
    }
    cinder_diags_destroy(&publication_diags); free(path); return result;
}
