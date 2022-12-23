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

typedef struct { unsigned char e_ident[16]; uint16_t e_type; uint16_t e_machine; uint32_t e_version; uint64_t e_entry; uint64_t e_phoff; uint64_t e_shoff; uint32_t e_flags; uint16_t e_ehsize; uint16_t e_phentsize; uint16_t e_phnum; uint16_t e_shentsize; uint16_t e_shnum; uint16_t e_shstrndx; } ElfHeader;
typedef struct { uint32_t sh_name; uint32_t sh_type; uint64_t sh_flags; uint64_t sh_addr; uint64_t sh_offset; uint64_t sh_size; uint32_t sh_link; uint32_t sh_info; uint64_t sh_addralign; uint64_t sh_entsize; } SectionHeader;
typedef struct { uint32_t st_name; unsigned char st_info; unsigned char st_other; uint16_t st_shndx; uint64_t st_value; uint64_t st_size; } Symbol;
typedef struct { uint64_t r_offset; uint64_t r_info; int64_t r_addend; } Rela;

enum { ELF_HEADER_SIZE = 64U, ELF_SECTION_SIZE = 64U, ELF_SYMBOL_SIZE = 24U, ELF_RELA_SIZE = 24U };

static void append_symbol(CinderBytes *bytes, const Symbol *symbol) {
    cinder_bytes_put32(bytes, symbol->st_name);
    cinder_bytes_put8(bytes, symbol->st_info); cinder_bytes_put8(bytes, symbol->st_other);
    cinder_bytes_put16(bytes, symbol->st_shndx);
    cinder_bytes_put64(bytes, symbol->st_value); cinder_bytes_put64(bytes, symbol->st_size);
}

static void append_relocation(CinderBytes *bytes, const Rela *relocation) {
    cinder_bytes_put64(bytes, relocation->r_offset); cinder_bytes_put64(bytes, relocation->r_info);
    cinder_bytes_put64(bytes, (uint64_t)relocation->r_addend);
}

static void append_section(CinderBytes *bytes, const SectionHeader *section) {
    cinder_bytes_put32(bytes, section->sh_name); cinder_bytes_put32(bytes, section->sh_type);
    cinder_bytes_put64(bytes, section->sh_flags); cinder_bytes_put64(bytes, section->sh_addr);
    cinder_bytes_put64(bytes, section->sh_offset); cinder_bytes_put64(bytes, section->sh_size);
    cinder_bytes_put32(bytes, section->sh_link); cinder_bytes_put32(bytes, section->sh_info);
    cinder_bytes_put64(bytes, section->sh_addralign); cinder_bytes_put64(bytes, section->sh_entsize);
}

