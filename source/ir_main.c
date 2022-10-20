#include "cinder.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *input = NULL, *output = NULL, *pass = NULL, *pass_stats = NULL, *pass_trace = NULL;
    bool interpret = false, object = false, assembly = false, verify = false, classify = false, dump_passes = false;
    int level = 0;
    for (int a = 1; a < argc; ++a) {
        if (strcmp(argv[a], "--help") == 0) {
            fputs("Usage: cinderir [--verify|--interpret|--classify|-c|-S] [-O0|-O1|-O2] [--pass=NAME] [--pass-stats PATH] [--pass-trace DIR] [-o PATH] input.cir\nPasses: constant-fold, cfg-simplify, mem2reg, sparse-constants, dead-code, value-numbering, copy-cleanup, local-memory, loop-motion, strength-reduction.\n--dump-passes prints records only. Without a mode, write canonical IR. Native objects target Linux x86-64.\n", stdout); return 0;
        }
        if (strcmp(argv[a], "--verify") == 0) verify = true;
        else if (strcmp(argv[a], "--interpret") == 0) interpret = true;
        else if (strcmp(argv[a], "--classify") == 0) classify = true;
        else if (strcmp(argv[a], "--dump-passes") == 0) dump_passes = true;
        else if (strcmp(argv[a], "--pass-stats") == 0 && a + 1 < argc) pass_stats = argv[++a];
        else if (strcmp(argv[a], "--pass-trace") == 0 && a + 1 < argc) pass_trace = argv[++a];
        else if (strcmp(argv[a], "-c") == 0) object = true;
        else if (strcmp(argv[a], "-S") == 0) assembly = true;
        else if (strcmp(argv[a], "-O0") == 0) level = 0;
        else if (strcmp(argv[a], "-O1") == 0) level = 1;
        else if (strcmp(argv[a], "-O2") == 0) level = 2;
        else if (strncmp(argv[a], "--pass=", 7U) == 0 && pass == NULL) pass = argv[a] + 7U;
        else if (strcmp(argv[a], "-o") == 0 && a + 1 < argc) output = argv[++a];
        else if (argv[a][0] == '-' || input != NULL) { fprintf(stderr, "cinderir: invalid argument '%s'\n", argv[a]); return 2; }
        else input = argv[a];
    }
    if (input == NULL || (unsigned)interpret + (unsigned)object + (unsigned)assembly + (unsigned)verify + (unsigned)classify + (unsigned)dump_passes > 1U || (object && output == NULL) || (pass != NULL && level != 0)) {
        fputs("cinderir: one input and one mode are required; -c requires -o\n", stderr); return 2;
    }
    CinderSourceManager sources; cinder_sources_init(&sources);
    CinderDiagnostics diags; cinder_diags_init(&diags);
    CinderTypeContext types; cinder_types_init(&types);
    CinderIRModule module; cinder_ir_init(&module, &types);
    int result = 1;
    CinderFileId file = cinder_source_load(&sources, input, stderr);
    CinderSourceFile *source = cinder_source_get(&sources, file);
    if (source == NULL || cinder_parse_ir(&module, source->bytes, source->size, &diags) != 0) goto done;
    CinderOptStats stats;
    if ((pass == NULL ? cinder_optimize_trace(&module, level, pass_trace, &stats, &diags) : cinder_optimize_only_trace(&module, pass, pass_trace, &stats, &diags)) != 0 || cinder_verify_ir(&module, &diags) != 0) goto done;
    if (pass_stats != NULL && cinder_save_pass_stats(&stats, pass_stats, &diags) != 0) goto done;
    if (dump_passes) { result = cinder_write_pass_stats(&stats, stdout) != 0 || fflush(stdout) != 0; goto done; }
    if (verify) { puts("IR verified"); result = 0; goto done; }
    if (interpret || classify) {
        CinderInterpResult observed = cinder_interpret(&module, "main", NULL, 0U, 1000000U, &diags);
        if (classify) {
            uint64_t floating_bits; memcpy(&floating_bits, &observed.floating, sizeof(floating_bits));
            printf("{\"valid\":%s,\"classification\":%u,\"class\":\"%s\",\"integer\":%" PRId64 ",\"floating\":%s,\"floating_bits\":\"%016" PRIx64 "\"}\n", observed.valid ? "true" : "false", (unsigned)observed.classification, cinder_interp_class_name(observed.classification), observed.value, observed.floating_result ? "true" : "false", floating_bits);
            result = 0; goto done;
        }
        if (!observed.valid) goto done;
        printf("interpret main => %" PRId64 "\n", observed.value); result = 0; goto done;
    }
    CinderMachineObject machine; cinder_machine_init(&machine);
    if (object || assembly) {
        if (cinder_lower_globals(&module, &machine, &diags) != 0) goto done_machine;
        for (size_t f = 0U; f < module.functions.len; ++f)
            if (cinder_mir_boundary(&module.functions.data[f], &diags) != 0) goto done_machine;
        if (cinder_verify_ir(&module, &diags) != 0) goto done_machine;
        for (size_t f = 0U; f < module.functions.len; ++f) {
            CinderAllocation allocation; cinder_alloc_init(&allocation, &module.functions.data[f]);
            int failed = cinder_allocate(&allocation, &diags) != 0 || cinder_verify_allocation(&allocation, &diags) != 0 || cinder_lower_x86(&module.functions.data[f], &allocation, &machine, false, NULL, &diags) != 0;
            cinder_alloc_destroy(&allocation); if (failed) goto done_machine;
        }
    }
    if (object) result = cinder_write_elf64(&machine, output, &diags);
    else if (output == NULL || strcmp(output, "-") == 0) {
        result = assembly ? cinder_write_assembly(&machine, stdout, &diags) : cinder_write_ir(&module, stdout, &diags);
        if (fflush(stdout) != 0) result = 1;
    } else {
        CinderOutput staged;
        if (cinder_output_begin(&staged, output, &diags) != 0) goto done_machine;
        int failed = assembly ? cinder_write_assembly(&machine, staged.stream, &diags) : cinder_write_ir(&module, staged.stream, &diags);
        if (failed) cinder_output_abort(&staged);
        else result = cinder_output_commit(&staged, &diags);
    }
done_machine:
    cinder_machine_destroy(&machine);
done:
    if (diags.items.len != 0U) cinder_diag_print(&diags, &sources, stderr);
    cinder_ir_destroy(&module); cinder_types_destroy(&types); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources);
    return result;
}
