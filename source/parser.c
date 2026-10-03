#include "cinder.h"

#include <stdlib.h>
#include <string.h>

static CinderToken *peek(CinderAst *ast) { size_t i = ast->cursor < ast->tokens->tokens.len ? ast->cursor : ast->tokens->tokens.len - 1U; return &ast->tokens->tokens.data[i]; }
static CinderToken *previous(CinderAst *ast) { return &ast->tokens->tokens.data[ast->cursor - 1U]; }
static bool is(CinderAst *ast, CinderTokenKind kind) { return peek(ast)->kind == kind; }
static bool take(CinderAst *ast, CinderTokenKind kind) { if (!is(ast, kind)) return false; ast->cursor++; return true; }
static CinderToken *expect(CinderAst *ast, CinderTokenKind kind, const char *what) {
    if (is(ast, kind)) return &ast->tokens->tokens.data[ast->cursor++];
    cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "expected %s, found %s", what, cinder_token_name(peek(ast)->kind));
    return NULL;
}

static void *node_alloc(CinderAst *ast, size_t size) { return cinder_arena_alloc(&ast->arena, size, _Alignof(max_align_t)); }
static CinderExpr *new_expr(CinderAst *ast, CinderExprKind kind, CinderLoc loc) { CinderExpr *expr = node_alloc(ast, sizeof(*expr)); memset(expr, 0, sizeof(*expr)); expr->kind = kind; expr->loc = loc; return expr; }
static CinderStmt *new_stmt(CinderAst *ast, CinderStmtKind kind, CinderLoc loc) { CinderStmt *stmt = node_alloc(ast, sizeof(*stmt)); memset(stmt, 0, sizeof(*stmt)); stmt->kind = kind; stmt->loc = loc; return stmt; }
static CinderDecl *new_decl(CinderAst *ast, CinderDeclKind kind, CinderLoc loc) { CinderDecl *decl = node_alloc(ast, sizeof(*decl)); memset(decl, 0, sizeof(*decl)); decl->kind = kind; decl->loc = loc; decl->params.data = NULL; decl->params.len = 0U; decl->params.cap = 0U; return decl; }

static bool is_type_start(CinderTokenKind kind) {
    return kind == TOK_KW_CONST || kind == TOK_KW_VOLATILE || kind == TOK_KW_RESTRICT || kind == TOK_KW_VOID || kind == TOK_KW_CHAR || kind == TOK_KW_SHORT || kind == TOK_KW_INT || kind == TOK_KW_LONG || kind == TOK_KW_SIGNED || kind == TOK_KW_UNSIGNED || kind == TOK_KW_FLOAT || kind == TOK_KW_DOUBLE || kind == TOK_KW__BOOL || kind == TOK_KW_STRUCT || kind == TOK_KW_UNION || kind == TOK_KW_ENUM;
}

static CinderType *parse_type_specifier(CinderAst *ast);
static CinderType *parse_declarator(CinderAst *ast, CinderType *base, char **name, CinderLoc *name_loc, CinderDecl **function_decl);

static CinderType *parse_aggregate_specifier(CinderAst *ast, bool is_union) {
    CinderType *type = cinder_type_new(ast->types, is_union ? TYPE_UNION : TYPE_STRUCT);
    CinderToken *keyword = &ast->tokens->tokens.data[ast->cursor - 1U];
    type->tag = NULL;
    if (is(ast, TOK_IDENTIFIER)) {
        CinderToken *tag = &ast->tokens->tokens.data[ast->cursor++];
        type->tag = cinder_arena_strndup(&ast->arena, tag->text, tag->length);
    }
    if (!take(ast, '{')) return type;
    while (!is(ast, TOK_EOF) && !is(ast, '}')) {
        CinderType *field_base = parse_type_specifier(ast);
        char *field_name = NULL;
        CinderLoc field_loc = peek(ast)->loc;
        CinderType *field_type = parse_declarator(ast, field_base, &field_name, &field_loc, NULL);
        CinderField field;
        field.name = field_name;
        field.type = field_type;
        field.offset = 0U;
        field.bit_offset = 0U;
        field.bit_width = 0U;
        cinder_vec_push((CinderVec *)&type->fields, &field);
        (void)expect(ast, ';', "';'");
    }
    (void)expect(ast, '}', "'}'");
    (void)keyword;
    (void)cinder_type_layout_aggregate(type, ast->diags, type->tag == NULL ? cinder_loc(CINDER_NO_FILE, 0U, 0U) : peek(ast)->loc);
    return type;
}

