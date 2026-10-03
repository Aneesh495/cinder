#include "cinder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cinder_tokens_init(CinderTokenStream *tokens) {
    tokens->tokens.data = NULL;
    tokens->tokens.len = 0U;
    tokens->tokens.cap = 0U;
    tokens->text = NULL;
    tokens->text_len = 0U;
}

void cinder_tokens_destroy(CinderTokenStream *tokens) {
    free(tokens->tokens.data);
    free(tokens->text);
    tokens->tokens.data = NULL;
    tokens->tokens.len = 0U;
    tokens->tokens.cap = 0U;
    tokens->text = NULL;
    tokens->text_len = 0U;
}

const char *cinder_token_name(CinderTokenKind kind) {
    static const char *names[] = {
        [TOK_EOF - TOK_EOF] = "eof",
        [TOK_IDENTIFIER - TOK_EOF] = "identifier",
        [TOK_NUMBER - TOK_EOF] = "number",
        [TOK_STRING - TOK_EOF] = "string",
        [TOK_CHAR - TOK_EOF] = "character",
    };
    if (kind >= TOK_EOF && (size_t)(kind - TOK_EOF) < CINDER_ARRAY_LEN(names) && names[kind - TOK_EOF] != NULL) {
        return names[kind - TOK_EOF];
    }
    switch (kind) {
        case TOK_ARROW: return "->";
        case TOK_EQEQ: return "==";
        case TOK_NEQ: return "!=";
        case TOK_LE: return "<=";
        case TOK_GE: return ">=";
        case TOK_ANDAND: return "&&";
        case TOK_OROR: return "||";
        case TOK_SHL: return "<<";
        case TOK_SHR: return ">>";
        case TOK_PLUSPLUS: return "++";
        case TOK_MINUSMINUS: return "--";
        case TOK_PLUSEQ: return "+=";
        case TOK_MINUSEQ: return "-=";
        case TOK_STAREQ: return "*=";
        case TOK_SLASHEQ: return "/=";
        case TOK_PERCENTEQ: return "%=";
        case TOK_ANDEQ: return "&=";
        case TOK_OREQ: return "|=";
        case TOK_XOREQ: return "^=";
        case TOK_LSHIFT_EQ: return "<<=";
        case TOK_RSHIFT_EQ: return ">>=";
        case TOK_ELLIPSIS: return "...";
        case TOK_KW_AUTO: return "auto";
        case TOK_KW_BREAK: return "break";
        case TOK_KW_CASE: return "case";
        case TOK_KW_CHAR: return "char";
        case TOK_KW_CONST: return "const";
        case TOK_KW_CONTINUE: return "continue";
        case TOK_KW_DEFAULT: return "default";
        case TOK_KW_DO: return "do";
        case TOK_KW_DOUBLE: return "double";
        case TOK_KW_ELSE: return "else";
        case TOK_KW_ENUM: return "enum";
        case TOK_KW_EXTERN: return "extern";
        case TOK_KW_FLOAT: return "float";
        case TOK_KW_FOR: return "for";
        case TOK_KW_GOTO: return "goto";
        case TOK_KW_IF: return "if";
        case TOK_KW_INLINE: return "inline";
        case TOK_KW_INT: return "int";
        case TOK_KW_LONG: return "long";
        case TOK_KW_REGISTER: return "register";
        case TOK_KW_RESTRICT: return "restrict";
        case TOK_KW_RETURN: return "return";
        case TOK_KW_SHORT: return "short";
        case TOK_KW_SIGNED: return "signed";
        case TOK_KW_SIZEOF: return "sizeof";
        case TOK_KW_STATIC: return "static";
        case TOK_KW_STRUCT: return "struct";
        case TOK_KW_SWITCH: return "switch";
        case TOK_KW_TYPEDEF: return "typedef";
        case TOK_KW_UNION: return "union";
        case TOK_KW_UNSIGNED: return "unsigned";
        case TOK_KW_VOID: return "void";
        case TOK_KW_VOLATILE: return "volatile";
        case TOK_KW_WHILE: return "while";
        case TOK_KW__BOOL: return "_Bool";
        case TOK_KW_ALIGNAS: return "_Alignas";
        case TOK_KW_ALIGNOF: return "_Alignof";
        case TOK_KW_GENERIC: return "_Generic";
        case TOK_KW_NORETURN: return "_Noreturn";
        case TOK_KW_STATIC_ASSERT: return "_Static_assert";
        default: break;
    }
    return "punctuation";
}

void cinder_dump_tokens(const CinderTokenStream *tokens, CinderSourceManager *sources, FILE *out) {
    for (size_t i = 0U; i < tokens->tokens.len; ++i) {
        const CinderToken *token = &tokens->tokens.data[i];
        CinderLoc loc = token->loc;
        cinder_loc_linecol(sources, &loc);
        fprintf(out, "%s:%u:%u: %-12s ", cinder_loc_name(sources, loc), loc.line, loc.column, cinder_token_name(token->kind));
        if (token->kind >= 0 && token->kind < TOK_EOF) {
            fprintf(out, "'%c'", (char)token->kind);
        } else {
            fprintf(out, "%.*s", (int)token->length, token->text);
        }
        if (token->kind == TOK_NUMBER) {
            fprintf(out, " = %lld", (long long)token->integer);
        }
        fputc('\n', out);
    }
}
