#include "pp_private.h"

#include <stdlib.h>
#include <string.h>

typedef struct { PPTokens raw; PPTokens expanded; } PPArgument;
typedef struct { PPArgument *data; size_t len; size_t cap; } PPArguments;

static bool hidden(const PPHide *hide, unsigned id) {
    for (; hide != NULL; hide = hide->next) if (hide->id == id) return true;
    return false;
}

static const PPHide *hide_add(PP *pp, const PPHide *hide, unsigned id) {
    if (hidden(hide, id)) return hide;
    PPHide *node = cinder_arena_alloc(&pp->arena, sizeof(*node), _Alignof(PPHide));
    node->id = id;
    node->next = hide;
    return node;
}

static const PPHide *hide_union(PP *pp, const PPHide *left, const PPHide *right) {
    for (; right != NULL; right = right->next) left = hide_add(pp, left, right->id);
    return left;
}

static const PPHide *hide_intersection(PP *pp, const PPHide *left, const PPHide *right) {
    const PPHide *result = NULL;
    for (; left != NULL; left = left->next) if (hidden(right, left->id)) result = hide_add(pp, result, left->id);
    return result;
}

PPMacro *pp_find(PP *pp, PPToken token) {
    if (token.kind != PP_IDENT) return NULL;
    for (size_t i = pp->macros.len; i > 0U; --i) {
        PPMacro *macro = &pp->macros.data[i - 1U];
        if (pp_is(token, macro->name)) return macro;
    }
    return NULL;
}

static int parameter(const PPMacro *macro, PPToken token) {
    if (token.kind != PP_IDENT) return -1;
    for (size_t i = 0U; i < macro->params.len; ++i) if (pp_is(token, macro->params.data[i])) return (int)i;
    return -1;
}

static bool equivalent(const PPMacro *left, const PPMacro *right) {
    if (left->function != right->function || left->variadic != right->variadic ||
        left->params.len != right->params.len || left->replacement.len != right->replacement.len) return false;
    for (size_t i = 0U; i < left->params.len; ++i) if (strcmp(left->params.data[i], right->params.data[i]) != 0) return false;
    for (size_t i = 0U; i < left->replacement.len; ++i) {
        PPToken a = left->replacement.data[i], b = right->replacement.data[i];
        if (!pp_is(a, b.text) || (i > 0U && a.space != b.space)) return false;
    }
    return true;
}