static CinderType *parse_type_specifier(CinderAst *ast) {
    CinderTypeContext *types = ast->types;
    unsigned qualifiers = 0U, longs = 0U;
    int sign = 0;
    bool short_spec = false, int_spec = false;
    CinderTokenKind scalar = 0;
    CinderType *aggregate = NULL;
    bool consumed = false;
    while (true) {
        CinderTokenKind kind = peek(ast)->kind;
        if (kind == TOK_KW_CONST || kind == TOK_KW_VOLATILE || kind == TOK_KW_RESTRICT) {
            qualifiers |= kind == TOK_KW_CONST ? 1U : kind == TOK_KW_VOLATILE ? 2U : 4U;
            ++ast->cursor;
        } else if (kind == TOK_KW_SIGNED || kind == TOK_KW_UNSIGNED) {
            if (sign != 0) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "duplicate or conflicting signedness specifier");
            sign = kind == TOK_KW_UNSIGNED ? 1 : -1; ++ast->cursor;
        } else if (kind == TOK_KW_LONG) { ++longs; ++ast->cursor; }
        else if (kind == TOK_KW_SHORT) {
            if (short_spec) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "duplicate short specifier");
            short_spec = true; ++ast->cursor;
        } else if (kind == TOK_KW_INT) {
            if (int_spec) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "duplicate int specifier");
            int_spec = true; ++ast->cursor;
        } else if (kind == TOK_KW_CHAR || kind == TOK_KW_VOID || kind == TOK_KW__BOOL || kind == TOK_KW_FLOAT || kind == TOK_KW_DOUBLE) {
            if (scalar != 0) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "conflicting type specifiers");
            scalar = kind; ++ast->cursor;
        } else if (kind == TOK_KW_STRUCT || kind == TOK_KW_UNION) {
            if (aggregate != NULL) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "conflicting aggregate specifiers");
            ++ast->cursor; aggregate = parse_aggregate_specifier(ast, kind == TOK_KW_UNION);
        } else break;
        consumed = true;
    }
    if (!consumed || (scalar == 0 && aggregate == NULL && sign == 0 && longs == 0U && !short_spec && !int_spec)) {
        cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "expected a type specifier"); return types->error_type;
    }
    if (longs > 2U || (short_spec && longs != 0U) || (scalar != 0 && (short_spec || int_spec || longs != 0U)) || (aggregate != NULL && (scalar != 0 || sign != 0 || short_spec || int_spec || longs != 0U)) || (scalar != 0 && scalar != TOK_KW_CHAR && sign != 0)) {
        cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "invalid combination of type specifiers"); return types->error_type;
    }
    CinderType *type = aggregate;
    if (type == NULL) {
        if (scalar == TOK_KW_VOID) type = types->void_type;
        else if (scalar == TOK_KW__BOOL) type = types->bool_type;
        else if (scalar == TOK_KW_FLOAT) type = types->float_type;
        else if (scalar == TOK_KW_DOUBLE) type = types->double_type;
        else if (scalar == TOK_KW_CHAR) type = sign == 0 ? types->char_type : sign > 0 ? types->uchar_type : types->schar_type;
        else if (short_spec) type = sign > 0 ? types->ushort_type : types->short_type;
        else if (longs == 2U) type = sign > 0 ? types->ullong_type : types->llong_type;
        else if (longs == 1U) type = sign > 0 ? types->ulong_type : types->long_type;
        else type = sign > 0 ? types->uint_type : types->int_type;
    }
    if ((qualifiers & 4U) != 0U && type->kind != TYPE_POINTER) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "restrict requires an object pointer type");
    return cinder_type_qualified(types, type, qualifiers);
}

