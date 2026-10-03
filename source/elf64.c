#include "cinder.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SHF_WRITE 0x1ULL
#define SHF_ALLOC 0x2ULL
#define SHF_EXECINSTR 0x4ULL
#define SHT_PROGBITS 1U
#define SHT_SYMTAB 2U
#define SHT_STRTAB 3U
#define SHT_RELA 4U
#define SHT_NOBITS 8U
#define STB_GLOBAL 1U
#define STT_NOTYPE 0U
#define STT_FUNC 2U
#define R_X86_64_PLT32 4U
#define DATA_SECTION 1U
#define RODATA_SECTION 2U
#define BSS_SECTION 3U

#if defined(__GNUC__) || defined(__clang__)
#define CINDER_PACKED __attribute__((packed))
#else
#define CINDER_PACKED
#endif

typedef struct CINDER_PACKED { unsigned char e_ident[16]; uint16_t e_type; uint16_t e_machine; uint32_t e_version; uint64_t e_entry; uint64_t e_phoff; uint64_t e_shoff; uint32_t e_flags; uint16_t e_ehsize; uint16_t e_phentsize; uint16_t e_phnum; uint16_t e_shentsize; uint16_t e_shnum; uint16_t e_shstrndx; } ElfHeader;
typedef struct CINDER_PACKED { uint32_t sh_name; uint32_t sh_type; uint64_t sh_flags; uint64_t sh_addr; uint64_t sh_offset; uint64_t sh_size; uint32_t sh_link; uint32_t sh_info; uint64_t sh_addralign; uint64_t sh_entsize; } SectionHeader;
typedef struct CINDER_PACKED { uint32_t st_name; unsigned char st_info; unsigned char st_other; uint16_t st_shndx; uint64_t st_value; uint64_t st_size; } Symbol;
typedef struct CINDER_PACKED { uint64_t r_offset; uint64_t r_info; int64_t r_addend; } Rela;

typedef struct {
    const char *name;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    const unsigned char *data;
    size_t size;
    uint32_t link;
    uint32_t info;
    uint64_t entsize;
    uint32_t name_offset;
    uint64_t file_offset;
} Section;

static size_t align_up(size_t value, size_t alignment) {
    if (alignment <= 1U) return value;
    size_t mask = alignment - 1U;
    return value > SIZE_MAX - mask ? SIZE_MAX : (value + mask) & ~mask;
}

static uint32_t string_add(CinderBytes *strings, const char *text) {
    uint32_t offset = (uint32_t)strings->len;
    cinder_bytes_append(strings, (const unsigned char *)text, strlen(text) + 1U);
    return offset;
}

static void write_at(CinderBytes *file, size_t offset, const void *data, size_t size) {
    if (offset > SIZE_MAX - size) return;
    size_t end = offset + size;
    if (end > file->len) {
        cinder_bytes_reserve(file, end - file->len);
        memset(file->data + file->len, 0, end - file->len);
        file->len = end;
    }
    if (data != NULL) memcpy(file->data + offset, data, size);
    else memset(file->data + offset, 0, size);
}

static uint16_t section_for_data(unsigned kind) {
    if (kind == DATA_SECTION) return 2U;
    if (kind == RODATA_SECTION) return 3U;
    if (kind == BSS_SECTION) return 4U;
    return 0U;
}

