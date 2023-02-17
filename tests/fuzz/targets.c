#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "cinder.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef CINDER_FUZZ_STAGE
#error CINDER_FUZZ_STAGE must select a target
#endif
int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static struct {
    uint64_t attempts, source_buffers, preprocessed, lexed, parsed, typed;
    uint64_t verified_ir, roundtrips, allocated_functions, encoded_functions, objects, nonempty_objects;
    uint64_t diagnostics, harness_io_errors, harness_extent_rejections;
} counters;
static char output_path[4096];
static char *owned_directory;

static void record(void) {
    const char *path = getenv("CINDER_FUZZ_STATS");
    if (path != NULL) {
        FILE *out = fopen(path, "wb");
        if (out != NULL) {
            fprintf(out, "{\"schema\":1,\"stage\":%d,\"attempts\":%" PRIu64 ",\"source_buffers\":%" PRIu64 ",\"preprocessed\":%" PRIu64 ",\"lexed\":%" PRIu64 ",\"parsed\":%" PRIu64 ",\"typed\":%" PRIu64 ",\"verified_ir\":%" PRIu64 ",\"roundtrips\":%" PRIu64 ",\"allocated_functions\":%" PRIu64 ",\"encoded_functions\":%" PRIu64 ",\"objects\":%" PRIu64 ",\"nonempty_objects\":%" PRIu64 ",\"diagnostics\":%" PRIu64 ",\"harness_io_errors\":%" PRIu64 ",\"harness_extent_rejections\":%" PRIu64 "}\n", CINDER_FUZZ_STAGE, counters.attempts, counters.source_buffers, counters.preprocessed, counters.lexed, counters.parsed, counters.typed, counters.verified_ir, counters.roundtrips, counters.allocated_functions, counters.encoded_functions, counters.objects, counters.nonempty_objects, counters.diagnostics, counters.harness_io_errors, counters.harness_extent_rejections);
            (void)fclose(out);
        }
    }
    if (owned_directory != NULL) { (void)unlink(output_path); (void)rmdir(owned_directory); free(owned_directory); }
}
int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    if (CINDER_FUZZ_STAGE == 3) {
        const char *work = getenv("CINDER_FUZZ_WORK");
        if (work == NULL) {
            const char *temporary = getenv("TMPDIR"); if (temporary == NULL) temporary = "/tmp";
            size_t length = strlen(temporary) + 32U; owned_directory = cinder_alloc(length);
            (void)snprintf(owned_directory, length, "%s/cinder-fuzz-XXXXXX", temporary);
            if (mkdtemp(owned_directory) == NULL) { free(owned_directory); owned_directory = NULL; return -1; }
            work = owned_directory;
        }
        int length = snprintf(output_path, sizeof(output_path), "%s/object.o", work);
        if (length < 0 || (size_t)length >= sizeof(output_path)) return -1;
    }
    if (atexit(record) != 0) return -1;
    return 0;
}