int pp_define(PP *pp, const PPTokens *line, bool predefined) {
    if (line->len == 0U || line->data[0].kind != PP_IDENT) {
        cinder_diag(pp->diags, CINDER_ERROR, line->len == 0U ? (CinderLoc){0} : line->data[0].loc, "expected identifier after #define");
        return 1;
    }
    PPMacro macro;
    memset(&macro, 0, sizeof(macro));
    macro.name = line->data[0].text;
    macro.id = ++pp->next_macro;
    macro.definition = line->data[0].loc;
    macro.predefined = predefined;
    size_t i = 1U;
    if (i < line->len && pp_is(line->data[i], "(") && !line->data[i].space) {
        macro.function = true;
        ++i;
        if (i < line->len && !pp_is(line->data[i], ")")) {
            for (;;) {
                if (i >= line->len) break;
                PPToken token = line->data[i++];
                if (pp_is(token, "...")) {
                    const char *name = "__VA_ARGS__";
                    cinder_vec_push((CinderVec *)&macro.params, &name);
                    macro.variadic = true;
                    break;
                }
                if (token.kind != PP_IDENT || pp_is(token, "__VA_ARGS__")) {
                    cinder_diag(pp->diags, CINDER_ERROR, token.loc, "invalid macro parameter");
                    break;
                }
                for (size_t p = 0U; p < macro.params.len; ++p) {
                    if (strcmp(macro.params.data[p], token.text) == 0) cinder_diag(pp->diags, CINDER_ERROR, token.loc, "duplicate macro parameter '%s'", token.text);
                }
                cinder_vec_push((CinderVec *)&macro.params, &token.text);
                if (i < line->len && pp_is(line->data[i], ",")) { ++i; continue; }
                break;
            }
        }
        if (i >= line->len || !pp_is(line->data[i], ")")) {
            cinder_diag(pp->diags, CINDER_ERROR, macro.definition, "expected ')' in macro parameter list");
        } else ++i;
    }
    for (; i < line->len; ++i) pp_push(&macro.replacement, line->data[i]);
    for (i = 0U; i < macro.replacement.len; ++i) {
        PPToken token = macro.replacement.data[i];
        if (pp_is(token, "##") && (i == 0U || i + 1U == macro.replacement.len))
            cinder_diag(pp->diags, CINDER_ERROR, token.loc, "'##' cannot occur at an end of a macro replacement");
        if (macro.function && pp_is(token, "#") &&
            (i + 1U == macro.replacement.len || parameter(&macro, macro.replacement.data[i + 1U]) < 0))
            cinder_diag(pp->diags, CINDER_ERROR, token.loc, "'#' must be followed by a macro parameter");
        if (pp_is(token, "__VA_ARGS__") && !macro.variadic)
            cinder_diag(pp->diags, CINDER_ERROR, token.loc, "__VA_ARGS__ requires a variadic macro");
    }
    PPMacro *old = pp_find(pp, line->data[0]);
    if (old != NULL) {
        if (old->predefined && !predefined) cinder_diag(pp->diags, CINDER_ERROR, macro.definition, "cannot redefine predefined macro '%s'", macro.name);
        else if (!equivalent(old, &macro)) cinder_diag(pp->diags, CINDER_ERROR, macro.definition, "incompatible redefinition of macro '%s'", macro.name);
        free(macro.params.data);
        pp_destroy_tokens(&macro.replacement);
    } else {
        cinder_vec_push((CinderVec *)&pp->macros, &macro);
    }
    return pp->diags->errors == 0U ? 0 : 1;
}

void pp_undef(PP *pp, PPToken name) {
    PPMacro *macro = pp_find(pp, name);
    if (macro == NULL) return;
    if (macro->predefined) {
        cinder_diag(pp->diags, CINDER_ERROR, name.loc, "cannot undefine predefined macro '%s'", name.text);
        return;
    }
    size_t index = (size_t)(macro - pp->macros.data);
    free(macro->params.data);
    pp_destroy_tokens(&macro->replacement);
    memmove(&pp->macros.data[index], &pp->macros.data[index + 1U], (pp->macros.len - index - 1U) * sizeof(*macro));
    --pp->macros.len;
}

static void arguments_destroy(PPArguments *args) {
    for (size_t i = 0U; i < args->len; ++i) {
        pp_destroy_tokens(&args->data[i].raw);
        pp_destroy_tokens(&args->data[i].expanded);
    }
    free(args->data);
    memset(args, 0, sizeof(*args));
}

static void argument_push(PPArguments *args) {
    PPArgument argument;
    memset(&argument, 0, sizeof(argument));
    cinder_vec_push((CinderVec *)args, &argument);
}

static int collect_arguments(PP *pp, const PPMacro *macro, const PPTokens *work, size_t open, size_t *end, PPArguments *args) {
    unsigned nesting = 0U;
    bool closed = false;
    argument_push(args);
    size_t i = open + 1U;
    bool next_space = false;
    for (; i < work->len; ++i) {
        PPToken token = work->data[i];
        if (token.kind == PP_NEWLINE) {
            next_space = true;
            continue;
        }
        if (next_space) token.space = true;
        next_space = false;
        if (pp_is(token, ")") && nesting == 0U) { closed = true; break; }
        if (pp_is(token, "(")) ++nesting;
        else if (pp_is(token, ")")) --nesting;
        if (pp_is(token, ",") && nesting == 0U &&
            (!macro->variadic || args->len < macro->params.len)) argument_push(args);
        else pp_push(&args->data[args->len - 1U].raw, token);
    }
    if (!closed) {
        cinder_diag(pp->diags, CINDER_ERROR, work->data[open].loc, "unterminated invocation of macro '%s'", macro->name);
        return 1;
    }
    *end = i + 1U;
    if (macro->params.len == 0U && args->len == 1U && args->data[0].raw.len == 0U) args->len = 0U;
    if (args->len != macro->params.len) {
        cinder_diag(pp->diags, CINDER_ERROR, work->data[open].loc, "macro '%s' requires %zu arguments, got %zu", macro->name, macro->params.len, args->len);
        return 1;
    }
    return 0;
}