static void append_header(CinderBytes *bytes, const ElfHeader *header) {
    cinder_bytes_append(bytes, header->e_ident, 16U);
    cinder_bytes_put16(bytes, header->e_type); cinder_bytes_put16(bytes, header->e_machine);
    cinder_bytes_put32(bytes, header->e_version);
    cinder_bytes_put64(bytes, header->e_entry); cinder_bytes_put64(bytes, header->e_phoff); cinder_bytes_put64(bytes, header->e_shoff);
    cinder_bytes_put32(bytes, header->e_flags);
    cinder_bytes_put16(bytes, header->e_ehsize); cinder_bytes_put16(bytes, header->e_phentsize); cinder_bytes_put16(bytes, header->e_phnum);
    cinder_bytes_put16(bytes, header->e_shentsize); cinder_bytes_put16(bytes, header->e_shnum); cinder_bytes_put16(bytes, header->e_shstrndx);
}

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
    enum { DEBUG_LIMIT = 16U, SECTION_LIMIT = 12U + 2U * DEBUG_LIMIT, RELOCATION_LIMIT = 3U + DEBUG_LIMIT };
    if (!cinder_debug_sections_valid(object, diags)) return 1;
    for (size_t i = 0U; i < object->fixups.len; ++i) if (object->fixups.data[i].symbol == NULL) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "relocation has no symbol"); return 1; }
    size_t section_count = 12U + 2U * object->debug_sections.len;
    CinderBytes strtab = {NULL, 0U, 0U};
    CinderBytes shstrtab = {NULL, 0U, 0U};
    cinder_bytes_put8(&strtab, 0U);
    cinder_bytes_put8(&shstrtab, 0U);
    const char *section_names[] = {"", ".text", ".data", ".rodata", ".bss", ".rela.text", ".rela.data", ".rela.rodata", ".note.GNU-stack", ".symtab", ".strtab", ".shstrtab"};
    uint32_t section_name_offsets[CINDER_ARRAY_LEN(section_names)];
    for (size_t i = 0U; i < CINDER_ARRAY_LEN(section_names); ++i) section_name_offsets[i] = string_add(&shstrtab, section_names[i]);
    uint32_t debug_name_offsets[DEBUG_LIMIT], debug_rela_offsets[DEBUG_LIMIT];
    char *debug_rela_names[DEBUG_LIMIT] = {NULL};
    for (size_t i = 0U; i < object->debug_sections.len; ++i) {
        const char *name = object->debug_sections.data[i].name;
        debug_name_offsets[i] = string_add(&shstrtab, name);
        size_t length = strlen(name);
        debug_rela_names[i] = cinder_alloc(length + 6U);
        memcpy(debug_rela_names[i], ".rela", 5U); memcpy(debug_rela_names[i] + 5U, name, length + 1U);
        debug_rela_offsets[i] = string_add(&shstrtab, debug_rela_names[i]);
    }

    CinderBytes symbytes = {NULL, 0U, 0U};
    Symbol null_symbol;
    memset(&null_symbol, 0, sizeof(null_symbol));
    append_symbol(&symbytes, &null_symbol);
    CINDER_VEC_TYPE(uint32_t) function_indices = {NULL, 0U, 0U};
    CINDER_VEC_TYPE(uint32_t) data_indices = {NULL, 0U, 0U};

    uint32_t zero_index = 0U;
    for (size_t i = 0U; i < object->defined_symbols.len; ++i) cinder_vec_push((CinderVec *)&function_indices, &zero_index);
    for (size_t i = 0U; i < object->data_symbols.len; ++i) cinder_vec_push((CinderVec *)&data_indices, &zero_index);
    uint32_t debug_indices[DEBUG_LIMIT];
    for (size_t i = 0U; i < object->debug_sections.len; ++i) {
        Symbol symbol; memset(&symbol, 0, sizeof(symbol));
        symbol.st_info = 3U; symbol.st_shndx = (uint16_t)(12U + i * 2U);
        debug_indices[i] = (uint32_t)(symbytes.len / ELF_SYMBOL_SIZE);
        append_symbol(&symbytes, &symbol);
    }
    uint32_t first_global = 1U;
    for (unsigned binding = 0U; binding < 2U; ++binding) {
        if (binding == 1U) first_global = (uint32_t)(symbytes.len / ELF_SYMBOL_SIZE);
        for (size_t i = 0U; i < object->defined_symbols.len; ++i) {
            bool global = object->symbol_globals.data[i];
            if (global != (binding == 1U)) continue;
            Symbol symbol; memset(&symbol, 0, sizeof(symbol));
            symbol.st_name = string_add(&strtab, object->defined_symbols.data[i]);
            symbol.st_info = (unsigned char)((binding << 4U) | STT_FUNC);
            symbol.st_shndx = 1U; symbol.st_value = object->symbol_offsets.data[i];
            symbol.st_size = object->symbol_sizes.data[i];
            function_indices.data[i] = (uint32_t)(symbytes.len / ELF_SYMBOL_SIZE);
            append_symbol(&symbytes, &symbol);
        }
        for (size_t i = 0U; i < object->data_symbols.len; ++i) {
            const CinderDataSymbol *definition = &object->data_symbols.data[i];
            if (definition->global != (binding == 1U)) continue;
            Symbol symbol; memset(&symbol, 0, sizeof(symbol));
            symbol.st_name = string_add(&strtab, definition->name);
            symbol.st_info = (unsigned char)((binding << 4U) | 1U);
            symbol.st_shndx = section_for_data(definition->section_kind);
            symbol.st_value = definition->offset; symbol.st_size = definition->size;
            data_indices.data[i] = (uint32_t)(symbytes.len / ELF_SYMBOL_SIZE);
            append_symbol(&symbytes, &symbol);
        }
    }

    CINDER_VEC_TYPE(char *) undefined = {NULL, 0U, 0U};
    for (size_t i = 0U; i < object->fixups.len; ++i) {
        bool already = false;
        bool defined = false;
        for (size_t d = 0U; d < object->defined_symbols.len; ++d) if (strcmp(object->defined_symbols.data[d], object->fixups.data[i].symbol) == 0) defined = true;
        for (size_t d = 0U; d < object->data_symbols.len; ++d) if (strcmp(object->data_symbols.data[d].name, object->fixups.data[i].symbol) == 0) defined = true;
        for (size_t d = 0U; d < object->debug_sections.len; ++d) if (strcmp(object->debug_sections.data[d].name, object->fixups.data[i].symbol) == 0) defined = true;
        for (size_t u = 0U; u < undefined.len; ++u) if (strcmp(undefined.data[u], object->fixups.data[i].symbol) == 0) already = true;
        if (!defined && !already) { char *name = cinder_strndup(object->fixups.data[i].symbol, strlen(object->fixups.data[i].symbol)); cinder_vec_push((CinderVec *)&undefined, &name); }
    }
    CINDER_VEC_TYPE(uint32_t) undefined_indices = {NULL, 0U, 0U};
    for (size_t i = 0U; i < undefined.len; ++i) {
        Symbol symbol;
        memset(&symbol, 0, sizeof(symbol));
        symbol.st_name = string_add(&strtab, undefined.data[i]);
        symbol.st_info = (unsigned char)(STB_GLOBAL << 4U | STT_NOTYPE);
        uint32_t index = (uint32_t)(symbytes.len / ELF_SYMBOL_SIZE);
        append_symbol(&symbytes, &symbol);
        cinder_vec_push((CinderVec *)&undefined_indices, &index);
    }

    CinderBytes relabytes[RELOCATION_LIMIT] = {{NULL, 0U, 0U}};
    CinderBytes file = {NULL, 0U, 0U};
    for (size_t i = 0U; i < object->fixups.len; ++i) {
        size_t symbol_index = SIZE_MAX;
        for (size_t d = 0U; d < object->defined_symbols.len; ++d) if (strcmp(object->defined_symbols.data[d], object->fixups.data[i].symbol) == 0) symbol_index = function_indices.data[d];
        for (size_t d = 0U; d < object->data_symbols.len; ++d) if (strcmp(object->data_symbols.data[d].name, object->fixups.data[i].symbol) == 0) symbol_index = data_indices.data[d];
        for (size_t d = 0U; d < object->debug_sections.len; ++d) if (strcmp(object->debug_sections.data[d].name, object->fixups.data[i].symbol) == 0) symbol_index = debug_indices[d];
        for (size_t u = 0U; u < undefined.len; ++u) if (strcmp(undefined.data[u], object->fixups.data[i].symbol) == 0) symbol_index = undefined_indices.data[u];
        if (symbol_index == SIZE_MAX) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "internal relocation symbol lookup failure for '%s'", object->fixups.data[i].symbol); continue; }
        Rela rela;
        rela.r_offset = object->fixups.data[i].offset;
        rela.r_info = ((uint64_t)symbol_index << 32U) | (uint32_t)object->fixups.data[i].type;
        rela.r_addend = object->fixups.data[i].addend;
        unsigned section = object->fixups.data[i].section_kind;
        size_t extent = 0U, width = section == 0U || object->fixups.data[i].type == 10 ? 4U : 8U;
        bool valid_type = false;
        if (section == 0U) { extent = object->text.len; valid_type = object->fixups.data[i].type == 2 || object->fixups.data[i].type == 4; }
        else if (section <= RODATA_SECTION) { extent = section == DATA_SECTION ? object->data.len : object->rodata.len; valid_type = object->fixups.data[i].type == 1; }
        else if (section >= 4U && (size_t)section - 4U < object->debug_sections.len) {
            extent = object->debug_sections.data[section - 4U].bytes.len;
            valid_type = object->fixups.data[i].type == 1 || object->fixups.data[i].type == 10;
        }
        if (!valid_type || object->fixups.data[i].offset > extent || width > extent - object->fixups.data[i].offset) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "relocation is outside its section or has an invalid width/type"); continue;
        }
        for (size_t j = 0U; j < i; ++j) {
            const CinderFixup *previous = &object->fixups.data[j];
            if (previous->section_kind != section) continue;
            size_t previous_width = section == 0U || previous->type == 10 ? 4U : 8U;
            if (previous->offset <= extent && previous_width <= extent - previous->offset &&
                previous->offset < object->fixups.data[i].offset + width && object->fixups.data[i].offset < previous->offset + previous_width) {
                cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "object relocations overlap"); break;
            }
        }
        append_relocation(&relabytes[section >= 4U ? section - 1U : section], &rela);
    }
    if (diags->errors != 0U) goto failure;

    Section sections[SECTION_LIMIT];
    memset(sections, 0, sizeof(sections));
    sections[0] = (Section){"", 0U, 0U, 0U, NULL, 0U, 0U, 0U, 0U, section_name_offsets[0], 0U};
    sections[1] = (Section){".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16U, object->text.data, object->text.len, 0U, 0U, 0U, section_name_offsets[1], 0U};
    sections[2] = (Section){".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 16U, object->data.data, object->data.len, 0U, 0U, 0U, section_name_offsets[2], 0U};
    sections[3] = (Section){".rodata", SHT_PROGBITS, SHF_ALLOC, 16U, object->rodata.data, object->rodata.len, 0U, 0U, 0U, section_name_offsets[3], 0U};
    sections[4] = (Section){".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 16U, NULL, object->bss_size, 0U, 0U, 0U, section_name_offsets[4], 0U};
    sections[5] = (Section){".rela.text", SHT_RELA, 0U, 8U, relabytes[0].data, relabytes[0].len, 9U, 1U, ELF_RELA_SIZE, section_name_offsets[5], 0U};
    sections[6] = (Section){".rela.data", SHT_RELA, 0U, 8U, relabytes[1].data, relabytes[1].len, 9U, 2U, ELF_RELA_SIZE, section_name_offsets[6], 0U};
    sections[7] = (Section){".rela.rodata", SHT_RELA, 0U, 8U, relabytes[2].data, relabytes[2].len, 9U, 3U, ELF_RELA_SIZE, section_name_offsets[7], 0U};
    sections[8] = (Section){".note.GNU-stack", SHT_PROGBITS, 0U, 1U, NULL, 0U, 0U, 0U, 0U, section_name_offsets[8], 0U};
    sections[9] = (Section){".symtab", SHT_SYMTAB, 0U, 8U, symbytes.data, symbytes.len, 10U, first_global, ELF_SYMBOL_SIZE, section_name_offsets[9], 0U};
    sections[10] = (Section){".strtab", SHT_STRTAB, 0U, 1U, strtab.data, strtab.len, 0U, 0U, 0U, section_name_offsets[10], 0U};
    sections[11] = (Section){".shstrtab", SHT_STRTAB, 0U, 1U, shstrtab.data, shstrtab.len, 0U, 0U, 0U, section_name_offsets[11], 0U};
    for (size_t i = 0U; i < object->debug_sections.len; ++i) {
        const CinderDebugSection *debug = &object->debug_sections.data[i];
        size_t at = 12U + 2U * i;
        sections[at] = (Section){debug->name, SHT_PROGBITS, 0U, debug->alignment, debug->bytes.data, debug->bytes.len, 0U, 0U, 0U, debug_name_offsets[i], 0U};
        sections[at + 1U] = (Section){debug_rela_names[i], SHT_RELA, 0U, 8U, relabytes[3U + i].data, relabytes[3U + i].len, 9U, (uint32_t)at, ELF_RELA_SIZE, debug_rela_offsets[i], 0U};
    }


    size_t cursor = ELF_HEADER_SIZE;
    for (size_t i = 1U; i < section_count; ++i) {
        cursor = align_up(cursor, (size_t)sections[i].align);
        sections[i].file_offset = cursor;
        if (sections[i].size > SIZE_MAX - cursor) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "ELF section size overflow"); goto failure; }
        if (sections[i].type != SHT_NOBITS && sections[i].size != 0U) write_at(&file, cursor, sections[i].data, sections[i].size);
        if (sections[i].type != SHT_NOBITS) cursor += sections[i].size;
    }
    cursor = align_up(cursor, 8U);
    size_t section_table_offset = cursor;
    size_t section_table_size = ELF_SECTION_SIZE * section_count;
    if (cursor == SIZE_MAX || section_table_size > SIZE_MAX - cursor) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "ELF section table size overflow"); goto failure; }
    write_at(&file, section_table_offset, NULL, section_table_size);
    SectionHeader headers[SECTION_LIMIT];
    memset(headers, 0, sizeof(headers));
    for (size_t i = 1U; i < section_count; ++i) {
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
    CinderBytes serialized = {NULL, 0U, 0U};
    for (size_t i = 0U; i < section_count; ++i) append_section(&serialized, &headers[i]);
    write_at(&file, section_table_offset, serialized.data, serialized.len);
    free(serialized.data); serialized = (CinderBytes){NULL, 0U, 0U};
    ElfHeader header;
    memset(&header, 0, sizeof(header));
    header.e_ident[0] = 0x7FU; header.e_ident[1] = 'E'; header.e_ident[2] = 'L'; header.e_ident[3] = 'F'; header.e_ident[4] = 2U; header.e_ident[5] = 1U; header.e_ident[6] = 1U;
    header.e_type = 1U; header.e_machine = 62U; header.e_version = 1U; header.e_shoff = section_table_offset; header.e_ehsize = ELF_HEADER_SIZE; header.e_shentsize = ELF_SECTION_SIZE; header.e_shnum = (uint16_t)section_count; header.e_shstrndx = 11U;
    append_header(&serialized, &header);
    write_at(&file, 0U, serialized.data, serialized.len);
    free(serialized.data);
    if (diags->errors != 0U) goto failure;

    CinderOutput output;
    if (cinder_output_begin(&output, path, diags) != 0) goto failure;
    size_t written = fwrite(file.data, 1U, file.len, output.stream);
    if (written != file.len) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "short write while publishing ELF object '%s'", path);
        cinder_output_abort(&output);
        goto failure;
    }
    if (cinder_output_commit(&output, diags) != 0) goto failure;
    free(file.data); free(symbytes.data); free(strtab.data); free(shstrtab.data); for (size_t r = 0U; r < RELOCATION_LIMIT; ++r) free(relabytes[r].data); for (size_t r = 0U; r < object->debug_sections.len; ++r) free(debug_rela_names[r]); free(function_indices.data); free(data_indices.data); free(undefined_indices.data); for (size_t i = 0U; i < undefined.len; ++i) free(undefined.data[i]); free(undefined.data);
    return diags->errors == 0U ? 0 : 1;

failure:
    free(file.data); free(symbytes.data); free(strtab.data); free(shstrtab.data); for (size_t r = 0U; r < RELOCATION_LIMIT; ++r) free(relabytes[r].data); for (size_t r = 0U; r < object->debug_sections.len; ++r) free(debug_rela_names[r]); free(function_indices.data); free(data_indices.data); free(undefined_indices.data); for (size_t i = 0U; i < undefined.len; ++i) free(undefined.data[i]); free(undefined.data);
    return 1;
}
