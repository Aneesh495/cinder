#include "cinder.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

static const char *default_object_name(const char *input) {
    static char path[4096];
    const char *slash = strrchr(input, '/');
    const char *base = slash == NULL ? input : slash + 1;
    size_t length = strlen(base);
    if (length > 2U && strcmp(base + length - 2U, ".c") == 0) length -= 2U;
    if (length + 3U >= sizeof(path)) return "cinder.o";
    memcpy(path, base, length); memcpy(path + length, ".o", 3U); return path;
}

static int write_linked(const char *object_path, const char *output) {
#if defined(__linux__)
    pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) { char *const args[] = {(char *)"cc", (char *)object_path, (char *)"-o", (char *)output, NULL}; execvp(args[0], args); _exit(127); }
    int status = 0; if (waitpid(child, &status, 0) < 0) return 1; return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
#else
    (void)object_path; (void)output;
    return 2;
#endif
}

void cinder_print_help(FILE *out) {
    fputs("Cinder c17-core compiler\n\nUsage: cindercc [options] file.c\n\n", out);
    fputs("  -E                 preprocess only\n  -S                 emit x86-64 assembly\n  -c                 emit ELF64 relocatable object\n  -o PATH             output path\n  -I DIR              quoted/angle include directory\n  -DNAME[=VALUE]      define a preprocessing macro\n  -fsyntax-only       stop after semantic analysis\n  -O0/-O1/-O2        select conservative optimization level\n  --dump-tokens       print preprocessing tokens\n  --dump-ast          print parsed/typed AST\n  --emit-ir           print typed CFG IR\n  --dump-mir          print machine lowering boundary\n  --dump-regalloc     print allocation intervals and frame\n  --interpret         execute main in the independent IR interpreter\n  --explorer DIR      write an offline stage summary report\n  -fverify-each       verify IR at each boundary\n  -g                  request debug profile (currently source contract only)\n  --help              show this help\n  --version           show compiler version\n", out);
}

void cinder_print_version(FILE *out) { fprintf(out, "cindercc %s (C17-core, ELF64 x86-64 backend)\n", CINDER_VERSION); }