static PPToken stringify(PP *pp, const PPTokens *raw, CinderLoc loc) {
    CinderBytes text = {0};
    cinder_bytes_put8(&text, '"');
    bool pending_space = false;
    bool emitted = false;
    for (size_t i = 0U; i < raw->len; ++i) {
        PPToken token = raw->data[i];
        if (token.kind == PP_NEWLINE) { pending_space = true; continue; }
        if (emitted && (token.space || pending_space)) cinder_bytes_put8(&text, ' ');
        for (size_t c = 0U; c < token.length; ++c) {
            unsigned char byte = (unsigned char)token.text[c];
            if (token.kind == PP_LITERAL && (byte == '\\' || byte == '"')) cinder_bytes_put8(&text, '\\');
            cinder_bytes_put8(&text, byte);
        }
        pending_space = false;
        emitted = true;
    }
    cinder_bytes_put8(&text, '"');
    PPToken token = pp_token(pp, PP_LITERAL, (const char *)text.data, text.len, loc);
    free(text.data);
    return token;
}

static PPToken paste(PP *pp, PPToken left, PPToken right, CinderLoc loc) {
    if (left.kind == PP_EMPTY) return right;
    if (right.kind == PP_EMPTY) return left;
    if (left.length > SIZE_MAX - right.length - 1U) {
        cinder_diag(pp->diags, CINDER_ERROR, loc, "token paste length overflow");
        return left;
    }
    size_t length = left.length + right.length;
    char *text = cinder_alloc(length + 1U);
    memcpy(text, left.text, left.length);
    memcpy(text + left.length, right.text, right.length);
    text[length] = '\0';
    PPTokens scanned = {0};
    pp_scan(pp, text, length, CINDER_NO_FILE, 0U, &scanned);
    PPToken result = left;
    if (scanned.len != 1U || scanned.data[0].length != length || scanned.data[0].kind == PP_NEWLINE) {
        cinder_diag(pp->diags, CINDER_ERROR, loc, "token paste '%s' does not form one preprocessing token", text);
    } else {
        result = scanned.data[0];
        result.loc = loc;
        result.spelling = left.spelling;
        result.hide = hide_intersection(pp, left.hide, right.hide);
        result.space = left.space;
    }
    pp_destroy_tokens(&scanned);
    free(text);
    return result;
}

