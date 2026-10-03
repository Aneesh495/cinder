#include "pp_private.h"

#include <stdlib.h>
#include <string.h>

bool pp_ident_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool pp_ident_continue(unsigned char c) {
    return pp_ident_start(c) || (c >= '0' && c <= '9');
}

bool pp_is(PPToken token, const char *text) {
    return token.length == strlen(text) && memcmp(token.text, text, token.length) == 0;
}

PPToken pp_token(PP *pp, PPKind kind, const char *text, size_t length, CinderLoc loc) {
    PPToken token;
    memset(&token, 0, sizeof(token));
    token.kind = kind;
    token.text = cinder_arena_strndup(&pp->arena, text, length);
    token.length = length;
    token.loc = loc;
    token.spelling = loc;
    return token;
}

void pp_push(PPTokens *tokens, PPToken token) {
    cinder_vec_push((CinderVec *)tokens, &token);
}

void pp_append(PPTokens *dest, const PPTokens *source) {
    for (size_t i = 0U; i < source->len; ++i) pp_push(dest, source->data[i]);
}

void pp_destroy_tokens(PPTokens *tokens) {
    free(tokens->data);
    memset(tokens, 0, sizeof(*tokens));
}

static bool whitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

static bool starts(const char *text, size_t remaining, const char *prefix) {
    size_t length = strlen(prefix);
    return remaining >= length && memcmp(text, prefix, length) == 0;
}

static size_t literal_prefix(const char *text, size_t remaining) {
    if (remaining > 0U && (*text == '"' || *text == '\'')) return 0U;
    if (remaining >= 2U && (text[0] == 'L' || text[0] == 'u' || text[0] == 'U') &&
        (text[1] == '"' || text[1] == '\'')) return 1U;
    if (remaining >= 3U && text[0] == 'u' && text[1] == '8' && text[2] == '"') return 2U;
    return SIZE_MAX;
}