static CinderType *parse_type_name(CinderAst *ast) {
    CinderType *type = parse_type_specifier(ast);
    while (take(ast, '*')) type = cinder_type_pointer(ast->types, type);
    while (take(ast, '[')) {
        size_t count = 0U;
        if (is(ast, TOK_NUMBER) && !peek(ast)->is_floating && peek(ast)->integer >= 0) { count = (size_t)peek(ast)->integer; ++ast->cursor; }
        else cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "type-name array requires a constant nonnegative bound");
        (void)expect(ast, ']', "']'");
        type = cinder_type_array(ast->types, type, count);
    }
    return type;
}

static CinderType *parse_declarator(CinderAst *ast, CinderType *base, char **name, CinderLoc *name_loc, CinderDecl **function_decl) {
    while (take(ast, '*')) {
        base = cinder_type_pointer(ast->types, base);
        unsigned qualifiers = 0U;
        while (is(ast, TOK_KW_CONST) || is(ast, TOK_KW_VOLATILE) || is(ast, TOK_KW_RESTRICT)) {
            CinderTokenKind q = peek(ast)->kind; ++ast->cursor;
            qualifiers |= q == TOK_KW_CONST ? 1U : q == TOK_KW_VOLATILE ? 2U : 4U;
        }
        base = cinder_type_qualified(ast->types, base, qualifiers);
    }
    if (!is(ast, TOK_IDENTIFIER)) {
        cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "expected an identifier in declarator");
        return base;
    }
    CinderToken *identifier = &ast->tokens->tokens.data[ast->cursor++];
    *name = cinder_arena_strndup(&ast->arena, identifier->text, identifier->length);
    *name_loc = identifier->loc;
    bool variadic = false;
    if (take(ast, '(')) {
        CinderParamVec params = {NULL, 0U, 0U};
        if (!is(ast, ')')) {
            if (is(ast, TOK_KW_VOID) && ast->cursor + 1U < ast->tokens->tokens.len && ast->tokens->tokens.data[ast->cursor + 1U].kind == ')') {
                ast->cursor++;
            } else while (true) {
                if (take(ast, TOK_ELLIPSIS)) { variadic = true; break; }
                CinderType *param_base = parse_type_specifier(ast);
                char *param_name = NULL;
                CinderLoc param_loc;
                CinderType *param_type = parse_declarator(ast, param_base, &param_name, &param_loc, NULL);
                CinderParam param = {param_name, param_type};
                cinder_vec_push((CinderVec *)&params, &param);
                if (!take(ast, ',')) break;
            }
        }
        (void)expect(ast, ')', "')'");
        base = cinder_type_function(ast->types, base, &params);
        base->variadic = variadic;
        free(params.data);
        if (function_decl != NULL) {
            CinderDecl *ignored = NULL;
            (void)ignored;
        }
    }
    while (take(ast, '[')) {
        size_t length = 0U;
        if (is(ast, TOK_NUMBER)) length = (size_t)peek(ast)->integer, ast->cursor++;
        (void)expect(ast, ']', "']'");
        base = cinder_type_array(ast->types, base, length);
    }
    return base;
}

static CinderExpr *parse_expression(CinderAst *ast);
static CinderExpr *parse_assignment(CinderAst *ast);

