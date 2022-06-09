#include "cinder.h"

#include <string.h>

bool cinder_utf8_validate(const char *bytes, size_t length, size_t *invalid) {
    for (size_t i = 0U; i < length;) {
        unsigned char first = (unsigned char)bytes[i];
        if (first != 0U && first < 0x80U) { ++i; continue; }
        size_t count = first >= 0xC2U && first <= 0xDFU ? 2U : first >= 0xE0U && first <= 0xEFU ? 3U : first >= 0xF0U && first <= 0xF4U ? 4U : 0U;
        if (count == 0U || count > length - i) { *invalid = i; return false; }
        uint32_t value = first & (count == 2U ? 0x1FU : count == 3U ? 0x0FU : 0x07U);
        for (size_t j = 1U; j < count; ++j) {
            unsigned char next = (unsigned char)bytes[i + j];
            if ((next & 0xC0U) != 0x80U) { *invalid = i + j; return false; }
            value = (value << 6U) | (next & 0x3FU);
        }
        if ((count == 3U && value < 0x800U) || (count == 4U && value < 0x10000U) || (value >= 0xD800U && value <= 0xDFFFU) || value > 0x10FFFFU) { *invalid = i; return false; }
        i += count;
    }
    return true;
}

static unsigned hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10U;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10U;
    return 16U;
}

static size_t encode_utf8(char *bytes, uint32_t value) {
    if (value < 0x80U) { bytes[0] = (char)value; return 1U; }
    if (value < 0x800U) { bytes[0] = (char)(0xC0U | (value >> 6U)); bytes[1] = (char)(0x80U | (value & 0x3FU)); return 2U; }
    if (value < 0x10000U) { bytes[0] = (char)(0xE0U | (value >> 12U)); bytes[1] = (char)(0x80U | ((value >> 6U) & 0x3FU)); bytes[2] = (char)(0x80U | (value & 0x3FU)); return 3U; }
    bytes[0] = (char)(0xF0U | (value >> 18U)); bytes[1] = (char)(0x80U | ((value >> 12U) & 0x3FU)); bytes[2] = (char)(0x80U | ((value >> 6U) & 0x3FU)); bytes[3] = (char)(0x80U | (value & 0x3FU)); return 4U;
}

char *cinder_literal_decode(CinderArena *arena, const CinderToken *token, size_t *length, CinderDiagnostics *diags) {
    char *decoded = cinder_arena_alloc(arena, token->length + 1U, _Alignof(char));
    size_t prefix = token->length >= 3U && token->text[0] == 'u' && token->text[1] == '8' ? 2U : token->text[0] == '"' || token->text[0] == '\'' ? 0U : 1U;
    *length = 0U; decoded[0] = '\0';
    if (prefix == 1U || (prefix == 2U && token->kind == TOK_CHAR)) { cinder_diag(diags, CINDER_ERROR, token->loc, "wide/Unicode code-unit literal is outside the narrow UTF-8 profile"); return decoded; }
    if (token->length < prefix + 2U || token->text[token->length - 1U] != token->text[prefix]) { cinder_diag(diags, CINDER_ERROR, token->loc, "literal has no closing delimiter"); return decoded; }
    size_t end = token->length - 1U;
    for (size_t i = prefix + 1U; i < end; ++i) {
        unsigned char c = (unsigned char)token->text[i];
        if (c != '\\') { decoded[(*length)++] = (char)c; continue; }
        if (++i >= end) { cinder_diag(diags, CINDER_ERROR, token->loc, "incomplete escape sequence"); break; }
        c = (unsigned char)token->text[i];
        uint32_t value = 0U;
        if (c >= '0' && c <= '7') {
            value = c - '0';
            for (unsigned n = 1U; n < 3U && i + 1U < end && token->text[i + 1U] >= '0' && token->text[i + 1U] <= '7'; ++n) value = value * 8U + (unsigned char)token->text[++i] - '0';
            if (value > 255U) cinder_diag(diags, CINDER_ERROR, token->loc, "octal escape exceeds the target byte range");
        } else if (c == 'x') {
            size_t start = i; bool overflow = false;
            while (i + 1U < end && hex_digit((unsigned char)token->text[i + 1U]) < 16U) {
                unsigned digit = hex_digit((unsigned char)token->text[++i]);
                if (value > (255U - digit) / 16U) overflow = true;
                if (!overflow) value = value * 16U + digit;
            }
            if (i == start) cinder_diag(diags, CINDER_ERROR, token->loc, "hexadecimal escape requires a digit");
            if (overflow) cinder_diag(diags, CINDER_ERROR, token->loc, "hexadecimal escape exceeds the target byte range");
        } else if (c == 'u' || c == 'U') {
            unsigned digits = c == 'u' ? 4U : 8U; bool valid = true;
            for (unsigned n = 0U; n < digits; ++n) {
                if (i + 1U >= end || hex_digit((unsigned char)token->text[i + 1U]) == 16U) { valid = false; break; }
                value = value * 16U + hex_digit((unsigned char)token->text[++i]);
            }
            if (!valid || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU) || (value < 0xA0U && value != 0x24U && value != 0x40U && value != 0x60U)) cinder_diag(diags, CINDER_ERROR, token->loc, "invalid universal character name in literal");
            else *length += encode_utf8(decoded + *length, value);
            continue;
        } else {
            switch (c) {
                case '\'': case '"': case '?': case '\\': value = c; break;
                case 'a': value = 7U; break; case 'b': value = 8U; break;
                case 'f': value = 12U; break; case 'n': value = 10U; break;
                case 'r': value = 13U; break; case 't': value = 9U; break;
                case 'v': value = 11U; break;
                default: cinder_diag(diags, CINDER_ERROR, token->loc, "unknown escape sequence"); value = c; break;
            }
        }
        decoded[(*length)++] = (char)value;
    }
    decoded[*length] = '\0';
    return decoded;
}