static int substitute(PP *pp, const PPMacro *macro, PPArguments *args, PPToken invocation, const PPHide *hide, PPTokens *result, unsigned depth) {
    PPTokens replaced = {0};
    for (size_t i = 0U; i < macro->replacement.len; ++i) {
        PPToken token = macro->replacement.data[i];
        if (macro->function && pp_is(token, "#") && i + 1U < macro->replacement.len) {
            int index = parameter(macro, macro->replacement.data[++i]);
            if (index < 0 || (size_t)index >= args->len) continue;
            PPToken quoted = stringify(pp, &args->data[index].raw, invocation.loc);
            quoted.definition = token.loc;
            pp_push(&replaced, quoted);
            continue;
        }
        int index = parameter(macro, token);
        if (index >= 0 && (size_t)index < args->len) {
            PPArgument *argument = &args->data[index];
            bool pasted = (i > 0U && pp_is(macro->replacement.data[i - 1U], "##")) ||
                          (i + 1U < macro->replacement.len && pp_is(macro->replacement.data[i + 1U], "##"));
            PPTokens *selected = &argument->raw;
            if (!pasted) {
                if (argument->expanded.len == 0U && argument->raw.len != 0U)
                    pp_expand(pp, &argument->raw, &argument->expanded, depth + 1U);
                selected = &argument->expanded;
            }
            if (selected->len == 0U && pasted) pp_push(&replaced, pp_token(pp, PP_EMPTY, "", 0U, invocation.loc));
            else for (size_t a = 0U; a < selected->len; ++a) {
                PPToken argument_token = selected->data[a];
                if (a == 0U) argument_token.space = token.space;
                pp_push(&replaced, argument_token);
            }
        } else {
            token.loc = invocation.loc;
            token.definition = macro->definition;
            pp_push(&replaced, token);
        }
    }
    for (size_t i = 0U; i < replaced.len; ++i) {
        PPToken token = replaced.data[i];
        if (pp_is(token, "##") && result->len > 0U && i + 1U < replaced.len) {
            PPToken left = result->data[--result->len];
            PPToken right = replaced.data[++i];
            pp_push(result, paste(pp, left, right, invocation.loc));
        } else pp_push(result, token);
    }
    size_t count = 0U;
    for (size_t i = 0U; i < result->len; ++i) {
        PPToken token = result->data[i];
        if (token.kind == PP_EMPTY) continue;
        token.hide = hide_union(pp, token.hide, hide);
        if (token.definition.file == CINDER_NO_FILE) token.definition = macro->definition;
        result->data[count++] = token;
    }
    result->len = count;
    if (count > 0U) result->data[0].space = invocation.space;
    pp_destroy_tokens(&replaced);
    return pp->diags->errors == 0U ? 0 : 1;
}

static void replace_tokens(PPTokens *work, size_t begin, size_t end, const PPTokens *replacement) {
    size_t retained = work->len - (end - begin);
    if (replacement->len > SIZE_MAX - retained) abort();
    size_t length = retained + replacement->len;
    if (length > work->cap) {
        work->data = cinder_realloc(work->data, length * sizeof(*work->data));
        work->cap = length;
    }
    memmove(work->data + begin + replacement->len, work->data + end, (work->len - end) * sizeof(*work->data));
    if (replacement->len != 0U) memcpy(work->data + begin, replacement->data, replacement->len * sizeof(*work->data));
    work->len = length;
}

static PPToken builtin_token(PP *pp, PPToken token) {
    char number[64];
    if (pp_is(token, "__LINE__")) {
        CinderLoc loc = token.loc;
        cinder_loc_linecol(pp->sources, &loc);
        int n = snprintf(number, sizeof(number), "%u", loc.line);
        return pp_token(pp, PP_NUMBER, number, n > 0 ? (size_t)n : 0U, token.loc);
    }
    if (pp_is(token, "__FILE__")) {
        const char *path = cinder_loc_name(pp->sources, token.loc);
        PPTokens filename = {0};
        PPToken literal = pp_token(pp, PP_LITERAL, path, strlen(path), token.loc);
        pp_push(&filename, literal);
        PPToken result = stringify(pp, &filename, token.loc);
        pp_destroy_tokens(&filename);
        return result;
    }
    const char *value = pp_is(token, "__DATE__") ? pp->date : pp->time;
    return pp_token(pp, PP_LITERAL, value, strlen(value), token.loc);
}