static CinderExpr *parse_primary(CinderAst *ast) {
    CinderToken *token = peek(ast);
    if (take(ast, TOK_NUMBER)) {
        CinderExpr *expr = new_expr(ast, token->is_floating ? EX_FLOAT : EX_INT, token->loc);
        if (token->is_floating) { expr->as.floating = token->floating; expr->type = token->number_float32 ? ast->types->float_type : ast->types->double_type; }
        else {
            expr->as.integer = token->integer;
            if (token->number_rank == 0U) expr->type = token->number_unsigned ? ast->types->uint_type : ast->types->int_type;
            else if (token->number_rank == 1U) expr->type = token->number_unsigned ? ast->types->ulong_type : ast->types->long_type;
            else expr->type = token->number_unsigned ? ast->types->ullong_type : ast->types->llong_type;
        }
        return expr;
    }
    if (take(ast, TOK_CHAR)) {
        CinderExpr *expr = new_expr(ast, EX_CHAR, token->loc); expr->as.integer = token->length >= 3U ? (unsigned char)token->text[1] : 0; return expr;
    }
    if (take(ast, TOK_STRING)) {
        CinderExpr *expr = new_expr(ast, EX_STRING, token->loc); expr->as.string = cinder_arena_strndup(&ast->arena, token->text, token->length); return expr;
    }
    if (take(ast, TOK_IDENTIFIER)) {
        CinderExpr *expr = new_expr(ast, EX_NAME, token->loc); expr->as.name = cinder_arena_strndup(&ast->arena, token->text, token->length); return expr;
    }
    if (take(ast, '(')) {
        CinderExpr *expr = parse_expression(ast);
        (void)expect(ast, ')', "')'");
        return expr;
    }
    cinder_diag(ast->diags, CINDER_ERROR, token->loc, "expected an expression, found %s", cinder_token_name(token->kind));
    ast->cursor++;
    return new_expr(ast, EX_INT, token->loc);
}

static CinderExpr *parse_postfix(CinderAst *ast) {
    CinderExpr *expr = parse_primary(ast);
    while (true) {
        if (take(ast, '(')) {
            CinderExpr *call = new_expr(ast, EX_CALL, expr->loc);
            call->as.call.callee = expr;
            call->as.call.args.data = NULL; call->as.call.args.len = 0U; call->as.call.args.cap = 0U;
            if (expr->kind == EX_NAME && strcmp(expr->as.name, "va_arg") == 0) {
                CinderExpr *list = parse_assignment(ast);
                (void)expect(ast, ',', "','");
                CinderType *argument_type = parse_type_specifier(ast);
                (void)expect(ast, ')', "')'");
                CinderExpr *va = new_expr(ast, EX_VA_ARG, expr->loc); va->as.va_arg.list = list; va->as.va_arg.type = argument_type; expr = va; continue;
            }
            if (!is(ast, ')')) {
                do {
                    CinderExpr *arg = parse_assignment(ast);
                    cinder_vec_push((CinderVec *)&call->as.call.args, &arg);
                } while (take(ast, ','));
            }
            (void)expect(ast, ')', "')'");
            expr = call;
        } else if (take(ast, TOK_PLUSPLUS)) {
            CinderExpr *unary = new_expr(ast, EX_UNARY, expr->loc); unary->as.unary.op = TOK_PLUSPLUS; unary->as.unary.value = expr; unary->as.unary.postfix = true; expr = unary;
        } else if (take(ast, TOK_MINUSMINUS)) {
            CinderExpr *unary = new_expr(ast, EX_UNARY, expr->loc); unary->as.unary.op = TOK_MINUSMINUS; unary->as.unary.value = expr; unary->as.unary.postfix = true; expr = unary;
        } else break;
    }
    return expr;
}

