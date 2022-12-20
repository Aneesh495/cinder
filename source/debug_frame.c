#include "cinder.h"

#include <string.h>

/* DWARF v4, little-endian DWARF32, x86-64 address size. Section-relative
 * offsets remain original bytes; target addresses use original ELF fixups. */
static void uleb(CinderBytes *bytes, uint64_t value) {
    do {
        uint8_t byte = (uint8_t)(value & 127U); value >>= 7U;
        cinder_bytes_put8(bytes, (uint8_t)(byte | (value != 0U ? 128U : 0U)));
    } while (value != 0U);
}

size_t cinder_debug_section(CinderMachineObject *object, const char *name, size_t alignment) {
    for (size_t i = 0U; i < object->debug_sections.len; ++i)
        if (strcmp(object->debug_sections.data[i].name, name) == 0) return i;
    CinderDebugSection section;
    memset(&section, 0, sizeof(section));
    section.name = cinder_strndup(name, strlen(name)); section.alignment = alignment;
    cinder_vec_push((CinderVec *)&object->debug_sections, &section);
    return object->debug_sections.len - 1U;
}

bool cinder_debug_sections_valid(const CinderMachineObject *object, CinderDiagnostics *diags) {
    if (object->debug_sections.len > 16U || (object->debug_sections.len != 0U && object->debug_sections.data == NULL)) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "invalid object debug section inventory"); return false;
    }
    for (size_t i = 0U; i < object->debug_sections.len; ++i) {
        const CinderDebugSection *section = &object->debug_sections.data[i];
        size_t length = section->name == NULL ? 0U : strlen(section->name);
        bool valid_name = length > 7U && length <= 64U && strncmp(section->name, ".debug_", 7U) == 0;
        for (size_t n = 7U; valid_name && n < length; ++n) {
            unsigned char ch = (unsigned char)section->name[n];
            valid_name = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
        }
        if (!valid_name || section->alignment == 0U || section->alignment > 16U ||
            (section->alignment & (section->alignment - 1U)) != 0U || section->bytes.len > UINT32_MAX ||
            (section->bytes.len != 0U && section->bytes.data == NULL)) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "invalid object debug section"); return false;
        }
        for (size_t j = 0U; j < i; ++j) if (strcmp(section->name, object->debug_sections.data[j].name) == 0) {
            cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "duplicate object debug section"); return false;
        }
    }
    return true;
}

static CinderBytes *frame_bytes(const CinderUnwind *unwind, CinderMachineObject *object) {
    return &object->debug_sections.data[unwind->section].bytes;
}

static void advance(CinderUnwind *unwind, CinderMachineObject *object, size_t position) {
    if (position == unwind->position) return;
    CinderBytes *bytes = frame_bytes(unwind, object);
    cinder_bytes_put8(bytes, 4U); /* DW_CFA_advance_loc4 */
    cinder_bytes_put32(bytes, (uint32_t)(position - unwind->position));
    unwind->position = position;
}

static void same_value(CinderBytes *bytes, unsigned reg) {
    cinder_bytes_put8(bytes, 8U); uleb(bytes, reg);
}

static void finish_record(CinderBytes *bytes, size_t record) {
    while ((bytes->len & 7U) != 0U) cinder_bytes_put8(bytes, 0U);
    cinder_bytes_patch32(bytes, record, (uint32_t)(bytes->len - record - 4U));
}

