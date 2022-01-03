#include "cinder.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int cinder_write_explorer(const char *directory, const CinderTokenStream *tokens, const CinderAst *ast, const CinderIRModule *module, const CinderMachineObject *machine, CinderDiagnostics *diags) {
    if (mkdir(directory, 0755U) != 0 && errno != EEXIST) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot create explorer directory '%s'", directory); return 1; }
    char path[4096];
    int written = snprintf(path, sizeof(path), "%s/index.html", directory);
    if (written <= 0 || (size_t)written >= sizeof(path)) return 1;
    FILE *file = fopen(path, "w");
    if (file == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot write explorer report: %s", strerror(errno)); return 1; }
    fprintf(file, "<!doctype html><meta charset=utf-8><title>Cinder explorer</title><style>body{font-family:monospace;max-width:1000px;margin:2rem auto}pre{background:#f4f4f4;padding:1rem;overflow:auto}</style><h1>Cinder compilation explorer</h1><p>tokens=%zu declarations=%zu functions=%zu text-bytes=%zu</p><h2>Pipeline</h2><pre>source -> preprocessing -> tokens -> AST -> typed IR -> optimization -> MIR -> ELF</pre>", tokens->tokens.len, ast->declarations.len, module->functions.len, machine == NULL ? 0U : machine->text.len);
    fclose(file);
    return 0;
}