static CinderExpr *parse_unary(CinderAst *ast) {
    CinderTokenKind kind = peek(ast)->kind;
    if (kind == '+' || kind == '-' || kind == '!' || kind == '~' || kind == '&' || kind == '*' || kind == TOK_PLUSPLUS || kind == TOK_MINUSMINUS) {
        CinderToken *token = &ast->tokens->tokens.data[ast->cursor++];
        CinderExpr *expr = new_expr(ast, EX_UNARY, token->loc); expr->as.unary.op = (int)token->kind; expr->as.unary.value = parse_unary(ast); return expr;
    }
    if (kind == TOK_KW_SIZEOF || kind == TOK_KW_ALIGNOF) {
        ++ast->cursor;
        CinderExpr *expr = new_expr(ast, kind == TOK_KW_SIZEOF ? EX_SIZEOF : EX_ALIGNOF, previous(ast)->loc);
        if (is(ast, '(') && ast->cursor + 1U < ast->tokens->tokens.len && is_type_start(ast->tokens->tokens.data[ast->cursor + 1U].kind)) {
            ++ast->cursor; expr->queried_type = parse_type_name(ast); (void)expect(ast, ')', "')'");
        } else if (kind == TOK_KW_ALIGNOF) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "_Alignof requires a type name");
        else expr->as.unary.value = parse_unary(ast);
        return expr;
    }
    if (is(ast, '(') && ast->cursor + 1U < ast->tokens->tokens.len && is_type_start(ast->tokens->tokens.data[ast->cursor + 1U].kind)) {
        ++ast->cursor;
        CinderLoc loc = previous(ast)->loc;
        CinderType *type = parse_type_name(ast); (void)expect(ast, ')', "')'");
        CinderExpr *expr = new_expr(ast, EX_CAST, loc); expr->as.cast.cast_type = type;
        expr->as.cast.value = parse_unary(ast); return expr;
    }
    return parse_postfix(ast);
}

static int precedence(int kind) {
    switch (kind) {
        case TOK_OROR: return 1;
        case TOK_ANDAND: return 2;
        case '|': return 3;
        case '^': return 4;
        case '&': return 5;
        case TOK_EQEQ: case TOK_NEQ: return 6;
        case '<': case '>': case TOK_LE: case TOK_GE: return 7;
        case TOK_SHL: case TOK_SHR: return 8;
        case '+': case '-': return 9;
        case '*': case '/': case '%': return 10;
        default: return 0;
    }
}

static CinderExpr *parse_binary(CinderAst *ast, int minimum) {
    CinderExpr *left = parse_unary(ast);
    while (true) {
        int priority = precedence((int)peek(ast)->kind);
        if (priority < minimum || priority == 0) break;
        CinderToken *operator_token = &ast->tokens->tokens.data[ast->cursor++];
        CinderExpr *right = parse_binary(ast, priority + 1);
        CinderExpr *binary = new_expr(ast, EX_BINARY, operator_token->loc);
        binary->as.binary.op = (int)operator_token->kind;
        binary->as.binary.left = left;
        binary->as.binary.right = right;
        left = binary;
    }
    return left;
}

static CinderExpr *parse_conditional(CinderAst *ast) {
    CinderExpr *condition = parse_binary(ast, 1);
    if (!take(ast, '?')) return condition;
    CinderExpr *expr = new_expr(ast, EX_CONDITIONAL, condition->loc);
    expr->as.conditional.condition = condition;
    expr->as.conditional.yes = parse_expression(ast);
    (void)expect(ast, ':', "':'");
    expr->as.conditional.no = parse_conditional(ast);
    return expr;
}

static CinderExpr *parse_assignment(CinderAst *ast) {
    CinderExpr *left = parse_conditional(ast);
    CinderTokenKind kind = peek(ast)->kind;
    if (kind == '=' || kind == TOK_PLUSEQ || kind == TOK_MINUSEQ || kind == TOK_STAREQ || kind == TOK_SLASHEQ || kind == TOK_PERCENTEQ || kind == TOK_ANDEQ || kind == TOK_OREQ || kind == TOK_XOREQ || kind == TOK_LSHIFT_EQ || kind == TOK_RSHIFT_EQ) {
        CinderToken *operator_token = &ast->tokens->tokens.data[ast->cursor++];
        CinderExpr *right = parse_assignment(ast);
        CinderExpr *assignment = new_expr(ast, EX_ASSIGN, operator_token->loc);
        assignment->as.assign.target = left; assignment->as.assign.value = right; assignment->as.assign.op = (int)operator_token->kind;
        return assignment;
    }
    return left;
}

