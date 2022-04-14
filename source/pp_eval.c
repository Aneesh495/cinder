#include "pp_private.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t bits; bool unsig; } PPValue;
typedef struct { PP *pp; const PPTokens *tokens; size_t cursor; unsigned depth; } PPExpression;

static int64_t signed_bits(uint64_t bits) {
    int64_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static PPValue boolean(bool value) {
    return (PPValue){value ? 1U : 0U, false};
}

static CinderLoc expression_loc(const PPExpression *expression) {
    if (expression->tokens->len == 0U) return (CinderLoc){0};
    size_t i = expression->cursor < expression->tokens->len ? expression->cursor : expression->tokens->len - 1U;
    return expression->tokens->data[i].loc;
}

static bool expr_take(PPExpression *expression, const char *text) {
    if (expression->cursor < expression->tokens->len && pp_is(expression->tokens->data[expression->cursor], text)) {
        ++expression->cursor;
        return true;
    }
    return false;
}

static void expected(PPExpression *expression, const char *text) {
    if (!expr_take(expression, text)) cinder_diag(expression->pp->diags, CINDER_ERROR, expression_loc(expression), "expected '%s' in preprocessing expression", text);
}

static int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

static PPValue character(PPExpression *expression, PPToken token) {
    size_t i = 0U;
    while (i < token.length && token.text[i] != '\'') ++i;
    if (i == token.length) {
        cinder_diag(expression->pp->diags, CINDER_ERROR, token.loc, "string is not permitted in a preprocessing expression");
        return boolean(false);
    }
    ++i;
    uint64_t value = i < token.length ? (unsigned char)token.text[i++] : 0U;
    if (value == '\\' && i < token.length) {
        unsigned char escape = (unsigned char)token.text[i++];
        switch (escape) {
            case 'a': value = 7U; break;
            case 'b': value = 8U; break;
            case 'f': value = 12U; break;
            case 'n': value = 10U; break;
            case 'r': value = 13U; break;
            case 't': value = 9U; break;
            case 'v': value = 11U; break;
            case '\\': case '\'': case '"': case '?': value = escape; break;
            case 'x': {
                value = 0U;
                size_t begin = i;
                while (i < token.length && hex_digit((unsigned char)token.text[i]) >= 0) {
                    value = value * 16U + (unsigned)hex_digit((unsigned char)token.text[i++]);
                    if (value > 255U) break;
                }
                if (i == begin) cinder_diag(expression->pp->diags, CINDER_ERROR, token.loc, "hex escape requires a digit");
                break;
            }
            default:
                if (escape >= '0' && escape <= '7') {
                    value = escape - '0';
                    for (unsigned n = 1U; n < 3U && i < token.length && token.text[i] >= '0' && token.text[i] <= '7'; ++n)
                        value = value * 8U + (unsigned)(token.text[i++] - '0');
                } else cinder_diag(expression->pp->diags, CINDER_ERROR, token.loc, "invalid character escape");
                break;
        }
    }
    if (i + 1U != token.length || token.text[i] != '\'' || value > 255U)
        cinder_diag(expression->pp->diags, CINDER_ERROR, token.loc, "unsupported multi-character or out-of-range character constant");
    return (PPValue){value, false};
}

static PPValue integer(PPExpression *expression, PPToken token) {
    char *text = cinder_strndup(token.text, token.length);
    char *end = NULL;
    errno = 0;
    uint64_t bits = strtoull(text, &end, 0);
    bool valid = errno == 0 && end != text;
    bool unsig = false;
    if (end != NULL && (*end == 'u' || *end == 'U')) { unsig = true; ++end; }
    if (end != NULL && (*end == 'l' || *end == 'L')) {
        char first = *end++;
        if (*end == first) ++end;
    }
    if (!unsig && end != NULL && (*end == 'u' || *end == 'U')) { unsig = true; ++end; }
    if (end == NULL || *end != '\0') valid = false;
    if (bits > INT64_MAX && !unsig) {
        if (text[0] == '0') unsig = true;
        else valid = false;
    }
    if (!valid) cinder_diag(expression->pp->diags, CINDER_ERROR, token.loc, "invalid or out-of-range preprocessing integer '%s'", text);
    free(text);
    return (PPValue){bits, unsig};
}

static PPValue conditional(PPExpression *expression, bool evaluate);

static PPValue unary(PPExpression *expression, bool evaluate) {
    if (++expression->depth > 256U) {
        cinder_diag(expression->pp->diags, CINDER_ERROR, expression_loc(expression), "preprocessing expression nesting limit exceeded");
        --expression->depth;
        return boolean(false);
    }
    PPValue result = boolean(false);
    if (expr_take(expression, "+")) result = unary(expression, evaluate);
    else if (expr_take(expression, "-")) {
        result = unary(expression, evaluate);
        if (evaluate && !result.unsig && result.bits == (UINT64_C(1) << 63U))
            cinder_diag(expression->pp->diags, CINDER_ERROR, expression_loc(expression), "signed overflow in preprocessing negation");
        result.bits = 0U - result.bits;
    } else if (expr_take(expression, "!")) result = boolean(unary(expression, evaluate).bits == 0U);
    else if (expr_take(expression, "~")) { result = unary(expression, evaluate); result.bits = ~result.bits; }
    else if (expr_take(expression, "(")) { result = conditional(expression, evaluate); expected(expression, ")"); }
    else if (expression->cursor < expression->tokens->len) {
        PPToken token = expression->tokens->data[expression->cursor++];
        if (token.kind == PP_NUMBER) result = integer(expression, token);
        else if (token.kind == PP_LITERAL) result = character(expression, token);
        else if (token.kind == PP_IDENT) result = boolean(false);
        else cinder_diag(expression->pp->diags, CINDER_ERROR, token.loc, "expected operand in preprocessing expression");
    } else cinder_diag(expression->pp->diags, CINDER_ERROR, expression_loc(expression), "missing operand in preprocessing expression");
    --expression->depth;
    return result;
}

static int priority(PPToken token) {
    if (pp_is(token, "||")) return 1;
    if (pp_is(token, "&&")) return 2;
    if (pp_is(token, "|")) return 3;
    if (pp_is(token, "^")) return 4;
    if (pp_is(token, "&")) return 5;
    if (pp_is(token, "==") || pp_is(token, "!=")) return 6;
    if (pp_is(token, "<") || pp_is(token, ">") || pp_is(token, "<=") || pp_is(token, ">=")) return 7;
    if (pp_is(token, "<<") || pp_is(token, ">>")) return 8;
    if (pp_is(token, "+") || pp_is(token, "-")) return 9;
    if (pp_is(token, "*") || pp_is(token, "/") || pp_is(token, "%")) return 10;
    return 0;
}

static void overflow(PPExpression *expression, PPToken op, bool evaluate) {
    if (evaluate) cinder_diag(expression->pp->diags, CINDER_ERROR, op.loc, "invalid signed arithmetic in preprocessing expression");
}

static PPValue apply(PPExpression *expression, PPToken op, PPValue a, PPValue b, bool evaluate) {
    bool unsig = a.unsig || b.unsig;
    uint64_t result = 0U;
    int64_t sa = signed_bits(a.bits), sb = signed_bits(b.bits);
    if (pp_is(op, "&&")) return boolean(a.bits != 0U && b.bits != 0U);
    if (pp_is(op, "||")) return boolean(a.bits != 0U || b.bits != 0U);
    if (pp_is(op, "==")) return boolean(a.bits == b.bits);
    if (pp_is(op, "!=")) return boolean(a.bits != b.bits);
    if (pp_is(op, "<")) return boolean(unsig ? a.bits < b.bits : sa < sb);
    if (pp_is(op, "<=")) return boolean(unsig ? a.bits <= b.bits : sa <= sb);
    if (pp_is(op, ">")) return boolean(unsig ? a.bits > b.bits : sa > sb);
    if (pp_is(op, ">=")) return boolean(unsig ? a.bits >= b.bits : sa >= sb);
    if (pp_is(op, "&")) result = a.bits & b.bits;
    else if (pp_is(op, "|")) result = a.bits | b.bits;
    else if (pp_is(op, "^")) result = a.bits ^ b.bits;
    else if (pp_is(op, "+")) {
        result = a.bits + b.bits;
        if (!unsig && ((~(a.bits ^ b.bits) & (a.bits ^ result)) >> 63U) != 0U) overflow(expression, op, evaluate);
    } else if (pp_is(op, "-")) {
        result = a.bits - b.bits;
        if (!unsig && (((a.bits ^ b.bits) & (a.bits ^ result)) >> 63U) != 0U) overflow(expression, op, evaluate);
    } else if (pp_is(op, "*")) {
        result = a.bits * b.bits;
        if (!unsig) {
            uint64_t ma = sa < 0 ? 0U - a.bits : a.bits;
            uint64_t mb = sb < 0 ? 0U - b.bits : b.bits;
            uint64_t limit = (sa < 0) != (sb < 0) ? UINT64_C(1) << 63U : (uint64_t)INT64_MAX;
            if (mb != 0U && ma > limit / mb) overflow(expression, op, evaluate);
        }
    } else if (pp_is(op, "/") || pp_is(op, "%")) {
        if (b.bits == 0U) {
            if (evaluate) cinder_diag(expression->pp->diags, CINDER_ERROR, op.loc, "division by zero in preprocessing expression");
        } else if (!unsig && sa == INT64_MIN && sb == -1) overflow(expression, op, evaluate);
        else if (unsig) result = pp_is(op, "/") ? a.bits / b.bits : a.bits % b.bits;
        else result = (uint64_t)(pp_is(op, "/") ? sa / sb : sa % sb);
    } else if (pp_is(op, "<<") || pp_is(op, ">>")) {
        unsig = a.unsig;
        if (b.bits >= 64U) {
            if (evaluate) cinder_diag(expression->pp->diags, CINDER_ERROR, op.loc, "invalid shift count in preprocessing expression");
        } else if (pp_is(op, "<<")) {
            result = a.bits << b.bits;
            if (!a.unsig && (sa < 0 || a.bits > ((uint64_t)INT64_MAX >> b.bits))) overflow(expression, op, evaluate);
        } else {
            result = a.bits >> b.bits;
            if (!a.unsig && sa < 0 && b.bits != 0U) result |= UINT64_MAX << (64U - b.bits);
        }
    }
    return (PPValue){result, unsig};
}

static PPValue binary(PPExpression *expression, int minimum, bool evaluate) {
    PPValue left = unary(expression, evaluate);
    while (expression->cursor < expression->tokens->len && expression->pp->diags->errors == 0U) {
        PPToken op = expression->tokens->data[expression->cursor];
        int precedence = priority(op);
        if (precedence < minimum) break;
        ++expression->cursor;
        bool right_evaluated = evaluate;
        if (pp_is(op, "&&") && left.bits == 0U) right_evaluated = false;
        if (pp_is(op, "||") && left.bits != 0U) right_evaluated = false;
        PPValue right = binary(expression, precedence + 1, right_evaluated);
        left = apply(expression, op, left, right, evaluate);
    }
    return left;
}

static PPValue conditional(PPExpression *expression, bool evaluate) {
    PPValue condition = binary(expression, 1, evaluate);
    if (!expr_take(expression, "?")) return condition;
    if (++expression->depth > 256U) {
        cinder_diag(expression->pp->diags, CINDER_ERROR, expression_loc(expression), "conditional expression nesting limit exceeded");
        --expression->depth;
        return boolean(false);
    }
    PPValue yes = conditional(expression, evaluate && condition.bits != 0U);
    expected(expression, ":");
    PPValue no = conditional(expression, evaluate && condition.bits == 0U);
    --expression->depth;
    PPValue result = condition.bits != 0U ? yes : no;
    result.unsig = yes.unsig || no.unsig;
    return result;
}

static bool is_defined(PP *pp, PPToken token) {
    return pp_find(pp, token) != NULL || pp_is(token, "__FILE__") || pp_is(token, "__LINE__") || pp_is(token, "__DATE__") || pp_is(token, "__TIME__");
}

int pp_evaluate(PP *pp, const PPTokens *tokens, bool *result) {
    PPTokens protected_tokens = {0}, expanded = {0};
    for (size_t i = 0U; i < tokens->len; ++i) {
        PPToken token = tokens->data[i];
        if (!pp_is(token, "defined")) { pp_push(&protected_tokens, token); continue; }
        bool parens = i + 1U < tokens->len && pp_is(tokens->data[i + 1U], "(");
        if (parens) ++i;
        if (++i >= tokens->len || tokens->data[i].kind != PP_IDENT) {
            cinder_diag(pp->diags, CINDER_ERROR, token.loc, "defined requires a macro identifier");
            break;
        }
        bool defined = is_defined(pp, tokens->data[i]);
        if (parens && (++i >= tokens->len || !pp_is(tokens->data[i], ")"))) {
            cinder_diag(pp->diags, CINDER_ERROR, token.loc, "expected ')' after defined operand");
            break;
        }
        pp_push(&protected_tokens, pp_token(pp, PP_NUMBER, defined ? "1" : "0", 1U, token.loc));
    }
    pp_expand(pp, &protected_tokens, &expanded, 0U);
    PPExpression expression = {pp, &expanded, 0U, 0U};
    PPValue value = conditional(&expression, true);
    if (expression.cursor != expanded.len) cinder_diag(pp->diags, CINDER_ERROR, expression_loc(&expression), "trailing tokens in preprocessing expression");
    *result = value.bits != 0U;
    pp_destroy_tokens(&protected_tokens);
    pp_destroy_tokens(&expanded);
    return pp->diags->errors == 0U ? 0 : 1;
}
