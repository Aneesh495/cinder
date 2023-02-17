#include "cinder.h"
#include <stdlib.h>
#include <string.h>

static int compile_buffer(unsigned index) {
    CinderSourceManager sources; CinderDiagnostics diags; CinderTokenStream tokens;
    CinderTypeContext types; CinderAst ast; CinderSema sema; CinderHIR hir; CinderIRModule module;
    cinder_sources_init(&sources); cinder_diags_init(&diags); cinder_tokens_init(&tokens); cinder_types_init(&types);
    sources.buffered_only = true;
    const char *header = "#pragma once\n#define BUFFER_VALUE 7\n#define ADD(a,b) ((a)+(b))\n";
    char input[256]; (void)snprintf(input, sizeof(input), "#include \"header.h\"\n#include <header.h>\nint main(void) { return ADD(BUFFER_VALUE,%u); }\n", index % 41U);
    int result = 1; bool ast_ready = false, hir_ready = false, module_ready = false, sema_ready = false;
    if (cinder_source_buffer(&sources, "/cinder-buffer/header.h", header, strlen(header), &diags) == CINDER_NO_FILE) goto done;
    if (cinder_source_buffer(&sources, "/cinder-buffer/input.c", input, strlen(input), &diags) == CINDER_NO_FILE) goto done;
    const char *directories[] = {"/cinder-buffer"};
    if (cinder_preprocess(&sources, "/cinder-buffer/input.c", directories, 1U, NULL, 0U, &diags) != 0) goto done;
    if (sources.files.len != 2U || sources.frames.len < 2U || sources.spans.len == 0U || cinder_lex(&sources, &tokens, &diags) != 0) goto done;
    cinder_ast_init(&ast, &types, &tokens, &diags); ast_ready = true;
    if (cinder_parse(&ast) != 0) goto done;
    cinder_sema_init(&sema, &ast, &types, &diags); sema_ready = true;
    if (cinder_sema_run(&sema) != 0) goto done;
    cinder_hir_init(&hir, &types); hir_ready = true;
    if (cinder_build_hir(&hir, &ast, &diags) != 0) goto done;
    cinder_ir_init(&module, &types); module_ready = true;
    if (cinder_lower_hir_ir(&module, &hir, &diags) != 0 || cinder_verify_ir(&module, &diags) != 0) goto done;
    CinderInterpResult observed = cinder_interpret(&module, "main", NULL, 0U, 10000U, &diags);
    if (observed.valid && observed.value == (int64_t)(7U + index % 41U)) result = 0;
done:
    if (module_ready) cinder_ir_destroy(&module);
    if (hir_ready) cinder_hir_destroy(&hir);
    if (sema_ready) cinder_sema_destroy(&sema);
    if (ast_ready) cinder_ast_destroy(&ast);
    cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources);
    return result;
}
static int block_external(const char *path) {
    CinderSourceManager sources; CinderDiagnostics diags;
    cinder_sources_init(&sources); cinder_diags_init(&diags); sources.buffered_only = true;
    char input[4096]; int length = snprintf(input, sizeof(input), "#include \"%s\"\nint main(void) { return 0; }\n", path);
    int result = 1;
    if (length < 0 || (size_t)length >= sizeof(input)) goto done;
    if (cinder_source_buffer(&sources, "/cinder-buffer/input.c", input, (size_t)length, &diags) == CINDER_NO_FILE) goto done;
    if (cinder_preprocess(&sources, "/cinder-buffer/input.c", NULL, 0U, NULL, 0U, &diags) == 0 || sources.files.len != 1U || diags.errors == 0U) goto done;
    for (size_t i = 0U; i < diags.items.len; ++i) if (strstr(diags.items.data[i].message, "cannot resolve include") != NULL) result = 0;
done:
    cinder_diags_destroy(&diags); cinder_sources_destroy(&sources); return result;
}
static int reject_bad_buffers(void) {
    CinderSourceManager sources; CinderDiagnostics diags;
    cinder_sources_init(&sources); cinder_diags_init(&diags); int rejected = 0;
    CinderFileId id = cinder_source_buffer(&sources, "owned.c", "int value;", 10U, &diags);
    if (id != 1U) goto done;
    const char *original = sources.files.data[0].bytes;
    if (cinder_source_buffer(&sources, "owned.c", "int changed;", 12U, &diags) == CINDER_NO_FILE && sources.files.data[0].bytes == original && strcmp(original, "int value;") == 0) ++rejected;
    if (cinder_source_buffer(&sources, "missing.c", NULL, 1U, &diags) == CINDER_NO_FILE) ++rejected;
    if (cinder_source_buffer(&sources, "large.c", "", 16U * 1024U * 1024U + 1U, &diags) == CINDER_NO_FILE) ++rejected;
    if (cinder_source_buffer(&sources, NULL, "", 0U, &diags) == CINDER_NO_FILE) ++rejected;
    if (cinder_source_buffer(&sources, "invalid.c", "\xff", 1U, &diags) == CINDER_NO_FILE && sources.files.len == 2U && !sources.files.data[1].valid && diags.items.data[diags.items.len - 1U].loc.file == 2U) ++rejected;
done:
    cinder_diags_destroy(&diags); cinder_sources_destroy(&sources); return rejected == 5 ? 0 : 1;
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    CinderSourceManager baseline; CinderDiagnostics diags;
    cinder_sources_init(&baseline); cinder_diags_init(&diags);
    CinderFileId actual = cinder_source_load_diagnostic(&baseline, argv[2], &diags);
    bool normal_read = actual != CINDER_NO_FILE && baseline.files.len == 1U && strstr(baseline.files.data[0].bytes, "external header") != NULL;
    cinder_diags_destroy(&diags); cinder_sources_destroy(&baseline);
    if (!normal_read) return 1;
    for (unsigned i = 0U; i < 1000U; ++i) if (compile_buffer(i) != 0) return 1;
    if (block_external(argv[1]) != 0 || block_external(argv[2]) != 0 || reject_bad_buffers() != 0) return 1;
    puts("{\"repeated_translation_units\":1000,\"blocked_external_includes\":2,\"rejected_buffers\":5}");
    return 0;
}