static CinderExpr *parse_expression(CinderAst *ast) {
    CinderExpr *expr = parse_assignment(ast);
    while (take(ast, ',')) {
        CinderExpr *comma = new_expr(ast, EX_BINARY, previous(ast)->loc);
        comma->as.binary.op = ',';
        comma->as.binary.left = expr;
        comma->as.binary.right = parse_assignment(ast);
        expr = comma;
    }
    return expr;
}

static CinderStmt *parse_statement(CinderAst *ast);

static CinderStmt *parse_compound(CinderAst *ast) {
    CinderLoc loc = peek(ast)->loc;
    (void)expect(ast, '{', "'{'");
    CinderStmt *stmt = new_stmt(ast, ST_BLOCK, loc);
    stmt->as.block.items.data = NULL; stmt->as.block.items.len = 0U; stmt->as.block.items.cap = 0U;
    while (!is(ast, TOK_EOF) && !is(ast, '}')) {
        CinderStmt *item = parse_statement(ast);
        if (item != NULL) cinder_vec_push((CinderVec *)&stmt->as.block.items, &item);
    }
    (void)expect(ast, '}', "'}'");
    return stmt;
}

static CinderDecl *parse_local_decl(CinderAst *ast) {
    CinderLoc loc = peek(ast)->loc;
    CinderType *base = parse_type_specifier(ast);
    char *name = NULL; CinderLoc name_loc;
    CinderType *type = parse_declarator(ast, base, &name, &name_loc, NULL);
    CinderDecl *decl = new_decl(ast, DECL_VAR, loc); decl->name = name; decl->loc = name_loc; decl->type = type;
    if (take(ast, '=')) decl->initializer = parse_assignment(ast);
    (void)expect(ast, ';', "';'");
    return decl;
}

