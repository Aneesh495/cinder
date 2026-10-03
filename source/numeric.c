#include "cinder.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int digit(unsigned char value) {
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'f') return (int)(value - 'a') + 10;
    if (value >= 'A' && value <= 'F') return (int)(value - 'A') + 10;
    return -1;
}

static int64_t signed_bits(uint64_t bits) {
    return bits <= (uint64_t)INT64_MAX ? (int64_t)bits : -1 - (int64_t)~bits;
}

static int error(CinderToken *token, CinderDiagnostics *diags, const char *reason) {
    cinder_diag(diags, CINDER_ERROR, token->loc, "invalid numeric constant: %s", reason);
    return 1;
}

static int integer_number(CinderToken *token, CinderDiagnostics *diags) {
    const char *text = token->text;
    size_t length = token->length, position = 0U;
    unsigned base = 10U;
    if (length >= 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { base = 16U; position = 2U; }
    else if (length > 1U && text[0] == '0') base = 8U;
    size_t first = position;
    uint64_t value = 0U;
    while (position < length) {
        int next = digit((unsigned char)text[position]);
        if (next < 0 || (unsigned)next >= base) break;
        if (value > (UINT64_MAX - (unsigned)next) / base) return error(token, diags, "integer exceeds target uint64 range");
        value = value * base + (unsigned)next;
        ++position;
    }
    if (position == first) return error(token, diags, "integer has no digits");
    bool unsig = false;
    unsigned rank = 0U;
    for (unsigned part = 0U; part < 2U && position < length; ++part) {
        char c = text[position];
        if ((c == 'u' || c == 'U') && !unsig) { unsig = true; ++position; }
        else if ((c == 'l' || c == 'L') && rank == 0U) {
            ++position; rank = 1U;
            if (position < length && text[position] == c) { ++position; rank = 2U; }
        } else break;
    }
    if (position != length) return error(token, diags, "invalid digit or integer suffix");
    unsigned selected = rank;
    bool selected_unsigned = unsig;
    if (rank == 0U) {
        if (unsig) selected = value <= UINT32_MAX ? 0U : 1U;
        else if (value <= INT32_MAX) selected = 0U;
        else if (base != 10U && value <= UINT32_MAX) { selected = 0U; selected_unsigned = true; }
        else { selected = 1U; selected_unsigned = value > (uint64_t)INT64_MAX; }
    } else if (!unsig && value > (uint64_t)INT64_MAX) selected_unsigned = true;
    if (base == 10U && !unsig && selected_unsigned) return error(token, diags, "decimal integer has no representable signed type");
    token->integer = signed_bits(value);
    token->number_unsigned = selected_unsigned;
    token->number_rank = selected;
    token->is_floating = false;
    return 0;
}

static int floating_number(CinderToken *token, CinderDiagnostics *diags, bool hex) {
    size_t end = token->length;
    bool single = end > 0U && (token->text[end - 1U] == 'f' || token->text[end - 1U] == 'F');
    if (single) --end;
    if (end > 0U && (token->text[end - 1U] == 'l' || token->text[end - 1U] == 'L')) return error(token, diags, "long double is outside the declared profile");
    size_t position = hex ? 2U : 0U;
    unsigned base = hex ? 16U : 10U;
    size_t digits = 0U;
    while (position < end && digit((unsigned char)token->text[position]) >= 0 && (unsigned)digit((unsigned char)token->text[position]) < base) { ++position; ++digits; }
    if (position < end && token->text[position] == '.') {
        ++position;
        while (position < end && digit((unsigned char)token->text[position]) >= 0 && (unsigned)digit((unsigned char)token->text[position]) < base) { ++position; ++digits; }
    }
    if (digits == 0U) return error(token, diags, "floating significand has no digits");
    bool exponent = position < end && (hex ? token->text[position] == 'p' || token->text[position] == 'P' : token->text[position] == 'e' || token->text[position] == 'E');
    if (exponent) {
        ++position;
        if (position < end && (token->text[position] == '+' || token->text[position] == '-')) ++position;
        size_t begin = position;
        while (position < end && token->text[position] >= '0' && token->text[position] <= '9') ++position;
        if (position == begin) return error(token, diags, "exponent has no digits");
    }
    if (position != end || (hex && !exponent)) return error(token, diags, "invalid floating digits, exponent, or suffix");
    /* The target contract is IEEE binary32/binary64. Conversion via the host
     * is permitted only on hosts providing those exact arithmetic formats. */
    if (FLT_RADIX != 2 || FLT_MANT_DIG != 24 || DBL_MANT_DIG != 53 || FLT_MAX_EXP != 128 || DBL_MAX_EXP != 1024) return error(token, diags, "host cannot convert declared target floating formats");
    char *copy = cinder_strndup(token->text, end);
    char *tail = NULL;
    errno = 0;
    double value = single ? (double)strtof(copy, &tail) : strtod(copy, &tail);
    bool valid = tail == copy + end && isfinite(value);
    if (!isfinite(value)) valid = false;
    free(copy);
    if (!valid) return error(token, diags, "floating value exceeds target range");
    token->is_floating = true;
    token->number_float32 = single;
    token->floating = value;
    return 0;
}

int cinder_parse_number(CinderToken *token, CinderDiagnostics *diags) {
    bool hex = token->length >= 2U && token->text[0] == '0' && (token->text[1] == 'x' || token->text[1] == 'X');
    bool floating = false;
    for (size_t i = 0U; i < token->length; ++i) {
        char c = token->text[i];
        if (c == '.' || (hex ? c == 'p' || c == 'P' : c == 'e' || c == 'E')) floating = true;
    }
    return floating ? floating_number(token, diags, hex) : integer_number(token, diags);
}
