#include "cinder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    CinderOptions options;
    memset(&options, 0, sizeof(options));
    options.optimization = 0;
    CINDER_VEC_TYPE(const char *) inputs = {NULL, 0U, 0U};
    CINDER_VEC_TYPE(const char *) includes = {NULL, 0U, 0U};
    CINDER_VEC_TYPE(const char *) defines = {NULL, 0U, 0U};
    if (argc == 1) { cinder_print_help(stderr); free(inputs.data); free(includes.data); free(defines.data); return 2; }
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) { cinder_print_help(stdout); free(inputs.data); free(includes.data); free(defines.data); return 0; }
        if (strcmp(arg, "--version") == 0) { cinder_print_version(stdout); free(inputs.data); free(includes.data); free(defines.data); return 0; }
        if (strcmp(arg, "-E") == 0) { options.preprocess_only = true; continue; }
        if (strcmp(arg, "-S") == 0) { options.emit_assembly = true; continue; }
        if (strcmp(arg, "-c") == 0) { options.emit_object = true; continue; }
        if (strcmp(arg, "-fsyntax-only") == 0) { options.syntax_only = true; continue; }
        if (strcmp(arg, "-fverify-each") == 0) { options.verify_each = true; continue; }
        if (strcmp(arg, "-g") == 0) { options.debug = true; continue; }
        if (strcmp(arg, "-O0") == 0) { options.optimization = 0; continue; }
        if (strcmp(arg, "-O1") == 0) { options.optimization = 1; continue; }
        if (strcmp(arg, "-O2") == 0) { options.optimization = 2; continue; }
        if (strcmp(arg, "--dump-tokens") == 0) { options.dump_tokens = true; continue; }
        if (strcmp(arg, "--dump-ast") == 0) { options.dump_ast = true; continue; }
        if (strcmp(arg, "--serialize-ir") == 0) { options.serialize_ir = true; continue; }
        if (strcmp(arg, "--emit-ir") == 0) { options.dump_ir = true; continue; }
        if (strcmp(arg, "--dump-mir") == 0) { options.dump_mir = true; continue; }
        if (strcmp(arg, "--dump-passes") == 0) { options.dump_passes = true; continue; }
        if (strcmp(arg, "--pass-stats") == 0 || strcmp(arg, "--pass-trace") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') { fprintf(stderr, "cindercc: %s requires a path\n", arg); free(inputs.data); free(includes.data); free(defines.data); return 2; }
            const char *path = argv[++i];
            if (strcmp(arg, "--pass-stats") == 0) options.pass_stats = path; else options.pass_trace = path;
            continue;
        }
        if (strcmp(arg, "--dump-regalloc") == 0) { options.dump_regalloc = true; continue; }
        if (strcmp(arg, "--interpret") == 0) { options.interpret = true; continue; }
        if (strcmp(arg, "--explorer") == 0) { if (i + 1 >= argc) { fprintf(stderr, "cindercc: --explorer requires a directory\n"); free(inputs.data); free(includes.data); free(defines.data); return 2; } options.explorer = argv[++i]; continue; }
        if (strncmp(arg, "-I", 2U) == 0) {
            const char *dir = arg[2] == '\0' && i + 1 < argc ? argv[++i] : arg + 2;
            if (*dir == '\0') { fprintf(stderr, "cindercc: -I requires a directory\n"); free(inputs.data); free(includes.data); free(defines.data); return 2; }
            cinder_vec_push((CinderVec *)&includes, &dir); continue;
        }
        if (strncmp(arg, "-D", 2U) == 0) {
            const char *define = arg[2] == '\0' && i + 1 < argc ? argv[++i] : arg + 2;
            if (*define == '\0') { fprintf(stderr, "cindercc: -D requires a macro definition\n"); free(inputs.data); free(includes.data); free(defines.data); return 2; }
            cinder_vec_push((CinderVec *)&defines, &define); continue;
        }
        if (strcmp(arg, "-o") == 0) { if (i + 1 >= argc) { fprintf(stderr, "cindercc: -o requires a path\n"); free(inputs.data); free(includes.data); free(defines.data); return 2; } options.output = argv[++i]; continue; }
        if (strncmp(arg, "-o", 2U) == 0 && arg[2] != '\0') { options.output = arg + 2; continue; }
        if (arg[0] == '-') { fprintf(stderr, "cindercc: unsupported option '%s'\n", arg); free(inputs.data); free(includes.data); free(defines.data); return 2; }
        cinder_vec_push((CinderVec *)&inputs, &arg);
    }
    if (inputs.len == 0U) { fprintf(stderr, "cindercc: no input file\n"); free(inputs.data); free(includes.data); free(defines.data); return 2; }
    if ((options.preprocess_only || options.syntax_only) && (options.dump_passes || options.pass_stats != NULL || options.pass_trace != NULL)) {
        fputs("cindercc: pass inspection requires IR lowering\n", stderr); free(inputs.data); free(includes.data); free(defines.data); return 2;
    }
    options.input = inputs.data[0]; options.inputs = inputs.data; options.input_count = inputs.len;
    const char *runtime_include = CINDER_RUNTIME_INCLUDE;
    cinder_vec_push((CinderVec *)&includes, &runtime_include);
    options.include_dirs = includes.data; options.include_count = includes.len; options.defines = defines.data; options.define_count = defines.len;
    int result = cinder_driver_run(&options);
    free(inputs.data); free(includes.data); free(defines.data); return result;
}