int pp_scan(PP *pp, const char *bytes, size_t length, CinderFileId file, size_t offset, PPTokens *output) {
    if (length > SIZE_MAX / sizeof(size_t) - 1U) {
        cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(file, offset, 0U), "source mapping size overflow");
        return 1;
    }
    /* Splicing precedes tokenization, including inside comments and literals. */
    char *text = cinder_alloc(length + 1U);
    size_t *positions = cinder_alloc((length + 1U) * sizeof(*positions));
    size_t n = 0U;
    for (size_t i = 0U; i < length; ++i) {
        if (bytes[i] == '\\' && i + 1U < length && bytes[i + 1U] == '\n') { ++i; continue; }
        if (bytes[i] == '\\' && i + 2U < length && bytes[i + 1U] == '\r' && bytes[i + 2U] == '\n') { i += 2U; continue; }
        positions[n] = offset + i;
        text[n++] = bytes[i];
    }
    text[n] = '\0';
    positions[n] = offset + length;
    bool space = false;
    size_t i = 0U;
    while (i < n) {
        if (whitespace((unsigned char)text[i])) { space = true; ++i; continue; }
        if (text[i] == '\n') {
            pp_push(output, pp_token(pp, PP_NEWLINE, "\n", 1U, cinder_loc(file, positions[i], 1U)));
            ++i; space = false; continue;
        }
        if (starts(text + i, n - i, "//")) {
            i += 2U;
            while (i < n && text[i] != '\n') ++i;
            space = true; continue;
        }
        if (starts(text + i, n - i, "/*")) {
            size_t start = i;
            i += 2U;
            while (i < n && !starts(text + i, n - i, "*/")) {
                if (text[i] == '\n') pp_push(output, pp_token(pp, PP_NEWLINE, "\n", 1U, cinder_loc(file, positions[i], 1U)));
                ++i;
            }
            if (i == n) {
                cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(file, positions[start], 2U), "unterminated block comment");
                break;
            }
            i += 2U; space = true; continue;
        }
        size_t start = i;
        PPKind kind = PP_PUNCT;
        size_t prefix = literal_prefix(text + i, n - i);
        if (prefix != SIZE_MAX) {
            kind = PP_LITERAL;
            i += prefix;
            char quote = text[i++];
            while (i < n && text[i] != quote && text[i] != '\n') {
                if (text[i] == '\\' && i + 1U < n) i += 2U;
                else ++i;
            }
            if (i < n && text[i] == quote) ++i;
            else cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(file, positions[start], positions[i] - positions[start]), "unterminated literal");
        } else if (pp_ident_start((unsigned char)text[i])) {
            kind = PP_IDENT;
            while (i < n && pp_ident_continue((unsigned char)text[i])) ++i;
        } else if ((text[i] >= '0' && text[i] <= '9') ||
                   (text[i] == '.' && i + 1U < n && text[i + 1U] >= '0' && text[i + 1U] <= '9')) {
            kind = PP_NUMBER;
            ++i;
            while (i < n) {
                unsigned char c = (unsigned char)text[i];
                if (pp_ident_continue(c) || c == '.') ++i;
                else if ((c == '+' || c == '-') && (text[i - 1U] == 'e' || text[i - 1U] == 'E' || text[i - 1U] == 'p' || text[i - 1U] == 'P')) ++i;
                else break;
            }
        } else {
            static const char *const punct[] = {"%:%:", ">>=", "<<=", "...", "##", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "<:", ":>", "<%", "%>", "%:"};
            size_t matched = 1U;
            for (size_t p = 0U; p < CINDER_ARRAY_LEN(punct); ++p) {
                if (starts(text + i, n - i, punct[p])) { matched = strlen(punct[p]); break; }
            }
            if (text[i] == '\0' || strchr("[](){}.&*+-~!/%<>^|?:;=,#", text[i]) == NULL) {
                cinder_diag(pp->diags, CINDER_ERROR, cinder_loc(file, positions[i], 1U), "unsupported source byte 0x%02x", (unsigned)(unsigned char)text[i]);
            }
            i += matched;
        }
        CinderLoc loc = cinder_loc(file, positions[start], positions[i] - positions[start]);
        PPToken token = pp_token(pp, kind, text + start, i - start, loc);
        token.space = space;
        if (pp_is(token, "%:")) token = pp_token(pp, PP_PUNCT, "#", 1U, loc);
        if (pp_is(token, "%:%:")) token = pp_token(pp, PP_PUNCT, "##", 2U, loc);
        if (pp_is(token, "<:")) token = pp_token(pp, PP_PUNCT, "[", 1U, loc);
        if (pp_is(token, ":>")) token = pp_token(pp, PP_PUNCT, "]", 1U, loc);
        if (pp_is(token, "<%")) token = pp_token(pp, PP_PUNCT, "{", 1U, loc);
        if (pp_is(token, "%>")) token = pp_token(pp, PP_PUNCT, "}", 1U, loc);
        token.space = space;
        pp_push(output, token);
        space = false;
        if (output->len > 1000000U) {
            cinder_diag(pp->diags, CINDER_ERROR, loc, "preprocessing token limit exceeded");
            break;
        }
    }
    free(positions);
    free(text);
    return pp->diags->errors == 0U ? 0 : 1;
}

void pp_render(PP *pp) {
    CinderBytes bytes = {0};
    bool line_start = true;
    for (size_t i = 0U; i < pp->output.len; ++i) {
        PPToken token = pp->output.data[i];
        if (token.kind == PP_EMPTY) continue;
        if (token.kind == PP_NEWLINE) {
            cinder_bytes_put8(&bytes, '\n');
            line_start = true;
            continue;
        }
        /* Separating tokens prevents accidental operator and number merging. */
        if (!line_start) cinder_bytes_put8(&bytes, ' ');
        CinderSourceSpan span;
        span.begin = bytes.len;
        span.end = bytes.len + token.length;
        span.expansion = token.loc;
        span.spelling = token.spelling;
        span.definition = token.definition;
        cinder_vec_push((CinderVec *)&pp->sources->spans, &span);
        cinder_bytes_append(&bytes, (const unsigned char *)token.text, token.length);
        line_start = false;
    }
    pp->sources->preprocessed_size = bytes.len;
    cinder_bytes_put8(&bytes, 0U);
    pp->sources->preprocessed = (char *)bytes.data;
}
