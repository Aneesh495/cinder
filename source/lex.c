#include "cinder.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static CinderTokenKind keyword_kind(const char *text, size_t length) {
    static const struct { const char *name; CinderTokenKind kind; } keywords[] = {
        {"auto", TOK_KW_AUTO}, {"break", TOK_KW_BREAK}, {"case", TOK_KW_CASE}, {"char", TOK_KW_CHAR},
        {"const", TOK_KW_CONST}, {"continue", TOK_KW_CONTINUE}, {"default", TOK_KW_DEFAULT}, {"do", TOK_KW_DO},
        {"double", TOK_KW_DOUBLE}, {"else", TOK_KW_ELSE}, {"enum", TOK_KW_ENUM}, {"extern", TOK_KW_EXTERN},
        {"float", TOK_KW_FLOAT}, {"for", TOK_KW_FOR}, {"goto", TOK_KW_GOTO}, {"if", TOK_KW_IF},
        {"inline", TOK_KW_INLINE}, {"int", TOK_KW_INT}, {"long", TOK_KW_LONG}, {"register", TOK_KW_REGISTER},
        {"restrict", TOK_KW_RESTRICT}, {"return", TOK_KW_RETURN}, {"short", TOK_KW_SHORT}, {"signed", TOK_KW_SIGNED},
        {"sizeof", TOK_KW_SIZEOF}, {"static", TOK_KW_STATIC}, {"struct", TOK_KW_STRUCT}, {"switch", TOK_KW_SWITCH},
        {"typedef", TOK_KW_TYPEDEF}, {"union", TOK_KW_UNION}, {"unsigned", TOK_KW_UNSIGNED}, {"void", TOK_KW_VOID},
        {"volatile", TOK_KW_VOLATILE}, {"while", TOK_KW_WHILE}, {"_Bool", TOK_KW__BOOL}, {"_Alignas", TOK_KW_ALIGNAS},
        {"_Alignof", TOK_KW_ALIGNOF}, {"_Generic", TOK_KW_GENERIC}, {"_Noreturn", TOK_KW_NORETURN},
        {"_Static_assert", TOK_KW_STATIC_ASSERT},
    };
    for (size_t i = 0U; i < CINDER_ARRAY_LEN(keywords); ++i) {
        if (strlen(keywords[i].name) == length && memcmp(keywords[i].name, text, length) == 0) {
            return keywords[i].kind;
        }
    }
    return TOK_IDENTIFIER;
}

static bool is_ident_start(unsigned char c) {
    return (c >= (unsigned char)'a' && c <= (unsigned char)'z') || (c >= (unsigned char)'A' && c <= (unsigned char)'Z') || c == (unsigned char)'_';
}

static bool is_ident_continue(unsigned char c) {
    return is_ident_start(c) || (c >= (unsigned char)'0' && c <= (unsigned char)'9');
}

static void push_token(CinderTokenStream *tokens, CinderTokenKind kind, const char *text, size_t length, CinderLoc loc, int64_t integer) {
    CinderToken token; memset(&token, 0, sizeof(token));
    token.kind = kind;
    token.text = text;
    token.length = length;
    token.loc = loc;
    token.integer = integer;
    token.is_floating = false;
    token.floating = 0.0;
    cinder_vec_push((CinderVec *)&tokens->tokens, &token);
}

static CinderTokenKind two_char_kind(char a, char b) {
    if (a == '-' && b == '>') return TOK_ARROW;
    if (a == '=' && b == '=') return TOK_EQEQ;
    if (a == '!' && b == '=') return TOK_NEQ;
    if (a == '<' && b == '=') return TOK_LE;
    if (a == '>' && b == '=') return TOK_GE;
    if (a == '&' && b == '&') return TOK_ANDAND;
    if (a == '|' && b == '|') return TOK_OROR;
    if (a == '<' && b == '<') return TOK_SHL;
    if (a == '>' && b == '>') return TOK_SHR;
    if (a == '+' && b == '+') return TOK_PLUSPLUS;
    if (a == '-' && b == '-') return TOK_MINUSMINUS;
    if (a == '+' && b == '=') return TOK_PLUSEQ;
    if (a == '-' && b == '=') return TOK_MINUSEQ;
    if (a == '*' && b == '=') return TOK_STAREQ;
    if (a == '/' && b == '=') return TOK_SLASHEQ;
    if (a == '%' && b == '=') return TOK_PERCENTEQ;
    if (a == '&' && b == '=') return TOK_ANDEQ;
    if (a == '|' && b == '=') return TOK_OREQ;
    if (a == '^' && b == '=') return TOK_XOREQ;
    return 0;
}

