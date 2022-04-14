#ifndef CINDER_PP_PRIVATE_H
#define CINDER_PP_PRIVATE_H
#include "cinder.h"

typedef enum { PP_IDENT, PP_NUMBER, PP_LITERAL, PP_PUNCT, PP_NEWLINE, PP_EMPTY } PPKind;
typedef struct PPHide PPHide;
struct PPHide {
    unsigned id;
    const PPHide *next;
};
typedef struct {
    PPKind kind;
    const char *text;
    size_t length;
    bool space;
    CinderLoc loc;
    CinderLoc spelling;
    CinderLoc definition;
    const PPHide *hide;
} PPToken;
typedef struct { PPToken *data; size_t len; size_t cap; } PPTokens;
typedef struct {
    const char *name;
    unsigned id;
    bool function;
    bool variadic;
    bool predefined;
    CINDER_VEC_TYPE(const char *) params;
    PPTokens replacement;
    CinderLoc definition;
} PPMacro;
typedef struct {
    CinderFileId file;
    bool once;
    bool included;
} PPFile;
typedef struct {
    CinderSourceManager *sources;
    CinderDiagnostics *diags;
    CinderArena arena;
    CINDER_VEC_TYPE(PPMacro) macros;
    CINDER_VEC_TYPE(const char *) include_dirs;
    CINDER_VEC_TYPE(PPFile) files;
    PPTokens output;
    unsigned next_macro;
    size_t expansion_steps;
    size_t produced_tokens;
    const char *date;
    const char *time;
} PP;

bool pp_ident_start(unsigned char c);
bool pp_ident_continue(unsigned char c);
bool pp_is(PPToken token, const char *text);
PPToken pp_token(PP *pp, PPKind kind, const char *text, size_t length, CinderLoc loc);
void pp_push(PPTokens *tokens, PPToken token);
void pp_append(PPTokens *dest, const PPTokens *source);
void pp_destroy_tokens(PPTokens *tokens);
int pp_scan(PP *pp, const char *bytes, size_t length, CinderFileId file, size_t offset, PPTokens *output);
PPMacro *pp_find(PP *pp, PPToken token);
int pp_define(PP *pp, const PPTokens *line, bool predefined);
void pp_undef(PP *pp, PPToken name);
int pp_expand(PP *pp, const PPTokens *input, PPTokens *output, unsigned depth);
int pp_evaluate(PP *pp, const PPTokens *tokens, bool *result);
int pp_process(PP *pp, const char *path, unsigned depth);
void pp_render(PP *pp);
void pp_pragma(PP *pp, const PPTokens *line, CinderLoc loc);

#endif
