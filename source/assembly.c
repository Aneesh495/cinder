#include "cinder.h"

#include <inttypes.h>

static void bytes(FILE *out, const unsigned char *data, size_t begin, size_t end) {
    while (begin < end) {
        size_t length = end - begin < 16U ? end - begin : 16U;
        fputs("  .byte ", out);
        for (size_t i = 0U; i < length; ++i)
            fprintf(out, "0x%02x%s", data[begin + i], i + 1U == length ? "" : ", ");
        fputc('\n', out); begin += length;
    }
}

static size_t next_text_label(const CinderMachineObject *object, size_t offset) {
    size_t next = object->text.len;
    for (size_t i = 0U; i < object->symbol_offsets.len; ++i)
        if (object->symbol_offsets.data[i] > offset && object->symbol_offsets.data[i] < next) next = object->symbol_offsets.data[i];
    return next;
}

static void text_labels(const CinderMachineObject *object, FILE *out, size_t offset) {
    for (size_t i = 0U; i < object->symbol_offsets.len; ++i) {
        if (object->symbol_offsets.data[i] != offset) continue;
        const char *name = object->defined_symbols.data[i];
        fprintf(out, "%s %s\n.type %s,@function\n%s:\n", object->symbol_globals.data[i] ? ".globl" : ".local", name, name, name);
    }
}

static int text_section(const CinderMachineObject *object, FILE *out, CinderDiagnostics *diags) {
    fputs(".text\n.p2align 4\n", out);
    size_t offset = 0U, relocation = 0U;
    while (offset < object->text.len) {
        text_labels(object, out, offset);
        if (relocation < object->fixups.len && object->fixups.data[relocation].offset == offset) {
            const CinderFixup *fix = &object->fixups.data[relocation++];
            if (object->text.len - offset < 4U || (fix->type != 2 && fix->type != 4)) {
                cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "assembly writer found an unsupported text relocation"); return 1;
            }
            fprintf(out, "  .long %s%s - . %c %" PRIu64 "\n", fix->symbol, fix->type == 4 ? "@PLT" : "", fix->addend < 0 ? '-' : '+', fix->addend < 0 ? UINT64_C(0) - (uint64_t)fix->addend : (uint64_t)fix->addend);
            offset += 4U;
        } else {
            size_t end = next_text_label(object, offset);
            if (relocation < object->fixups.len) {
                if (object->fixups.data[relocation].offset < offset) {
                    cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "assembly writer found overlapping relocations"); return 1;
                }
                if (object->fixups.data[relocation].offset < end) end = object->fixups.data[relocation].offset;
            }
            bytes(out, object->text.data, offset, end); offset = end;
        }
    }
    if (relocation != object->fixups.len) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "assembly relocation is outside text"); return 1;
    }
    for (size_t i = 0U; i < object->defined_symbols.len; ++i)
        fprintf(out, ".size %s,%zu\n", object->defined_symbols.data[i], object->symbol_sizes.data[i]);
    return 0;
}

static void data_labels(const CinderMachineObject *object, FILE *out, unsigned section, size_t offset) {
    for (size_t i = 0U; i < object->data_symbols.len; ++i) {
        const CinderDataSymbol *symbol = &object->data_symbols.data[i];
        if (symbol->section_kind != section || symbol->offset != offset) continue;
        fprintf(out, "%s %s\n.type %s,@object\n.size %s,%zu\n%s:\n", symbol->global ? ".globl" : ".local", symbol->name, symbol->name, symbol->name, symbol->size, symbol->name);
    }
}

static void data_section(const CinderMachineObject *object, FILE *out, unsigned section, const char *name, const unsigned char *data, size_t size) {
    fprintf(out, ".section %s\n.p2align 3\n", name);
    size_t offset = 0U;
    while (offset < size) {
        data_labels(object, out, section, offset);
        size_t next = size;
        for (size_t i = 0U; i < object->data_symbols.len; ++i) {
            const CinderDataSymbol *symbol = &object->data_symbols.data[i];
            if (symbol->section_kind == section && symbol->offset > offset && symbol->offset < next) next = symbol->offset;
        }
        if (data == NULL) fprintf(out, "  .zero %zu\n", next - offset);
        else bytes(out, data, offset, next);
        offset = next;
    }
    data_labels(object, out, section, size);
}

int cinder_write_assembly(const CinderMachineObject *object, FILE *out, CinderDiagnostics *diags) {
    if (text_section(object, out, diags) != 0) return 1;
    data_section(object, out, 1U, ".data", object->data.data, object->data.len);
    data_section(object, out, 2U, ".rodata", object->rodata.data, object->rodata.len);
    data_section(object, out, 3U, ".bss", NULL, object->bss_size);
    fputs(".section .note.GNU-stack,\"\",@progbits\n", out);
    if (ferror(out)) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "assembly output write failed"); return 1; }
    return 0;
}