int cinder_lex(CinderSourceManager *sources, CinderTokenStream *tokens, CinderDiagnostics *diags) {
    const char *text = sources->preprocessed;
    size_t length = sources->preprocessed_size;
    size_t i = 0U;
    while (i < length) {
        unsigned char c = (unsigned char)text[i];
        if (isspace(c) != 0) {
            ++i;
            continue;
        }
        size_t start = i;
        CinderLoc loc = cinder_preprocessed_loc(sources, start, 1U);
        size_t prefix = i + 1U < length && (c == 'L' || c == 'u' || c == 'U') && (text[i + 1U] == '"' || text[i + 1U] == '\'') ? 1U : i + 2U < length && c == 'u' && text[i + 1U] == '8' && text[i + 2U] == '"' ? 2U : 0U;
        if (is_ident_start(c) && prefix == 0U) {
            ++i;
            while (i < length && is_ident_continue((unsigned char)text[i])) ++i;
            CinderTokenKind kind = keyword_kind(text + start, i - start);
            push_token(tokens, kind, text + start, i - start, cinder_preprocessed_loc(sources, start, i - start), 0);
            continue;
        }
        if (isdigit(c) != 0 || (c == '.' && i + 1U < length && isdigit((unsigned char)text[i + 1U]) != 0)) {
            ++i;
            while (i < length) {
                unsigned char next = (unsigned char)text[i];
                if (isalnum(next) != 0 || next == '.' || next == '_') { ++i; continue; }
                if ((next == '+' || next == '-') && i > start && (text[i - 1U] == 'e' || text[i - 1U] == 'E' || text[i - 1U] == 'p' || text[i - 1U] == 'P')) { ++i; continue; }
                break;
            }
            push_token(tokens, TOK_NUMBER, text + start, i - start, cinder_preprocessed_loc(sources, start, i - start), 0);
            (void)cinder_parse_number(&tokens->tokens.data[tokens->tokens.len - 1U], diags);
            continue;
        }
        if (c == '"' || c == '\'' || prefix != 0U) {
            i += prefix;
            unsigned char quote = (unsigned char)text[i];
            ++i;
            bool closed = false;
            while (i < length) {
                if (text[i] == '\\' && i + 1U < length) {
                    i += 2U;
                    continue;
                }
                if ((unsigned char)text[i] == quote) {
                    ++i;
                    closed = true;
                    break;
                }
                if (text[i] == '\n') break;
                ++i;
            }
            if (!closed) {
                cinder_diag(diags, CINDER_ERROR, loc, "unterminated %s literal", quote == '"' ? "string" : "character");
            }
            push_token(tokens, quote == '"' ? TOK_STRING : TOK_CHAR, text + start, i - start, cinder_preprocessed_loc(sources, start, i - start), 0);
            continue;
        }
        if (i + 2U < length && text[i] == '.' && text[i + 1U] == '.' && text[i + 2U] == '.') {
            push_token(tokens, TOK_ELLIPSIS, text + i, 3U, cinder_preprocessed_loc(sources, i, 3U), 0);
            i += 3U;
            continue;
        }
        if (i + 2U < length && ((text[i] == '<' && text[i + 1U] == '<' && text[i + 2U] == '=') || (text[i] == '>' && text[i + 1U] == '>' && text[i + 2U] == '='))) {
            push_token(tokens, text[i] == '<' ? TOK_LSHIFT_EQ : TOK_RSHIFT_EQ, text + i, 3U, cinder_preprocessed_loc(sources, i, 3U), 0);
            i += 3U;
            continue;
        }
        if (i + 1U < length) {
            CinderTokenKind pair = two_char_kind(text[i], text[i + 1U]);
            if (pair != 0) {
                push_token(tokens, pair, text + i, 2U, cinder_preprocessed_loc(sources, i, 2U), 0);
                i += 2U;
                continue;
            }
        }
        if (strchr("{}[]();,:?~!+-*/%&|^<>=.", (int)c) != NULL) {
            push_token(tokens, (CinderTokenKind)c, text + i, 1U, loc, 0);
            ++i;
            continue;
        }
        cinder_diag(diags, CINDER_ERROR, loc, "unexpected character '%c'", c);
        ++i;
    }
    push_token(tokens, TOK_EOF, text + length, 0U, cinder_preprocessed_loc(sources, length, 0U), 0);
    return diags->errors == 0U ? 0 : 1;
}
