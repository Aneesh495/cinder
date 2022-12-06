#include "cinder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cli_error(const CinderOptions *options, const char *message, const char *argument) {
    CinderSourceManager sources; cinder_sources_init(&sources);
    CinderDiagnostics diags; cinder_diags_init(&diags); diags.json = options->json_diagnostics;
    cinder_diag(&diags, CINDER_ERROR, (CinderLoc){0}, "%s%s%s", message, argument == NULL ? "" : ": ", argument == NULL ? "" : argument);
    cinder_diag_print(&diags, &sources, stderr); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources);
}

int main(int argc, char **argv) {
    CinderOptions options;
    memset(&options, 0, sizeof(options));
    options.optimization = 0;
    CINDER_VEC_TYPE(const char *) inputs = {NULL, 0U, 0U};
    CINDER_VEC_TYPE(const char *) includes = {NULL, 0U, 0U};
    CINDER_VEC_TYPE(const char *) defines = {NULL, 0U, 0U};
    CINDER_VEC_TYPE(const char *) maps = {NULL, 0U, 0U};
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0) break;
        if (strcmp(argv[i], "--diagnostics=json") == 0) options.json_diagnostics = true;
        else if (strcmp(argv[i], "--diagnostics=text") == 0) options.json_diagnostics = false;
        else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "-D") == 0 || strcmp(argv[i], "-MF") == 0 || strcmp(argv[i], "-MQ") == 0 || strcmp(argv[i], "--explorer") == 0 || strcmp(argv[i], "--pass-stats") == 0 || strcmp(argv[i], "--pass-trace") == 0) ++i;
    }
    if (argc == 1) { cinder_print_help(stderr); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; }
    bool positional = false;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (positional) { cinder_vec_push((CinderVec *)&inputs, &arg); continue; }
        if (strcmp(arg, "--") == 0) { positional = true; continue; }
        if (strncmp(arg, "-ffile-prefix-map=", sizeof("-ffile-prefix-map=") - 1U) == 0 || strncmp(arg, "-fmacro-prefix-map=", sizeof("-fmacro-prefix-map=") - 1U) == 0) { cinder_vec_push((CinderVec *)&maps, &arg); continue; }
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) { cinder_print_help(stdout); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 0; }
        if (strcmp(arg, "--version") == 0) { cinder_print_version(stdout); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 0; }
        if (strcmp(arg, "-E") == 0) { options.preprocess_only = true; continue; }
        if (strcmp(arg, "-S") == 0) { options.emit_assembly = true; continue; }
        if (strcmp(arg, "-c") == 0) { options.emit_object = true; continue; }
        if (strcmp(arg, "-M") == 0) { options.emit_dependencies = true; options.dependencies_only = true; continue; }
        if (strcmp(arg, "-MD") == 0) { options.emit_dependencies = true; continue; }
        if (strncmp(arg, "-MF", 3U) == 0 || strncmp(arg, "-MQ", 3U) == 0) {
            const char *path = arg[3] == '\0' && i + 1 < argc ? argv[++i] : arg + 3;
            if (*path == '\0') { cli_error(&options, "dependency option requires a path", arg); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; }
            if (arg[2] == 'F') options.dependency_file = path; else options.dependency_target = path;
            continue;
        }
        if (strncmp(arg, "-std=", 5U) == 0 || strncmp(arg, "--std=", 6U) == 0) {
            const char *profile = arg + (arg[1] == '-' ? 6U : 5U);
            if (strcmp(profile, "c17-core") != 0 && strcmp(profile, "c17") != 0) { cli_error(&options, "unsupported language profile", profile); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; }
            continue;
        }
        if (strncmp(arg, "--diagnostics=", 14U) == 0) {
            if (strcmp(arg + 14, "json") != 0 && strcmp(arg + 14, "text") != 0) { cli_error(&options, "unsupported diagnostic format", arg + 14); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; }
            continue;
        }
        if (strcmp(arg, "-fsyntax-only") == 0) { options.syntax_only = true; continue; }
        if (strcmp(arg, "-fverify-each") == 0) { options.verify_each = true; continue; }
        if (strcmp(arg, "-g") == 0) { options.debug = true; continue; }
        if (strcmp(arg, "-O0") == 0) { options.optimization = 0; continue; }
        if (strcmp(arg, "-O1") == 0) { options.optimization = 1; continue; }
        if (strcmp(arg, "-O2") == 0) { options.optimization = 2; continue; }
        if (strcmp(arg, "--dump-tokens") == 0) { options.dump_tokens = true; continue; }
        if (strcmp(arg, "--dump-hir") == 0) { options.dump_hir = true; continue; }
        if (strcmp(arg, "--dump-ast") == 0) { options.dump_ast = true; continue; }
        if (strcmp(arg, "--serialize-ir") == 0) { options.serialize_ir = true; continue; }
        if (strcmp(arg, "--emit-ir") == 0) { options.dump_ir = true; continue; }
        if (strcmp(arg, "--dump-mir") == 0) { options.dump_mir = true; continue; }
        if (strcmp(arg, "--dump-passes") == 0) { options.dump_passes = true; continue; }
        if (strcmp(arg, "--pass-stats") == 0 || strcmp(arg, "--pass-trace") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') { cli_error(&options, "pass inspection option requires a path", arg); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; }
            const char *path = argv[++i];
            if (strcmp(arg, "--pass-stats") == 0) options.pass_stats = path; else options.pass_trace = path;
            continue;
        }
        if (strcmp(arg, "--dump-regalloc") == 0) { options.dump_regalloc = true; continue; }
        if (strcmp(arg, "--interpret") == 0) { options.interpret = true; continue; }
        if (strcmp(arg, "--explorer") == 0) { if (i + 1 >= argc) { cli_error(&options, "--explorer requires a directory", NULL); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; } options.explorer = argv[++i]; continue; }
        if (strncmp(arg, "-I", 2U) == 0) {
            const char *dir = arg[2] == '\0' && i + 1 < argc ? argv[++i] : arg + 2;
            if (*dir == '\0') { cli_error(&options, "-I requires a directory", NULL); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; }
            cinder_vec_push((CinderVec *)&includes, &dir); continue;
        }
        if (strncmp(arg, "-D", 2U) == 0) {
            const char *define = arg[2] == '\0' && i + 1 < argc ? argv[++i] : arg + 2;
            if (*define == '\0') { cli_error(&options, "-D requires a macro definition", NULL); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; }
            cinder_vec_push((CinderVec *)&defines, &define); continue;
        }
        if (strcmp(arg, "-o") == 0) { if (i + 1 >= argc) { cli_error(&options, "-o requires a path", NULL); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; } options.output = argv[++i]; continue; }
        if (strncmp(arg, "-o", 2U) == 0 && arg[2] != '\0') { options.output = arg + 2; continue; }
        if (arg[0] == '-') { cli_error(&options, "unsupported option", arg); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; }
        cinder_vec_push((CinderVec *)&inputs, &arg);
    }
    if (inputs.len == 0U) { cli_error(&options, "no input file", NULL); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2; }
    if ((options.dependency_file != NULL || options.dependency_target != NULL) && !options.emit_dependencies) {
        cli_error(&options, "dependency paths require -M or -MD", NULL); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2;
    }
    if (options.dependencies_only && (options.dump_tokens || options.dump_ast || options.dump_hir || options.dump_ir || options.serialize_ir || options.dump_mir || options.dump_regalloc || options.dump_passes || options.pass_stats != NULL || options.pass_trace != NULL || options.interpret || options.explorer != NULL)) {
        cli_error(&options, "-M cannot share standard output with stage inspection", NULL); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2;
    }
    if ((options.preprocess_only || options.syntax_only) && (options.dump_passes || options.pass_stats != NULL || options.pass_trace != NULL)) {
        cli_error(&options, "pass inspection requires IR lowering", NULL); free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return 2;
    }
    options.input = inputs.data[0]; options.inputs = inputs.data; options.input_count = inputs.len;
    const char *runtime_include = CINDER_RUNTIME_INCLUDE;
    cinder_vec_push((CinderVec *)&includes, &runtime_include);
    options.include_dirs = includes.data; options.include_count = includes.len; options.defines = defines.data; options.define_count = defines.len;
    options.prefix_maps = maps.data; options.prefix_map_count = maps.len;
    int result = cinder_driver_run(&options);
    free(inputs.data); free(includes.data); free(defines.data); free(maps.data); return result;
}