static bool pragma_operator(PP *pp, PPTokens *work, size_t begin, size_t *end, unsigned depth) {
    size_t open = begin + 1U;
    while (open < work->len && work->data[open].kind == PP_NEWLINE) ++open;
    if (open == work->len || !pp_is(work->data[open], "(")) {
        cinder_diag(pp->diags, CINDER_ERROR, work->data[begin].loc, "_Pragma requires parentheses");
        return false;
    }
    size_t close = open + 1U;
    while (close < work->len && !pp_is(work->data[close], ")")) ++close;
    if (close == work->len) {
        cinder_diag(pp->diags, CINDER_ERROR, work->data[begin].loc, "unterminated _Pragma operand");
        return false;
    }
    PPTokens operand = {work->data + open + 1U, close - open - 1U, close - open - 1U};
    PPTokens expanded = {0};
    pp_expand(pp, &operand, &expanded, depth + 1U);
    if (expanded.len != 1U || expanded.data[0].kind != PP_LITERAL || expanded.data[0].text[0] != '"') {
        cinder_diag(pp->diags, CINDER_ERROR, work->data[begin].loc, "_Pragma operand must expand to one ordinary string literal");
        pp_destroy_tokens(&expanded);
        return false;
    }
    PPToken literal = expanded.data[0];
    char *text = cinder_alloc(literal.length);
    size_t length = 0U;
    for (size_t i = 1U; i + 1U < literal.length; ++i) {
        if (literal.text[i] == '\\' && i + 2U < literal.length && (literal.text[i + 1U] == '\\' || literal.text[i + 1U] == '"')) ++i;
        text[length++] = literal.text[i];
    }
    text[length] = '\0';
    PPTokens pragma = {0};
    pp_scan(pp, text, length, work->data[begin].loc.file, work->data[begin].loc.offset, &pragma);
    pp_pragma(pp, &pragma, work->data[begin].loc);
    pp_destroy_tokens(&pragma);
    pp_destroy_tokens(&expanded);
    free(text);
    *end = close + 1U;
    return true;
}

int pp_expand(PP *pp, const PPTokens *input, PPTokens *output, unsigned depth) {
    if (depth > 128U) {
        cinder_diag(pp->diags, CINDER_ERROR, input->len == 0U ? (CinderLoc){0} : input->data[0].loc, "macro argument expansion depth exceeded");
        return 1;
    }
    PPTokens work = {0};
    pp_append(&work, input);
    size_t i = 0U;
    while (i < work.len && pp->diags->errors == 0U) {
        PPToken token = work.data[i];
        if (token.kind == PP_IDENT && pp_is(token, "_Pragma")) {
            size_t end = i;
            if (!pragma_operator(pp, &work, i, &end, depth)) break;
            i = end;
            continue;
        }
        if (token.kind == PP_IDENT && (pp_is(token, "__FILE__") || pp_is(token, "__LINE__") || pp_is(token, "__DATE__") || pp_is(token, "__TIME__"))) {
            PPToken replacement = builtin_token(pp, token);
            replacement.space = token.space;
            pp_push(output, replacement);
            ++i; continue;
        }
        PPMacro *macro = pp_find(pp, token);
        if (macro == NULL || hidden(token.hide, macro->id)) {
            pp_push(output, token);
            ++i; continue;
        }
        size_t open = i + 1U;
        while (open < work.len && work.data[open].kind == PP_NEWLINE) ++open;
        if (macro->function && (open >= work.len || !pp_is(work.data[open], "("))) {
            pp_push(output, token); ++i; continue;
        }
        if (++pp->expansion_steps > 1000000U) {
            cinder_diag(pp->diags, CINDER_ERROR, token.loc, "macro expansion work limit exceeded");
            break;
        }
        PPArguments arguments = {0};
        size_t end = i + 1U;
        const PPHide *hide = token.hide;
        if (macro->function) {
            if (collect_arguments(pp, macro, &work, open, &end, &arguments) != 0) { arguments_destroy(&arguments); break; }
            hide = hide_intersection(pp, token.hide, work.data[end - 1U].hide);
        }
        hide = hide_add(pp, hide, macro->id);
        PPTokens replacement = {0};
        substitute(pp, macro, &arguments, token, hide, &replacement, depth);
        arguments_destroy(&arguments);
        pp->produced_tokens += replacement.len;
        if (pp->produced_tokens > 4000000U || work.len > 1000000U) {
            cinder_diag(pp->diags, CINDER_ERROR, token.loc, "macro output token limit exceeded");
            pp_destroy_tokens(&replacement); break;
        }
        replace_tokens(&work, i, end, &replacement);
        pp_destroy_tokens(&replacement);
    }
    pp_destroy_tokens(&work);
    return pp->diags->errors == 0U ? 0 : 1;
}