int cinder_driver_run(const CinderOptions *options) {
    CinderSourceManager sources; CinderDiagnostics diags; CinderTokenStream tokens; CinderTypeContext types; CinderAst ast; CinderSema sema; CinderIRModule module; CinderMachineObject machine;
    cinder_sources_init(&sources); cinder_diags_init(&diags); cinder_tokens_init(&tokens); cinder_types_init(&types);
    bool inspection_only = options->output == NULL && !options->emit_assembly && !options->emit_object && (options->dump_tokens || options->dump_ast || options->dump_ir || options->dump_mir || options->dump_regalloc || options->interpret || options->explorer != NULL);
    int result = 1;
    if (cinder_preprocess(&sources, options->input, options->include_dirs, options->include_count, options->defines, options->define_count, &diags) != 0) goto done;
    if (options->preprocess_only) { fputs(sources.preprocessed == NULL ? "" : sources.preprocessed, stdout); result = 0; goto done; }
    if (cinder_lex(&sources, &tokens, &diags) != 0) goto done;
    if (options->dump_tokens) cinder_dump_tokens(&tokens, &sources, stdout);
    if (inspection_only && !options->dump_ast && !options->dump_ir && !options->dump_mir && !options->dump_regalloc && !options->interpret && options->explorer == NULL) { result = 0; goto done; }
    cinder_ast_init(&ast, &types, &tokens, &diags);
    if (cinder_parse(&ast) != 0) goto done_ast;
    if (options->dump_ast) cinder_dump_ast(&ast, &sources, stdout);
    if (inspection_only && !options->dump_ir && !options->dump_mir && !options->dump_regalloc && !options->interpret && options->explorer == NULL) { result = 0; goto done_ast; }
    cinder_sema_init(&sema, &ast, &types, &diags);
    if (cinder_sema_run(&sema) != 0) goto done_sema;
    if (options->syntax_only) { result = 0; goto done_sema; }
    cinder_ir_init(&module, &types);
    if (cinder_lower_ir(&module, &ast, &diags) != 0) goto done_ir;
    if (options->verify_each && cinder_verify_ir(&module, &diags) != 0) goto done_ir;
    if (options->dump_ir) cinder_dump_ir(&module, stdout);
    CinderOptStats stats;
    if (cinder_optimize(&module, options->optimization, &stats, &diags) != 0) goto done_ir;
    if (options->verify_each && cinder_verify_ir(&module, &diags) != 0) goto done_ir;
    if (options->dump_mir) fprintf(stdout, "MIR boundary: %zu functions, target=x86_64-sysv\n", module.functions.len);
    if (options->interpret) { CinderInterpResult interpretation = cinder_interpret(&module, "main", NULL, 0U, 1000000U, &diags); if (!interpretation.valid) goto done_ir; fprintf(stdout, "interpret main => %lld\n", (long long)interpretation.value); result = 0; if (options->explorer == NULL && !options->dump_regalloc) goto done_ir; }
    if (inspection_only && !options->dump_regalloc && options->explorer == NULL) { result = 0; goto done_ir; }
    cinder_machine_init(&machine);
    FILE *assembly = NULL;
    if (options->emit_assembly) { assembly = options->output == NULL || strcmp(options->output, "-") == 0 ? stdout : fopen(options->output, "w"); if (assembly == NULL) { cinder_diag(&diags, CINDER_ERROR, (CinderLoc){0}, "cannot open assembly output '%s': %s", options->output, strerror(errno)); goto done_machine; } }
    for (size_t f = 0U; f < module.functions.len; ++f) {
        CinderAllocation allocation; cinder_alloc_init(&allocation, &module.functions.data[f]); if (cinder_allocate(&allocation, &diags) != 0) { cinder_alloc_destroy(&allocation); goto done_assembly; }
        if (options->dump_regalloc) cinder_dump_regalloc(&allocation, stdout);
        if (cinder_lower_x86(&module.functions.data[f], &allocation, &machine, options->emit_assembly, assembly, &diags) != 0) { cinder_alloc_destroy(&allocation); goto done_assembly; }
        cinder_alloc_destroy(&allocation);
    }
    if (options->explorer != NULL) {
        if (cinder_write_explorer(options->explorer, &tokens, &ast, &module, &machine, &diags) != 0) goto done_machine;
    }
    if (inspection_only) { result = 0; goto done_machine; }
    if (options->emit_assembly) { if (assembly != stdout) fclose(assembly); assembly = NULL; result = 0; goto done_machine; }
    const char *object_path = options->output == NULL ? default_object_name(options->input) : options->output;
    if (options->emit_object || options->output != NULL) {
        if (cinder_write_elf64(&machine, object_path, &diags) != 0) goto done_machine;
        result = 0;
    } else {
        char temporary[4096]; int written = snprintf(temporary, sizeof(temporary), "/tmp/cinder-%ld.o", (long)getpid());
        if (written <= 0 || (size_t)written >= sizeof(temporary) || cinder_write_elf64(&machine, temporary, &diags) != 0) goto done_machine;
#if defined(__linux__)
        const char *binary = options->output == NULL ? "a.out" : options->output; result = write_linked(temporary, binary); unlink(temporary);
#else
        cinder_diag(&diags, CINDER_ERROR, (CinderLoc){0}, "Linux x86-64 linking is unavailable on this host; use -c or -S for cross-target output"); result = 1; unlink(temporary);
#endif
    }
done_assembly:
    if (assembly != NULL && assembly != stdout) fclose(assembly);
done_machine:
    cinder_machine_destroy(&machine);
done_ir:
    cinder_ir_destroy(&module);
done_sema:
    cinder_sema_destroy(&sema);
done_ast:
    cinder_ast_destroy(&ast);
done:
    if (diags.errors != 0U) cinder_diag_print(&diags, &sources, stderr);
    bool ok = result == 0 && diags.errors == 0U;
    cinder_tokens_destroy(&tokens); cinder_types_destroy(&types); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources); return ok ? 0 : 1;
}