int cinder_write_elf64(const CinderMachineObject *object, const char *path, CinderDiagnostics *diags) {
    CinderBytes strtab = {NULL, 0U, 0U};
    CinderBytes shstrtab = {NULL, 0U, 0U};
    cinder_bytes_put8(&strtab, 0U);
    cinder_bytes_put8(&shstrtab, 0U);
    const char *section_names[] = {"", ".text", ".data", ".rodata", ".bss", ".rela.text", ".note.GNU-stack", ".symtab", ".strtab", ".shstrtab"};
    uint32_t section_name_offsets[CINDER_ARRAY_LEN(section_names)];
    for (size_t i = 0U; i < CINDER_ARRAY_LEN(section_names); ++i) section_name_offsets[i] = string_add(&shstrtab, section_names[i]);

    CinderBytes symbytes = {NULL, 0U, 0U};
    Symbol null_symbol;
    memset(&null_symbol, 0, sizeof(null_symbol));
    cinder_bytes_append(&symbytes, (const unsigned char *)&null_symbol, sizeof(null_symbol));
    CINDER_VEC_TYPE(uint32_t) function_indices = {NULL, 0U, 0U};
    CINDER_VEC_TYPE(uint32_t) data_indices = {NULL, 0U, 0U};

    for (size_t i = 0U; i < object->defined_symbols.len; ++i) {
        Symbol symbol;
        memset(&symbol, 0, sizeof(symbol));
        symbol.st_name = string_add(&strtab, object->defined_symbols.data[i]);
        symbol.st_info = (unsigned char)((STB_GLOBAL << 4U) | STT_FUNC);
        symbol.st_shndx = 1U;
        symbol.st_value = object->symbol_offsets.data[i];
        uint32_t index = (uint32_t)(symbytes.len / sizeof(Symbol));
        cinder_bytes_append(&symbytes, (const unsigned char *)&symbol, sizeof(symbol));
        cinder_vec_push((CinderVec *)&function_indices, &index);
    }
    for (size_t i = 0U; i < object->data_symbols.len; ++i) {
        const CinderDataSymbol *definition = &object->data_symbols.data[i];
        Symbol symbol;
        memset(&symbol, 0, sizeof(symbol));
        symbol.st_name = string_add(&strtab, definition->name);
        symbol.st_info = (unsigned char)((definition->global ? STB_GLOBAL : 0U) << 4U | STT_NOTYPE);
        symbol.st_shndx = section_for_data(definition->section_kind);
        symbol.st_value = definition->offset;
        symbol.st_size = definition->size;
        uint32_t index = (uint32_t)(symbytes.len / sizeof(Symbol));
        cinder_bytes_append(&symbytes, (const unsigned char *)&symbol, sizeof(symbol));
        cinder_vec_push((CinderVec *)&data_indices, &index);
    }

    CINDER_VEC_TYPE(char *) undefined = {NULL, 0U, 0U};
    for (size_t i = 0U; i < object->fixups.len; ++i) {
        bool already = false;
        bool defined = false;
        for (size_t d = 0U; d < object->defined_symbols.len; ++d) if (strcmp(object->defined_symbols.data[d], object->fixups.data[i].symbol) == 0) defined = true;
        for (size_t d = 0U; d < object->data_symbols.len; ++d) if (strcmp(object->data_symbols.data[d].name, object->fixups.data[i].symbol) == 0) defined = true;
        for (size_t u = 0U; u < undefined.len; ++u) if (strcmp(undefined.data[u], object->fixups.data[i].symbol) == 0) already = true;
        if (!defined && !already) { char *name = cinder_strndup(object->fixups.data[i].symbol, strlen(object->fixups.data[i].symbol)); cinder_vec_push((CinderVec *)&undefined, &name); }
    }
    CINDER_VEC_TYPE(uint32_t) undefined_indices = {NULL, 0U, 0U};
    for (size_t i = 0U; i < undefined.len; ++i) {
        Symbol symbol;
        memset(&symbol, 0, sizeof(symbol));
        symbol.st_name = string_add(&strtab, undefined.data[i]);
        symbol.st_info = (unsigned char)(STB_GLOBAL << 4U | STT_NOTYPE);
        uint32_t index = (uint32_t)(symbytes.len / sizeof(Symbol));
        cinder_bytes_append(&symbytes, (const unsigned char *)&symbol, sizeof(symbol));
        cinder_vec_push((CinderVec *)&undefined_indices, &index);
    }

    CinderBytes relabytes = {NULL, 0U, 0U};
    for (size_t i = 0U; i < object->fixups.len; ++i) {
        size_t symbol_index = SIZE_MAX;
        for (size_t d = 0U; d < object->defined_symbols.len; ++d) if (strcmp(object->defined_symbols.data[d], object->fixups.data[i].symbol) == 0) symbol_index = function_indices.data[d];
        for (size_t d = 0U; d < object->data_symbols.len; ++d) if (strcmp(object->data_symbols.data[d].name, object->fixups.data[i].symbol) == 0) symbol_index = data_indices.data[d];
        for (size_t u = 0U; u < undefined.len; ++u) if (strcmp(undefined.data[u], object->fixups.data[i].symbol) == 0) symbol_index = undefined_indices.data[u];
        if (symbol_index == SIZE_MAX) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "internal relocation symbol lookup failure for '%s'", object->fixups.data[i].symbol); continue; }
        Rela rela;
        rela.r_offset = object->fixups.data[i].offset;
        rela.r_info = ((uint64_t)symbol_index << 32U) | (uint32_t)object->fixups.data[i].type;
        rela.r_addend = object->fixups.data[i].addend;
        cinder_bytes_append(&relabytes, (const unsigned char *)&rela, sizeof(rela));
    }

    Section sections[10];
    memset(sections, 0, sizeof(sections));
    sections[0] = (Section){"", 0U, 0U, 0U, NULL, 0U, 0U, 0U, 0U, section_name_offsets[0], 0U};
    sections[1] = (Section){".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16U, object->text.data, object->text.len, 0U, 0U, 0U, section_name_offsets[1], 0U};
    sections[2] = (Section){".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 8U, object->data.data, object->data.len, 0U, 0U, 0U, section_name_offsets[2], 0U};
    sections[3] = (Section){".rodata", SHT_PROGBITS, SHF_ALLOC, 1U, object->rodata.data, object->rodata.len, 0U, 0U, 0U, section_name_offsets[3], 0U};
    sections[4] = (Section){".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 8U, NULL, object->bss_size, 0U, 0U, 0U, section_name_offsets[4], 0U};
    sections[5] = (Section){".rela.text", SHT_RELA, 0U, 8U, relabytes.data, relabytes.len, 7U, 1U, sizeof(Rela), section_name_offsets[5], 0U};
    sections[6] = (Section){".note.GNU-stack", SHT_PROGBITS, 0U, 1U, NULL, 0U, 0U, 0U, 0U, section_name_offsets[6], 0U};
    sections[7] = (Section){".symtab", SHT_SYMTAB, 0U, 8U, symbytes.data, symbytes.len, 8U, 1U, sizeof(Symbol), section_name_offsets[7], 0U};
    sections[8] = (Section){".strtab", SHT_STRTAB, 0U, 1U, strtab.data, strtab.len, 0U, 0U, 0U, section_name_offsets[8], 0U};
    sections[9] = (Section){".shstrtab", SHT_STRTAB, 0U, 1U, shstrtab.data, shstrtab.len, 0U, 0U, 0U, section_name_offsets[9], 0U};

    CinderBytes file = {NULL, 0U, 0U};
    size_t cursor = sizeof(ElfHeader);
    for (size_t i = 1U; i < CINDER_ARRAY_LEN(sections); ++i) {
        cursor = align_up(cursor, (size_t)sections[i].align);
        sections[i].file_offset = cursor;
        if (sections[i].size > SIZE_MAX - cursor) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "ELF section size overflow"); goto failure; }
        if (sections[i].type != SHT_NOBITS && sections[i].size != 0U) write_at(&file, cursor, sections[i].data, sections[i].size);
        if (sections[i].type != SHT_NOBITS) cursor += sections[i].size;
    }
    cursor = align_up(cursor, 8U);
    size_t section_table_offset = cursor;
    size_t section_table_size = sizeof(SectionHeader) * CINDER_ARRAY_LEN(sections);
    write_at(&file, section_table_offset, NULL, section_table_size);
    SectionHeader headers[10];
    memset(headers, 0, sizeof(headers));
    for (size_t i = 1U; i < CINDER_ARRAY_LEN(sections); ++i) {
        headers[i].sh_name = sections[i].name_offset;
        headers[i].sh_type = sections[i].type;
        headers[i].sh_flags = sections[i].flags;
        headers[i].sh_offset = sections[i].file_offset;
        headers[i].sh_size = sections[i].size;
        headers[i].sh_link = sections[i].link;
        headers[i].sh_info = sections[i].info;
        headers[i].sh_addralign = sections[i].align;
        headers[i].sh_entsize = sections[i].entsize;
    }
    write_at(&file, section_table_offset, headers, section_table_size);
    ElfHeader header;
    memset(&header, 0, sizeof(header));
    header.e_ident[0] = 0x7FU; header.e_ident[1] = 'E'; header.e_ident[2] = 'L'; header.e_ident[3] = 'F'; header.e_ident[4] = 2U; header.e_ident[5] = 1U; header.e_ident[6] = 1U;
    header.e_type = 1U; header.e_machine = 62U; header.e_version = 1U; header.e_shoff = section_table_offset; header.e_ehsize = sizeof(ElfHeader); header.e_shentsize = sizeof(SectionHeader); header.e_shnum = CINDER_ARRAY_LEN(sections); header.e_shstrndx = 9U;
    write_at(&file, 0U, &header, sizeof(header));

    CinderOutput output;
    if (cinder_output_begin(&output, path, diags) != 0) goto failure;
    size_t written = fwrite(file.data, 1U, file.len, output.stream);
    if (written != file.len) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "short write while publishing ELF object '%s'", path);
        cinder_output_abort(&output);
        goto failure;
    }
    if (cinder_output_commit(&output, diags) != 0) goto failure;
    free(file.data); free(symbytes.data); free(strtab.data); free(shstrtab.data); free(relabytes.data); free(function_indices.data); free(data_indices.data); free(undefined_indices.data); for (size_t i = 0U; i < undefined.len; ++i) free(undefined.data[i]); free(undefined.data);
    return diags->errors == 0U ? 0 : 1;

failure:
    free(file.data); free(symbytes.data); free(strtab.data); free(shstrtab.data); free(relabytes.data); free(function_indices.data); free(data_indices.data); free(undefined_indices.data); for (size_t i = 0U; i < undefined.len; ++i) free(undefined.data[i]); free(undefined.data);
    return 1;
}