void cinder_unwind_begin(CinderUnwind *unwind, CinderMachineObject *object, const char *symbol, size_t start) {
    memset(unwind, 0, sizeof(*unwind)); unwind->enabled = object->debug_enabled;
    if (!unwind->enabled) return;
    unwind->section = cinder_debug_section(object, ".debug_frame", 8U);
    unwind->function_start = start; unwind->position = start;
    CinderBytes *bytes = frame_bytes(unwind, object);
    if (bytes->len == 0U) {
        cinder_bytes_put32(bytes, 0U); cinder_bytes_put32(bytes, UINT32_MAX);
        cinder_bytes_put8(bytes, 4U); cinder_bytes_put8(bytes, 0U); /* version, augmentation */
        cinder_bytes_put8(bytes, 8U); cinder_bytes_put8(bytes, 0U); /* address and segment sizes */
        uleb(bytes, 1U); cinder_bytes_put8(bytes, 0x78U); uleb(bytes, 16U); /* alignments 1/-8, RIP */
        cinder_bytes_put8(bytes, 0x0CU); uleb(bytes, 7U); uleb(bytes, 8U); /* CFA = RSP + 8 */
        cinder_bytes_put8(bytes, 0x90U); uleb(bytes, 1U); /* RIP at CFA - 8 */
        same_value(bytes, 6U);
        for (unsigned reg = 12U; reg <= 15U; ++reg) same_value(bytes, reg);
        finish_record(bytes, 0U);
    }
    unwind->record = bytes->len;
    cinder_bytes_put32(bytes, 0U);
    CinderFixup cie = {bytes->len, cinder_strndup(".debug_frame", 12U), 10, 0, (unsigned)(4U + unwind->section)};
    cinder_vec_push((CinderVec *)&object->fixups, &cie);
    cinder_bytes_put32(bytes, 0U); /* section-relative CIE offset */
    CinderFixup address = {bytes->len, cinder_strndup(symbol, strlen(symbol)), 1, 0, (unsigned)(4U + unwind->section)};
    cinder_vec_push((CinderVec *)&object->fixups, &address);
    cinder_bytes_put64(bytes, 0U); cinder_bytes_put64(bytes, 0U); /* initial PC and range */
}

void cinder_unwind_prologue(CinderUnwind *unwind, CinderMachineObject *object) {
    if (!unwind->enabled) return;
    CinderBytes *bytes = frame_bytes(unwind, object);
    advance(unwind, object, unwind->function_start + 1U); /* after push RBP */
    cinder_bytes_put8(bytes, 0x0EU); uleb(bytes, 16U);
    cinder_bytes_put8(bytes, 0x86U); uleb(bytes, 2U);
    advance(unwind, object, unwind->function_start + 4U); /* after mov RBP,RSP */
    cinder_bytes_put8(bytes, 0x0DU); uleb(bytes, 6U);
}

void cinder_unwind_saved(CinderUnwind *unwind, CinderMachineObject *object, unsigned reg, int offset) {
    if (!unwind->enabled) return;
    advance(unwind, object, object->text.len);
    CinderBytes *bytes = frame_bytes(unwind, object);
    cinder_bytes_put8(bytes, (uint8_t)(0x80U | reg));
    uleb(bytes, (uint64_t)(16 - (int64_t)offset) / 8U);
}

void cinder_unwind_epilogue_begin(CinderUnwind *unwind, CinderMachineObject *object) {
    if (!unwind->enabled) return;
    advance(unwind, object, object->text.len);
    cinder_bytes_put8(frame_bytes(unwind, object), 0x0AU); /* remember body rules */
}

void cinder_unwind_restored(CinderUnwind *unwind, CinderMachineObject *object, unsigned reg) {
    if (!unwind->enabled) return;
    advance(unwind, object, object->text.len);
    same_value(frame_bytes(unwind, object), reg);
}

void cinder_unwind_leave(CinderUnwind *unwind, CinderMachineObject *object) {
    if (!unwind->enabled) return;
    advance(unwind, object, object->text.len);
    CinderBytes *bytes = frame_bytes(unwind, object);
    cinder_bytes_put8(bytes, 0x0CU); uleb(bytes, 7U); uleb(bytes, 8U);
    same_value(bytes, 6U);
}

void cinder_unwind_epilogue_end(CinderUnwind *unwind, CinderMachineObject *object) {
    if (!unwind->enabled) return;
    advance(unwind, object, object->text.len);
    cinder_bytes_put8(frame_bytes(unwind, object), 0x0BU); /* restore following block rules */
}

int cinder_unwind_finish(CinderUnwind *unwind, CinderMachineObject *object, CinderDiagnostics *diags) {
    if (!unwind->enabled) return 0;
    CinderBytes *bytes = frame_bytes(unwind, object);
    if (object->text.len < unwind->function_start || object->text.len - unwind->function_start > UINT32_MAX || bytes->len > UINT32_MAX - 7U) {
        cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "debug frame exceeds the DWARF32 range"); return 1;
    }
    uint64_t size = (uint64_t)(object->text.len - unwind->function_start);
    for (unsigned i = 0U; i < 8U; ++i) bytes->data[unwind->record + 16U + i] = (uint8_t)(size >> (i * 8U));
    finish_record(bytes, unwind->record);
    return 0;
}