static CinderStmt *parse_statement(CinderAst *ast) {
    CinderToken *token = peek(ast);
    if (is(ast, '{')) return parse_compound(ast);
    if (take(ast, ';')) return new_stmt(ast, ST_EMPTY, token->loc);
    if (is_type_start(token->kind)) {
        CinderStmt *stmt = new_stmt(ast, ST_DECL, token->loc); stmt->as.decl = parse_local_decl(ast); return stmt;
    }
    if (take(ast, TOK_KW_RETURN)) {
        CinderStmt *stmt = new_stmt(ast, ST_RETURN, token->loc);
        stmt->as.ret.value = is(ast, ';') ? NULL : parse_expression(ast);
        (void)expect(ast, ';', "';'"); return stmt;
    }
    if (take(ast, TOK_KW_IF)) {
        CinderStmt *stmt = new_stmt(ast, ST_IF, token->loc); (void)expect(ast, '(', "'('"); stmt->as.if_stmt.condition = parse_expression(ast); (void)expect(ast, ')', "')'"); stmt->as.if_stmt.then_branch = parse_statement(ast); stmt->as.if_stmt.else_branch = take(ast, TOK_KW_ELSE) ? parse_statement(ast) : NULL; return stmt;
    }
    if (take(ast, TOK_KW_WHILE)) {
        CinderStmt *stmt = new_stmt(ast, ST_WHILE, token->loc); (void)expect(ast, '(', "'('"); stmt->as.loop.condition = parse_expression(ast); (void)expect(ast, ')', "')'"); stmt->as.loop.body = parse_statement(ast); return stmt;
    }
    if (take(ast, TOK_KW_DO)) {
        CinderStmt *stmt = new_stmt(ast, ST_DO, token->loc);
        stmt->as.loop.body = parse_statement(ast);
        (void)expect(ast, TOK_KW_WHILE, "'while'");
        (void)expect(ast, '(', "'('");
        stmt->as.loop.condition = parse_expression(ast);
        (void)expect(ast, ')', "')'");
        (void)expect(ast, ';', "';'");
        return stmt;
    }
    if (take(ast, TOK_KW_FOR)) {
        CinderStmt *stmt = new_stmt(ast, ST_FOR, token->loc); (void)expect(ast, '(', "'('");
        if (is_type_start(peek(ast)->kind)) stmt->as.for_stmt.init = new_stmt(ast, ST_DECL, peek(ast)->loc), stmt->as.for_stmt.init->as.decl = parse_local_decl(ast);
        else if (!is(ast, ';')) { stmt->as.for_stmt.init = new_stmt(ast, ST_EXPR, peek(ast)->loc); stmt->as.for_stmt.init->as.expr = parse_expression(ast); (void)expect(ast, ';', "';'"); }
        else { take(ast, ';'); stmt->as.for_stmt.init = NULL; }
        stmt->as.for_stmt.condition = is(ast, ';') ? NULL : parse_expression(ast); (void)expect(ast, ';', "';'");
        stmt->as.for_stmt.step = is(ast, ')') ? NULL : parse_expression(ast); (void)expect(ast, ')', "')'"); stmt->as.for_stmt.body = parse_statement(ast); return stmt;
    }
    if (take(ast, TOK_KW_BREAK)) { CinderStmt *stmt = new_stmt(ast, ST_BREAK, token->loc); (void)expect(ast, ';', "';'"); return stmt; }
    if (take(ast, TOK_KW_CONTINUE)) { CinderStmt *stmt = new_stmt(ast, ST_CONTINUE, token->loc); (void)expect(ast, ';', "';'"); return stmt; }
    CinderStmt *stmt = new_stmt(ast, ST_EXPR, token->loc); stmt->as.expr = is(ast, ';') ? NULL : parse_expression(ast); (void)expect(ast, ';', "';'"); return stmt;
}

int cinder_parse(CinderAst *ast) {
    while (!is(ast, TOK_EOF)) {
        bool is_static = take(ast, TOK_KW_STATIC);
        bool is_extern = take(ast, TOK_KW_EXTERN);
        if (!is_type_start(peek(ast)->kind)) {
            cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "expected a declaration");
            ast->cursor++;
            continue;
        }
        CinderLoc loc = peek(ast)->loc;
        CinderType *base = parse_type_specifier(ast);
        if ((base->kind == TYPE_STRUCT || base->kind == TYPE_UNION) && is(ast, ';')) { ast->cursor++; continue; }
        char *name = NULL; CinderLoc name_loc;
        CinderType *type = parse_declarator(ast, base, &name, &name_loc, NULL);
        CinderDecl *decl = new_decl(ast, type->kind == TYPE_FUNCTION ? DECL_FUNCTION : DECL_VAR, loc);
        decl->name = name; decl->loc = name_loc; decl->type = type; decl->is_static = is_static; decl->is_extern = is_extern;
        if (type->kind == TYPE_FUNCTION) {
            for (size_t i = 0U; i < type->params.len; ++i) {
                CinderDecl *param = new_decl(ast, DECL_VAR, name_loc); param->name = type->params.data[i].name; param->type = type->params.data[i].type; cinder_vec_push((CinderVec *)&decl->params, &param);
            }
            if (is(ast, '{')) { decl->is_definition = true; decl->body = parse_compound(ast); }
            else (void)expect(ast, ';', "';'");
        } else {
            if (take(ast, '=')) decl->initializer = parse_assignment(ast);
            (void)expect(ast, ';', "';'");
        }
        cinder_vec_push((CinderVec *)&ast->declarations, &decl);
    }
    return ast->diags->errors == 0U ? 0 : 1;
}