static void frontend(const uint8_t *data, size_t size) {
    CinderSourceManager sources; CinderDiagnostics diags; CinderTokenStream tokens; CinderTypeContext types; CinderAst ast; CinderSema sema;
    cinder_sources_init(&sources); cinder_diags_init(&diags); cinder_tokens_init(&tokens); cinder_types_init(&types); sources.buffered_only = true;
    bool ast_ready = false, sema_ready = false;
    static const char fixture[] = "#pragma once\n#define STEP(x) ((x)+1)\ntypedef struct { int count; double value; } Fixture;\n";
    (void)cinder_source_buffer(&sources, "/cinder-fuzz/fixture.h", fixture, sizeof(fixture) - 1U, &diags);
    if (cinder_source_buffer(&sources, "/cinder-fuzz/input.c", (const char *)data, size, &diags) == CINDER_NO_FILE) goto done;
    ++counters.source_buffers;
    const char *directories[] = {"/cinder-fuzz"};
    if (cinder_preprocess(&sources, "/cinder-fuzz/input.c", directories, 1U, NULL, 0U, &diags) != 0) goto done;
    ++counters.preprocessed;
    if (cinder_lex(&sources, &tokens, &diags) != 0) goto done;
    ++counters.lexed; if (CINDER_FUZZ_STAGE == 0) goto done;
    cinder_ast_init(&ast, &types, &tokens, &diags); ast_ready = true;
    if (cinder_parse(&ast) != 0) goto done;
    ++counters.parsed; cinder_sema_init(&sema, &ast, &types, &diags); sema_ready = true;
    if (cinder_sema_run(&sema) == 0) ++counters.typed;
done:
    if (sema_ready) cinder_sema_destroy(&sema);
    if (ast_ready) cinder_ast_destroy(&ast);
    if (diags.errors != 0U) ++counters.diagnostics;
    cinder_types_destroy(&types); cinder_tokens_destroy(&tokens); cinder_diags_destroy(&diags); cinder_sources_destroy(&sources);
}
static void roundtrip(const CinderIRModule *module, CinderDiagnostics *diags) {
    FILE *out = tmpfile(); if (out == NULL) { ++counters.harness_io_errors; return; }
    CinderBytes bytes = {0};
    if (cinder_write_ir(module, out, diags) != 0 || fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) { ++counters.harness_io_errors; fclose(out); return; }
    unsigned char buffer[4096]; size_t count;
    while ((count = fread(buffer, 1U, sizeof(buffer), out)) != 0U) {
        if (bytes.len > 64U * 1024U * 1024U - count) { ++counters.harness_extent_rejections; free(bytes.data); fclose(out); return; }
        cinder_bytes_append(&bytes, buffer, count);
    }
    if (ferror(out)) { ++counters.harness_io_errors; free(bytes.data); fclose(out); return; }
    fclose(out); CinderTypeContext types; CinderIRModule parsed; cinder_types_init(&types); cinder_ir_init(&parsed, &types);
    if (cinder_parse_ir(&parsed, (const char *)bytes.data, bytes.len, diags) != 0) abort();
    ++counters.roundtrips;
    cinder_ir_destroy(&parsed); cinder_types_destroy(&types); free(bytes.data);
}
static uint64_t little(const unsigned char *p, unsigned width) {
    uint64_t value = 0U; for (unsigned i = 0U; i < width; ++i) value |= (uint64_t)p[i] << (i * 8U); return value;
}
static void invariant(bool valid) { if (!valid) abort(); }
static void object_layout(const char *path, const CinderMachineObject *machine) {
    FILE *input = fopen(path, "rb"); if (input == NULL) { ++counters.harness_io_errors; return; }
    CinderBytes file = {0}; unsigned char buffer[4096]; size_t count;
    while ((count = fread(buffer, 1U, sizeof(buffer), input)) != 0U) cinder_bytes_append(&file, buffer, count);
    if (ferror(input)) { fclose(input); free(file.data); ++counters.harness_io_errors; return; }
    fclose(input); invariant(file.len >= 64U); const unsigned char *data = file.data;
    invariant(memcmp(data, "\x7f" "ELF\x02\x01\x01", 7U) == 0 && little(data + 16U, 2U) == 1U && little(data + 18U, 2U) == 62U);
    uint64_t at = little(data + 40U, 8U), width = little(data + 58U, 2U), sections = little(data + 60U, 2U), names = little(data + 62U, 2U);
    invariant(width == 64U && sections >= 12U && names < sections && at <= file.len && sections <= (file.len - at) / width);
    const unsigned char *strings_section = data + at + names * width;
    uint64_t strings_at = little(strings_section + 24U, 8U), strings_size = little(strings_section + 32U, 8U);
    invariant(strings_at <= file.len && strings_size <= file.len - strings_at); bool found_text = false;
    for (uint64_t i = 0U; i < sections; ++i) {
        const unsigned char *section = data + at + i * width;
        uint64_t name = little(section, 4U), kind = little(section + 4U, 4U), offset = little(section + 24U, 8U), size = little(section + 32U, 8U), alignment = little(section + 48U, 8U);
        invariant(name < strings_size && memchr(data + strings_at + name, 0, (size_t)(strings_size - name)) != NULL);
        invariant(kind == 8U || (offset <= file.len && size <= file.len - offset));
        invariant(alignment == 0U || ((alignment & (alignment - 1U)) == 0U && offset % alignment == 0U));
        if (strcmp((const char *)data + strings_at + name, ".text") == 0) {
            invariant(size == machine->text.len && (size == 0U || memcmp(data + offset, machine->text.data, (size_t)size) == 0)); found_text = true;
        }
        if (kind == 4U) {
            uint64_t symbols = little(section + 40U, 4U), target = little(section + 44U, 4U);
            invariant(little(section + 56U, 8U) == 24U && size % 24U == 0U && symbols < sections && target < sections);
            const unsigned char *table = data + at + symbols * width, *to = data + at + target * width;
            invariant(little(table + 4U, 4U) == 2U && little(table + 56U, 8U) == 24U);
            uint64_t symbols_size = little(table + 32U, 8U), extent = little(to + 32U, 8U);
            for (uint64_t r = 0U; r < size; r += 24U) {
                uint64_t position = little(data + offset + r, 8U), info = little(data + offset + r + 8U, 8U), type = info & UINT32_MAX, bytes = type == 1U ? 8U : 4U;
                invariant((type == 1U || type == 2U || type == 4U || type == 10U) && (info >> 32U) < symbols_size / 24U && position <= extent && bytes <= extent - position);
            }
        }
    }
    invariant(found_text); free(file.data);
}
static void encode(CinderIRModule *module, CinderDiagnostics *diags) {
    CinderMachineObject machine; cinder_machine_init(&machine);
    for (size_t f = 0U; f < module->functions.len; ++f) if (cinder_mir_boundary(&module->functions.data[f], diags) != 0) goto done;
    if (cinder_verify_ir(module, diags) != 0 || cinder_lower_globals(module, &machine, diags) != 0) goto done;
    for (size_t f = 0U; f < module->functions.len; ++f) {
        CinderAllocation allocation; cinder_alloc_init(&allocation, &module->functions.data[f]);
        int failed = cinder_allocate(&allocation, diags);
        if (failed == 0) failed = cinder_verify_allocation(&allocation, diags);
        if (failed == 0) { ++counters.allocated_functions; failed = cinder_lower_x86(&module->functions.data[f], &allocation, &machine, false, NULL, diags); }
        cinder_alloc_destroy(&allocation); if (failed != 0) goto done;
        ++counters.encoded_functions;
    }
    if (cinder_write_elf64(&machine, output_path, diags) == 0) {
        object_layout(output_path, &machine); ++counters.objects;
        if (machine.text.len != 0U) ++counters.nonempty_objects;
    }
done:
    cinder_machine_destroy(&machine);
}
static void ir(const uint8_t *data, size_t size) {
    CinderTypeContext types; CinderIRModule module; CinderDiagnostics diags;
    cinder_types_init(&types); cinder_ir_init(&module, &types); cinder_diags_init(&diags);
    if (cinder_parse_ir(&module, (const char *)data, size, &diags) == 0) {
        ++counters.verified_ir;
        if (CINDER_FUZZ_STAGE == 2) roundtrip(&module, &diags); else encode(&module, &diags);
    }
    if (diags.errors != 0U) ++counters.diagnostics;
    cinder_diags_destroy(&diags); cinder_ir_destroy(&module); cinder_types_destroy(&types);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ++counters.attempts;
    if (size > 65536U) { ++counters.harness_extent_rejections; return 0; }
    if (data == NULL) data = (const uint8_t *)"";
    if (CINDER_FUZZ_STAGE < 2) frontend(data, size); else ir(data, size);
    return 0;
}
