#define _POSIX_C_SOURCE 200809L
#include "cinder.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int cinder_output_begin(CinderOutput *output, const char *path, CinderDiagnostics *diags) {
    memset(output, 0, sizeof(*output));
    output->destination = path;
    size_t length = strlen(path);
    const char suffix[] = ".cinder-XXXXXX";
    if (length > SIZE_MAX - sizeof(suffix)) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "output path is too long");
        return 1;
    }
    output->temporary = cinder_alloc(length + sizeof(suffix));
    memcpy(output->temporary, path, length);
    memcpy(output->temporary + length, suffix, sizeof(suffix));
    int descriptor = mkstemp(output->temporary);
    if (descriptor < 0) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot create temporary output for '%s': %s", path, strerror(errno));
        free(output->temporary);
        output->temporary = NULL;
        return 1;
    }
    output->stream = fdopen(descriptor, "wb");
    if (output->stream == NULL) {
        int saved = errno;
        close(descriptor);
        cinder_output_abort(output);
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot open output stream for '%s': %s", path, strerror(saved));
        return 1;
    }
    return 0;
}

int cinder_output_seal(CinderOutput *output, CinderDiagnostics *diags) {
    if (output->stream == NULL) return 0;
    int failed = ferror(output->stream);
    if (fclose(output->stream) != 0) failed = 1;
    output->stream = NULL;
    if (failed) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "failed writing complete output '%s'", output->destination);
        return 1;
    }
    return 0;
}

int cinder_output_commit(CinderOutput *output, CinderDiagnostics *diags) {
    if (cinder_output_seal(output, diags) != 0) {
        cinder_output_abort(output);
        return 1;
    }
    if (output->temporary == NULL || diags->errors != 0U) {
        cinder_output_abort(output);
        return 1;
    }
    if (rename(output->temporary, output->destination) != 0) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot publish output '%s': %s", output->destination, strerror(errno));
        cinder_output_abort(output);
        return 1;
    }
    free(output->temporary);
    output->temporary = NULL;
    return 0;
}

void cinder_output_abort(CinderOutput *output) {
    if (output->stream != NULL) fclose(output->stream);
    output->stream = NULL;
    if (output->temporary != NULL) {
        unlink(output->temporary);
        free(output->temporary);
    }
    output->temporary = NULL;
}
