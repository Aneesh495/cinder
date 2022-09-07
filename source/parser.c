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
static CinderExpr *new_expr(CinderAst *ast, CinderExprKind kind, CinderLoc loc) { CinderExpr *expr = node_alloc(ast, sizeof(*expr)); memset(expr, 0, sizeof(*expr)); expr->kind = kind; expr->loc = loc; expr->parse_index = ast->cursor; return expr; }
static CinderStmt *new_stmt(CinderAst *ast, CinderStmtKind kind, CinderLoc loc) { CinderStmt *stmt = node_alloc(ast, sizeof(*stmt)); memset(stmt, 0, sizeof(*stmt)); stmt->kind = kind; stmt->loc = loc; stmt->control_scope = -1; return stmt; }
static CinderDecl *new_decl(CinderAst *ast, CinderDeclKind kind, CinderLoc loc) { CinderDecl *decl = node_alloc(ast, sizeof(*decl)); memset(decl, 0, sizeof(*decl)); decl->kind = kind; decl->loc = loc; decl->parse_index = ast->cursor; decl->lowering_slot = -1; decl->params.data = NULL; decl->params.len = 0U; decl->params.cap = 0U; return decl; }

static CinderParseBinding *binding_token(CinderAst *ast, const CinderToken *token, bool tag) {
    if (token->kind != TOK_IDENTIFIER) return NULL;
    for (size_t i = ast->bindings.len; i > 0U; --i) {
        CinderParseBinding *binding = &ast->bindings.data[i - 1U];
        if ((binding->kind == PARSE_TAG) == tag && strlen(binding->name) == token->length && memcmp(binding->name, token->text, token->length) == 0) return binding;
    }
    return NULL;
}

static CinderParseBinding *bind_name(CinderAst *ast, char *name, CinderType *type, CinderParseBindingKind kind, int64_t value, CinderLoc loc) {
    if (name == NULL) return NULL;
    for (size_t i = ast->bindings.len; i > 0U; --i) {
        CinderParseBinding *old = &ast->bindings.data[i - 1U];
        if (old->scope != ast->scope_depth || (old->kind == PARSE_TAG) != (kind == PARSE_TAG) || strcmp(name, old->name) != 0) continue;
        if (kind != PARSE_TAG && (kind == PARSE_ENUMERATOR || old->kind == PARSE_ENUMERATOR || old->kind != kind || (kind == PARSE_TYPEDEF && !cinder_type_equal(old->type, type)))) cinder_diag(ast->diags, CINDER_ERROR, loc, "conflicting declaration of '%s'", name);
        break;
    }
    CinderParseBinding binding = {name, type, kind, value, ast->scope_depth, NULL};
    cinder_vec_push((CinderVec *)&ast->bindings, &binding);
    return &ast->bindings.data[ast->bindings.len - 1U];
}

static CinderType *local_linkage_type(CinderAst *ast, const char *name, CinderType *type) {
    if (name == NULL) return type;
    for (size_t i = ast->bindings.len; i > 0U; --i) {
        const CinderParseBinding *binding = &ast->bindings.data[i - 1U];
        if (binding->kind == PARSE_TAG || strcmp(binding->name, name) != 0) continue;
        bool linkage = binding->kind == PARSE_OBJECT && (binding->scope == 0U || (binding->decl != NULL && (binding->decl->is_extern || binding->decl->kind == DECL_FUNCTION)));
        return linkage && cinder_type_compatible(type, binding->type) ? cinder_type_composite(ast->types, type, binding->type) : type;
    }
    return type;
}

static CinderType *file_composite_type(CinderAst *ast, const char *name, CinderType *type) {
    if (name == NULL || ast->scope_depth != 0U) return type;
    for (size_t i = ast->bindings.len; i > 0U; --i) {
        const CinderParseBinding *binding = &ast->bindings.data[i - 1U];
        if (binding->scope == 0U && binding->kind == PARSE_OBJECT && strcmp(binding->name, name) == 0) {
            return cinder_type_compatible(type, binding->type) ? cinder_type_composite(ast->types, type, binding->type) : type;
        }
    }
    return type;
}

static bool type_keyword(CinderTokenKind kind) {
    return kind == TOK_KW_CONST || kind == TOK_KW_VOLATILE || kind == TOK_KW_RESTRICT || kind == TOK_KW_VOID || kind == TOK_KW_CHAR || kind == TOK_KW_SHORT || kind == TOK_KW_INT || kind == TOK_KW_LONG || kind == TOK_KW_SIGNED || kind == TOK_KW_UNSIGNED || kind == TOK_KW_FLOAT || kind == TOK_KW_DOUBLE || kind == TOK_KW__BOOL || kind == TOK_KW_STRUCT || kind == TOK_KW_UNION || kind == TOK_KW_ENUM;
}

static bool is_type_start(CinderAst *ast, const CinderToken *token) {
    CinderParseBinding *binding = binding_token(ast, token, false);
    return type_keyword(token->kind) || (binding != NULL && binding->kind == PARSE_TYPEDEF);
}

typedef struct {
    unsigned storage;
    unsigned function_specifiers;
    size_t alignment;
    bool alignment_specified;
} DeclarationSpec;

enum { STORAGE_TYPEDEF = 1U, STORAGE_STATIC = 2U, STORAGE_EXTERN = 4U, STORAGE_AUTO = 8U, STORAGE_REGISTER = 16U };

static bool declaration_start(CinderAst *ast, const CinderToken *token) {
    CinderTokenKind kind = token->kind;
    return is_type_start(ast, token) || kind == TOK_KW_ALIGNAS || kind == TOK_KW_TYPEDEF || kind == TOK_KW_STATIC || kind == TOK_KW_EXTERN || kind == TOK_KW_AUTO || kind == TOK_KW_REGISTER || kind == TOK_KW_INLINE || kind == TOK_KW_NORETURN;
}

static CinderType *parse_type_specifier(CinderAst *ast);
static CinderType *parse_declaration_specifiers(CinderAst *ast, DeclarationSpec *spec);
static CinderType *parse_type_name(CinderAst *ast);
static CinderType *parse_declarator(CinderAst *ast, CinderType *base, char **name, CinderLoc *name_loc, CinderDecl **function_decl);
static CinderExpr *parse_conditional(CinderAst *ast);
static void parse_static_assert(CinderAst *ast);

static CinderType *parse_aggregate_specifier(CinderAst *ast, bool is_union) {
    CinderTypeKind kind = is_union ? TYPE_UNION : TYPE_STRUCT;
    CinderType *type = NULL;
    CinderToken *keyword = &ast->tokens->tokens.data[ast->cursor - 1U];
    if (is(ast, TOK_IDENTIFIER)) {
        CinderToken *tag = &ast->tokens->tokens.data[ast->cursor++];
        CinderParseBinding *old = binding_token(ast, tag, true);
        if (old != NULL && ((!is(ast, '{') && !is(ast, ';')) || old->scope == ast->scope_depth)) {
            type = old->type;
            if (type->kind != kind) cinder_diag(ast->diags, CINDER_ERROR, tag->loc, "tag has a different aggregate kind");
        } else {
            type = cinder_type_new(ast->types, kind);
            type->tag = cinder_arena_strndup(&ast->arena, tag->text, tag->length);
            bind_name(ast, type->tag, type, PARSE_TAG, 0, tag->loc);
        }
    }
    if (type == NULL) type = cinder_type_new(ast->types, kind);
    if (!take(ast, '{')) return type;
    if (type->complete) cinder_diag(ast->diags, CINDER_ERROR, keyword->loc, "aggregate tag is already defined");
    if (is(ast, '}')) cinder_diag(ast->diags, CINDER_ERROR, keyword->loc, "aggregate definition requires a member");
    while (!is(ast, TOK_EOF) && !is(ast, '}')) {
        if (is(ast, TOK_KW_STATIC_ASSERT)) { parse_static_assert(ast); continue; }
        DeclarationSpec spec = {0};
        CinderType *field_base = parse_declaration_specifiers(ast, &spec);
        if (spec.storage != 0U || spec.function_specifiers != 0U) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "member declaration cannot have storage or function specifiers");
        do {
            char *field_name = NULL; CinderLoc field_loc = peek(ast)->loc;
            CinderType *field_type = parse_declarator(ast, field_base, &field_name, &field_loc, NULL);
            CinderField field = {field_name, field_type, 0U, 0U, 0U, spec.alignment};
            for (size_t f = 0U; f < type->fields.len; ++f)
                if (field_name != NULL && type->fields.data[f].name != NULL && strcmp(field_name, type->fields.data[f].name) == 0) cinder_diag(ast->diags, CINDER_ERROR, field_loc, "duplicate aggregate member '%s'", field_name);
            cinder_vec_push((CinderVec *)&type->fields, &field);
        } while (take(ast, ','));
        (void)expect(ast, ';', "';'");
    }
    (void)expect(ast, '}', "'}'");
    if (type->fields.len == 0U) cinder_diag(ast->diags, CINDER_ERROR, keyword->loc, "aggregate definition requires a member");
    (void)cinder_type_layout_aggregate(type, ast->diags, type->tag == NULL ? cinder_loc(CINDER_NO_FILE, 0U, 0U) : peek(ast)->loc);
    type->completion_index = ast->cursor;
    for (size_t t = 0U; t < ast->types->all_types.len; ++t) {
        CinderType *copy = ast->types->all_types.data[t];
        if (copy == type || copy->identity != type->identity) continue;
        copy->complete = type->complete; copy->size = type->size; copy->align = type->align;
        copy->completion_index = type->completion_index;
        copy->fields.len = 0U;
        for (size_t f = 0U; f < type->fields.len; ++f) cinder_vec_push((CinderVec *)&copy->fields, &type->fields.data[f]);
    }
    return type;
}

static CinderType *parse_enum_specifier(CinderAst *ast) {
    CinderType *type = NULL;
    if (is(ast, TOK_IDENTIFIER)) {
        CinderToken *tag = &ast->tokens->tokens.data[ast->cursor++];
        CinderParseBinding *old = binding_token(ast, tag, true);
        if (old != NULL && (!is(ast, '{') || old->scope == ast->scope_depth)) {
            type = old->type;
            if (type->kind != TYPE_ENUM) cinder_diag(ast->diags, CINDER_ERROR, tag->loc, "tag has a different kind");
        } else {
            type = cinder_type_new(ast->types, TYPE_ENUM); type->tag = cinder_arena_strndup(&ast->arena, tag->text, tag->length);
            bind_name(ast, type->tag, type, PARSE_TAG, 0, tag->loc);
        }
    }
    if (type == NULL) type = cinder_type_new(ast->types, TYPE_ENUM);
    if (!take(ast, '{')) { if (!type->complete) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "enum tag has no definition"); return type; }
    if (type->complete) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "enum tag is already defined");
    if (is(ast, '}')) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "enum definition requires an enumerator");
    int64_t next = 0;
    while (!is(ast, '}') && !is(ast, TOK_EOF)) {
        CinderToken *token = expect(ast, TOK_IDENTIFIER, "enumerator name");
        if (token == NULL) break;
        char *name = cinder_arena_strndup(&ast->arena, token->text, token->length);
        if (take(ast, '=')) {
            CinderExpr *expr = parse_conditional(ast); CinderType *value_type;
            if (!cinder_constant_integer(ast, expr, &next, &value_type) || next < INT32_MIN || next > INT32_MAX) { cinder_diag(ast->diags, CINDER_ERROR, token->loc, "enumerator requires an integer constant representable as int"); next = 0; }
            CinderConstantExpr constant = {expr, ast->current_function, next}; cinder_vec_push((CinderVec *)&ast->constant_exprs, &constant);
        } else if (next > INT32_MAX) cinder_diag(ast->diags, CINDER_ERROR, token->loc, "enumerator value is outside int range");
        bind_name(ast, name, ast->types->int_type, PARSE_ENUMERATOR, next, token->loc);
        ++next;
        if (!take(ast, ',')) break;
    }
    (void)expect(ast, '}', "'}'"); type->size = 4U; type->align = 4U; type->complete = true; type->completion_index = ast->cursor;
    return type;
}

static size_t parse_alignment(CinderAst *ast) {
    CinderLoc loc = previous(ast)->loc;
    if (ast->alignment_depth >= 64U) {
        cinder_diag(ast->diags, CINDER_ERROR, loc, "alignment specifier nesting exceeds the profile limit");
        size_t nesting = 0U;
        while (!is(ast, TOK_EOF)) {
            if (take(ast, '(')) ++nesting;
            else if (take(ast, ')')) { if (nesting == 0U || --nesting == 0U) break; }
            else ++ast->cursor;
        }
        return 0U;
    }
    ++ast->alignment_depth;
    (void)expect(ast, '(', "'('");
    CinderExpr *expr;
    if (is_type_start(ast, peek(ast))) {
        expr = new_expr(ast, EX_ALIGNOF, loc); expr->queried_type = parse_type_name(ast); expr->parse_index = ast->cursor;
    } else expr = parse_conditional(ast);
    (void)expect(ast, ')', "')'"); --ast->alignment_depth;
    int64_t value = 0; CinderType *type;
    bool constant = cinder_constant_integer(ast, expr, &value, &type);
    CinderConstantExpr retained = {expr, ast->current_function, value}; cinder_vec_push((CinderVec *)&ast->constant_exprs, &retained);
    if (!constant || value < 0 || value > 16 || (value != 0 && ((uint64_t)value & ((uint64_t)value - 1U)) != 0U)) {
        cinder_diag(ast->diags, CINDER_ERROR, loc, "alignment requires zero or a supported power-of-two integer constant up to 16"); return 0U;
    }
    return (size_t)value;
}

static CinderType *parse_declaration_specifiers(CinderAst *ast, DeclarationSpec *spec) {
    CinderTypeContext *types = ast->types;
    unsigned qualifiers = 0U, longs = 0U;
    int sign = 0;
    bool short_spec = false, int_spec = false;
    CinderTokenKind scalar = 0;
    CinderType *aggregate = NULL;
    bool consumed = false;
    while (true) {
        CinderTokenKind kind = peek(ast)->kind;
        if (kind == TOK_KW_ALIGNAS) {
            CinderLoc loc = peek(ast)->loc; ++ast->cursor;
            size_t alignment = parse_alignment(ast);
            if (spec == NULL) cinder_diag(ast->diags, CINDER_ERROR, loc, "alignment specifier is not allowed in a type name");
            else { spec->alignment_specified = true; if (alignment > spec->alignment) spec->alignment = alignment; }
        } else if (kind == TOK_KW_TYPEDEF || kind == TOK_KW_STATIC || kind == TOK_KW_EXTERN || kind == TOK_KW_AUTO || kind == TOK_KW_REGISTER) {
            unsigned storage = kind == TOK_KW_TYPEDEF ? STORAGE_TYPEDEF : kind == TOK_KW_STATIC ? STORAGE_STATIC : kind == TOK_KW_EXTERN ? STORAGE_EXTERN : kind == TOK_KW_AUTO ? STORAGE_AUTO : STORAGE_REGISTER;
            if (spec == NULL) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "storage class is not allowed in a type name");
            else { if (spec->storage != 0U) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "duplicate or conflicting storage classes"); spec->storage |= storage; }
            ++ast->cursor;
        } else if (kind == TOK_KW_INLINE || kind == TOK_KW_NORETURN) {
            if (spec == NULL) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "function specifier is not allowed in a type name");
            else spec->function_specifiers |= kind == TOK_KW_INLINE ? 1U : 2U;
            ++ast->cursor;
        } else if (kind == TOK_KW_CONST || kind == TOK_KW_VOLATILE || kind == TOK_KW_RESTRICT) {
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
        } else if (kind == TOK_KW_ENUM) {
            if (aggregate != NULL) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "conflicting enum specifier");
            ++ast->cursor; aggregate = parse_enum_specifier(ast);
        } else if (kind == TOK_IDENTIFIER && aggregate == NULL && scalar == 0 && sign == 0 && longs == 0U && !short_spec && !int_spec) {
            CinderParseBinding *binding = binding_token(ast, peek(ast), false);
            if (binding == NULL || binding->kind != PARSE_TYPEDEF) break;
            aggregate = binding->type; ++ast->cursor;
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

static CinderType *parse_type_specifier(CinderAst *ast) { return parse_declaration_specifiers(ast, NULL); }

static void declaration_attributes(CinderAst *ast, CinderDecl *decl, const DeclarationSpec *spec) {
    decl->alignment = spec->alignment;
    decl->has_alignment = spec->alignment != 0U;
    decl->is_noreturn = (spec->function_specifiers & 2U) != 0U;
    decl->is_register = (spec->storage & STORAGE_REGISTER) != 0U;
    if (decl->kind == DECL_FUNCTION && (spec->storage & (STORAGE_AUTO | STORAGE_REGISTER)) != 0U) cinder_diag(ast->diags, CINDER_ERROR, decl->loc, "function declaration cannot have auto or register storage");
    if (spec->alignment_specified && (decl->kind != DECL_VAR || (spec->storage & STORAGE_REGISTER) != 0U)) cinder_diag(ast->diags, CINDER_ERROR, decl->loc, "alignment cannot apply to a typedef, function, or register object");
    if (spec->function_specifiers != 0U && (decl->kind != DECL_FUNCTION || (decl->name != NULL && strcmp(decl->name, "main") == 0))) cinder_diag(ast->diags, CINDER_ERROR, decl->loc, "function specifier requires a function other than main");
}

typedef enum { DECLARATOR_NAME, DECLARATOR_POINTER, DECLARATOR_ARRAY, DECLARATOR_FUNCTION } DeclaratorKind;
typedef struct DeclaratorNode DeclaratorNode;
typedef struct ParameterNode ParameterNode;
struct ParameterNode { CinderParam param; ParameterNode *next; };
struct DeclaratorNode {
    DeclaratorKind kind;
    DeclaratorNode *child;
    unsigned qualifiers;
    size_t length;
    bool incomplete;
    bool variadic;
    ParameterNode *params;
};

static DeclaratorNode *declarator_node(CinderAst *ast, DeclaratorKind kind, DeclaratorNode *child) {
    DeclaratorNode *node = node_alloc(ast, sizeof(*node)); memset(node, 0, sizeof(*node)); node->kind = kind; node->child = child; return node;
}

static CinderType *apply_declarator(CinderAst *ast, DeclaratorNode *node, CinderType *base) {
    for (; node != NULL && node->kind != DECLARATOR_NAME; node = node->child) {
        if (node->kind == DECLARATOR_POINTER) base = cinder_type_qualified(ast->types, cinder_type_pointer(ast->types, base), node->qualifiers);
        else if (node->kind == DECLARATOR_ARRAY) {
            if (cinder_type_contains_flexible(base)) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "array element cannot contain a flexible array member");
            if (base->kind == TYPE_VOID || base->kind == TYPE_FUNCTION || (!base->complete && base->kind != TYPE_ERROR)) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "array element requires a complete object type");
            base = cinder_type_array(ast->types, base, node->length);
            if (node->incomplete) base->complete = false;
        } else {
            if (base->kind == TYPE_ARRAY || base->kind == TYPE_FUNCTION) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "function cannot return an array or function");
            CinderParamVec params = {NULL, 0U, 0U};
            for (ParameterNode *p = node->params; p != NULL; p = p->next) cinder_vec_push((CinderVec *)&params, &p->param);
            base = cinder_type_function(ast->types, base, &params); base->variadic = node->variadic; free(params.data);
        }
    }
    return base;
}

static DeclaratorNode *parse_declarator_node(CinderAst *ast, char **name, CinderLoc *name_loc, bool abstract);

static CinderType *declarator_type(CinderAst *ast, CinderType *base, char **name, CinderLoc *name_loc, bool abstract) {
    if (++ast->declarator_depth >= 256U) {
        --ast->declarator_depth; cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "declarator nesting exceeds 255 levels"); return ast->types->error_type;
    }
    DeclaratorNode *node = parse_declarator_node(ast, name, name_loc, abstract);
    CinderType *type = apply_declarator(ast, node, base); --ast->declarator_depth; return type;
}

static DeclaratorNode *parse_declarator_node(CinderAst *ast, char **name, CinderLoc *name_loc, bool abstract) {
    DeclaratorNode *first = NULL, *last = NULL;
    unsigned pointers = 0U;
    while (take(ast, '*')) {
        if (++pointers + ast->declarator_depth >= 256U) { cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "pointer declarator exceeds nesting limit"); break; }
        DeclaratorNode *pointer = declarator_node(ast, DECLARATOR_POINTER, NULL);
        while (is(ast, TOK_KW_CONST) || is(ast, TOK_KW_VOLATILE) || is(ast, TOK_KW_RESTRICT)) {
            CinderTokenKind kind = peek(ast)->kind; ++ast->cursor;
            pointer->qualifiers |= kind == TOK_KW_CONST ? 1U : kind == TOK_KW_VOLATILE ? 2U : 4U;
        }
        if (first == NULL) first = pointer; else last->child = pointer;
        last = pointer;
    }
    DeclaratorNode *direct;
    if (is(ast, TOK_IDENTIFIER)) {
        CinderToken *token = &ast->tokens->tokens.data[ast->cursor++];
        *name = cinder_arena_strndup(&ast->arena, token->text, token->length); *name_loc = token->loc;
        direct = declarator_node(ast, DECLARATOR_NAME, NULL);
    } else if (is(ast, '(') && ast->cursor + 1U < ast->tokens->tokens.len && (ast->tokens->tokens.data[ast->cursor + 1U].kind == '*' || ast->tokens->tokens.data[ast->cursor + 1U].kind == '(' || (ast->tokens->tokens.data[ast->cursor + 1U].kind == TOK_IDENTIFIER && !is_type_start(ast, &ast->tokens->tokens.data[ast->cursor + 1U])))) {
        ++ast->cursor;
        if (++ast->declarator_depth >= 256U) { cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "declarator nesting exceeds 255 levels"); direct = declarator_node(ast, DECLARATOR_NAME, NULL); }
        else direct = parse_declarator_node(ast, name, name_loc, abstract);
        --ast->declarator_depth; (void)expect(ast, ')', "')'");
    } else {
        direct = declarator_node(ast, DECLARATOR_NAME, NULL);
        if (!abstract) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "expected an identifier in declarator");
    }
    size_t suffixes = 0U;
    while (is(ast, '[') || is(ast, '(')) {
        if (++suffixes >= 256U) { cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "declarator suffix nesting exceeds 255 levels"); break; }
        if (take(ast, '[')) {
            DeclaratorNode *array = declarator_node(ast, DECLARATOR_ARRAY, direct);
            unsigned qualifiers = 0U;
            while (is(ast, TOK_KW_CONST) || is(ast, TOK_KW_VOLATILE) || is(ast, TOK_KW_RESTRICT) || is(ast, TOK_KW_STATIC)) {
                CinderTokenKind kind = peek(ast)->kind; ++ast->cursor;
                qualifiers |= kind == TOK_KW_CONST ? 1U : kind == TOK_KW_VOLATILE ? 2U : kind == TOK_KW_RESTRICT ? 4U : 0U;
            }
            array->qualifiers = qualifiers;
            if (is(ast, ']')) array->incomplete = true;
            else {
                CinderExpr *expr = parse_conditional(ast); CinderType *type; int64_t length = 0;
                if (!cinder_constant_integer(ast, expr, &length, &type) || length <= 0 || (uint64_t)length > SIZE_MAX) cinder_diag(ast->diags, CINDER_ERROR, expr->loc, "array bound requires a positive integer constant");
                else array->length = (size_t)length;
                CinderConstantExpr constant = {expr, ast->current_function, length}; cinder_vec_push((CinderVec *)&ast->constant_exprs, &constant);
            }
            (void)expect(ast, ']', "']'"); direct = array;
        } else {
            ++ast->cursor; DeclaratorNode *fn = declarator_node(ast, DECLARATOR_FUNCTION, direct); ParameterNode *tail = NULL;
            size_t saved = ast->bindings.len; ++ast->scope_depth;
            if (is(ast, TOK_KW_VOID) && ast->cursor + 1U < ast->tokens->tokens.len && ast->tokens->tokens.data[ast->cursor + 1U].kind == ')') ++ast->cursor;
            else if (!is(ast, ')')) do {
                if (take(ast, TOK_ELLIPSIS)) { if (fn->params == NULL) cinder_diag(ast->diags, CINDER_ERROR, previous(ast)->loc, "variadic prototype requires a named parameter"); fn->variadic = true; break; }
                DeclarationSpec spec = {0};
                CinderType *base = parse_declaration_specifiers(ast, &spec); char *param_name = NULL; CinderLoc loc = peek(ast)->loc;
                if (spec.alignment_specified || spec.function_specifiers != 0U || (spec.storage & ~(unsigned)STORAGE_REGISTER) != 0U) cinder_diag(ast->diags, CINDER_ERROR, loc, "parameter has a forbidden alignment, function, or storage specifier");
                CinderType *type = declarator_type(ast, base, &param_name, &loc, true);
                unsigned flags = (spec.storage & STORAGE_REGISTER) != 0U ? CINDER_PARAM_REGISTER : 0U;
                if (type->kind == TYPE_ARRAY || type->kind == TYPE_FUNCTION) flags |= CINDER_PARAM_ADJUSTED;
                if (type->kind == TYPE_ARRAY) type = cinder_type_pointer(ast->types, type->base);
                else if (type->kind == TYPE_FUNCTION) type = cinder_type_pointer(ast->types, type);
                if (type->kind == TYPE_VOID) cinder_diag(ast->diags, CINDER_ERROR, loc, "void parameter must be the sole unnamed parameter");
                ParameterNode *param = node_alloc(ast, sizeof(*param)); param->param = (CinderParam){param_name, type, flags}; param->next = NULL;
                if (tail == NULL) fn->params = param; else tail->next = param;
                tail = param;
                CinderDecl *parameter_binding = new_decl(ast, DECL_VAR, loc); parameter_binding->name = param_name; parameter_binding->type = type;
                parameter_binding->is_register = (flags & CINDER_PARAM_REGISTER) != 0U; parameter_binding->parameter_adjusted = (flags & CINDER_PARAM_ADJUSTED) != 0U;
                CinderParseBinding *binding = bind_name(ast, param_name, type, PARSE_OBJECT, 0, loc); if (binding != NULL) binding->decl = parameter_binding;
            } while (take(ast, ','));
            (void)expect(ast, ')', "')'"); --ast->scope_depth; ast->bindings.len = saved; direct = fn;
        }
    }
    if (last != NULL) { last->child = direct; return first; }
    return direct;
}

static CinderType *parse_type_name(CinderAst *ast) {
    CinderType *base = parse_type_specifier(ast); char *name = NULL; CinderLoc loc = peek(ast)->loc;
    CinderType *type = declarator_type(ast, base, &name, &loc, true);
    if (name != NULL) cinder_diag(ast->diags, CINDER_ERROR, loc, "type name cannot declare an identifier");
    return type;
}

static CinderType *parse_declarator(CinderAst *ast, CinderType *base, char **name, CinderLoc *name_loc, CinderDecl **function_decl) {
    (void)function_decl; *name_loc = peek(ast)->loc;
    return declarator_type(ast, base, name, name_loc, false);
}

static CinderExpr *parse_expression(CinderAst *ast);
static CinderExpr *parse_assignment(CinderAst *ast);

static CinderExpr *parse_initializer(CinderAst *ast, unsigned depth) {
    if (!is(ast, '{')) return parse_assignment(ast);
    CinderExpr *expr = new_expr(ast, EX_INIT_LIST, peek(ast)->loc);
    if (depth >= 64U) {
        cinder_diag(ast->diags, CINDER_ERROR, expr->loc, "initializer nesting exceeds the profile limit");
        size_t braces = 0U;
        do {
            if (take(ast, '{')) ++braces;
            else if (take(ast, '}')) --braces;
            else if (!is(ast, TOK_EOF)) ++ast->cursor;
        } while (braces != 0U && !is(ast, TOK_EOF));
        return expr;
    }
    (void)take(ast, '{');
    if (is(ast, '}')) cinder_diag(ast->diags, CINDER_ERROR, expr->loc, "empty initializer list is outside C17");
    while (!is(ast, '}') && !is(ast, TOK_EOF)) {
        CinderInitEntry entry; memset(&entry, 0, sizeof(entry));
        while (is(ast, '.') || is(ast, '[')) {
            CinderInitDesignator designator; memset(&designator, 0, sizeof(designator));
            if (take(ast, '.')) {
                CinderToken *name = expect(ast, TOK_IDENTIFIER, "initializer member name");
                if (name != NULL) designator.member = cinder_arena_strndup(&ast->arena, name->text, name->length);
            } else {
                (void)take(ast, '['); designator.index = parse_conditional(ast); (void)expect(ast, ']', "']'");
            }
            cinder_vec_push((CinderVec *)&entry.designators, &designator);
        }
        if (entry.designators.len != 0U) (void)expect(ast, '=', "'=' after initializer designator");
        size_t before = ast->cursor;
        entry.value = parse_initializer(ast, depth + 1U);
        cinder_vec_push((CinderVec *)&expr->as.initializer.entries, &entry);
        if (ast->cursor == before && !is(ast, TOK_EOF)) ++ast->cursor;
        if (!take(ast, ',')) break;
    }
    (void)expect(ast, '}', "'}'"); expr->parse_index = ast->cursor; return expr;
}

static CinderExpr *parse_primary(CinderAst *ast) {
    CinderToken *token = peek(ast);
    if (token->kind == TOK_IDENTIFIER && token->length == sizeof("__cinder_offsetof") - 1U && memcmp(token->text, "__cinder_offsetof", token->length) == 0) {
        ++ast->cursor;
        CinderExpr *expr = new_expr(ast, EX_OFFSETOF, token->loc);
        if (ast->offsetof_depth >= 128U) {
            cinder_diag(ast->diags, CINDER_ERROR, token->loc, "offsetof nesting exceeds the profile limit");
            size_t parentheses = 0U;
            while (!is(ast, TOK_EOF)) {
                if (take(ast, '(')) ++parentheses;
                else if (take(ast, ')')) { if (parentheses == 0U || --parentheses == 0U) break; }
                else ++ast->cursor;
            }
            return expr;
        }
        ++ast->offsetof_depth;
        (void)expect(ast, '(', "'('"); expr->as.offset.object_type = parse_type_name(ast);
        (void)expect(ast, ',', "','");
        bool first = true;
        do {
            CinderInitDesignator part = {NULL, NULL};
            if (first || take(ast, '.')) {
                CinderToken *name = expect(ast, TOK_IDENTIFIER, "offsetof member name");
                if (name != NULL) part.member = cinder_arena_strndup(&ast->arena, name->text, name->length);
            } else {
                (void)expect(ast, '[', "'['"); part.index = parse_conditional(ast); (void)expect(ast, ']', "']'");
            }
            cinder_vec_push((CinderVec *)&expr->as.offset.path, &part); first = false;
            if (expr->as.offset.path.len >= 256U && (is(ast, '.') || is(ast, '['))) {
                cinder_diag(ast->diags, CINDER_ERROR, token->loc, "offsetof designator exceeds the profile limit");
                while (!is(ast, ')') && !is(ast, TOK_EOF)) ++ast->cursor;
                break;
            }
        } while (is(ast, '.') || is(ast, '['));
        (void)expect(ast, ')', "')'"); expr->parse_index = ast->cursor;
        --ast->offsetof_depth; return expr;
    }
    if (take(ast, TOK_KW_GENERIC)) {
        if (ast->generic_depth >= 128U) {
            cinder_diag(ast->diags, CINDER_ERROR, token->loc, "generic expression nesting exceeds the profile limit");
            size_t parentheses = 0U;
            while (!is(ast, TOK_EOF)) {
                if (take(ast, '(')) ++parentheses;
                else if (take(ast, ')')) { if (parentheses == 0U || --parentheses == 0U) break; }
                else ++ast->cursor;
            }
            return new_expr(ast, EX_INT, token->loc);
        }
        ++ast->generic_depth;
        CinderExpr *expr = new_expr(ast, EX_GENERIC, token->loc);
        expr->as.generic.selected = SIZE_MAX;
        (void)expect(ast, '(', "'('"); expr->as.generic.control = parse_assignment(ast);
        (void)expect(ast, ',', "','");
        do {
            CinderGenericAssociation association;
            association.type = take(ast, TOK_KW_DEFAULT) ? NULL : parse_type_name(ast);
            association.parse_index = ast->cursor;
            (void)expect(ast, ':', "':'"); association.value = parse_assignment(ast);
            cinder_vec_push((CinderVec *)&expr->as.generic.associations, &association);
        } while (take(ast, ','));
        (void)expect(ast, ')', "')'"); --ast->generic_depth; return expr;
    }
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
        CinderExpr *expr = new_expr(ast, EX_CHAR, token->loc);
        size_t count; char *bytes = cinder_literal_decode(&ast->arena, token, &count, ast->diags);
        if (count != 1U) cinder_diag(ast->diags, CINDER_ERROR, token->loc, "ordinary character constant requires one target byte");
        unsigned value = count == 0U ? 0U : (unsigned char)bytes[0];
        expr->as.integer = value < 128U ? (int64_t)value : (int64_t)value - 256;
        expr->type = ast->types->int_type; return expr;
    }
    if (take(ast, TOK_STRING)) {
        CinderExpr *expr = new_expr(ast, EX_STRING, token->loc);
        CinderBytes bytes = {NULL, 0U, 0U};
        do {
            size_t count; char *part = cinder_literal_decode(&ast->arena, token, &count, ast->diags);
            cinder_bytes_append(&bytes, (const unsigned char *)part, count);
            token = peek(ast);
        } while (take(ast, TOK_STRING));
        expr->literal_length = bytes.len;
        expr->as.string = cinder_arena_strndup(&ast->arena, bytes.data == NULL ? "" : (const char *)bytes.data, bytes.len);
        expr->type = cinder_type_array(ast->types, ast->types->char_type, bytes.len + 1U);
        free(bytes.data); return expr;
    }
    if (take(ast, TOK_IDENTIFIER)) {
        CinderParseBinding *binding = binding_token(ast, token, false);
        if (binding != NULL && binding->kind == PARSE_ENUMERATOR) { CinderExpr *expr = new_expr(ast, EX_INT, token->loc); expr->as.integer = binding->value; expr->type = binding->type; return expr; }
        if (binding != NULL && binding->kind == PARSE_TYPEDEF) cinder_diag(ast->diags, CINDER_ERROR, token->loc, "typedef name is not an expression");
        CinderExpr *expr = new_expr(ast, EX_NAME, token->loc); expr->as.name = cinder_arena_strndup(&ast->arena, token->text, token->length);
        if (binding != NULL) { expr->type = binding->type; expr->name_visible = binding->kind == PARSE_OBJECT; expr->resolved_decl = binding->decl; }
        return expr;
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

static CinderExpr *parse_postfix_tail(CinderAst *ast, CinderExpr *expr) {
    while (true) {
        if (take(ast, '(')) {
            CinderExpr *call = new_expr(ast, EX_CALL, expr->loc);
            call->as.call.callee = expr;
            call->as.call.args.data = NULL; call->as.call.args.len = 0U; call->as.call.args.cap = 0U;
            if (expr->kind == EX_NAME && strcmp(expr->as.name, "__cinder_va_arg") == 0) {
                CinderExpr *list = parse_assignment(ast);
                (void)expect(ast, ',', "','");
                CinderType *argument_type = parse_type_name(ast);
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
        } else if (take(ast, '[')) {
            CinderExpr *index = new_expr(ast, EX_INDEX, expr->loc);
            index->as.index.base = expr; index->as.index.index = parse_expression(ast);
            (void)expect(ast, ']', "']'"); expr = index;
        } else if (is(ast, '.') || is(ast, TOK_ARROW)) {
            bool arrow = take(ast, TOK_ARROW); if (!arrow) (void)take(ast, '.');
            CinderToken *name = expect(ast, TOK_IDENTIFIER, "member name");
            CinderExpr *member = new_expr(ast, EX_MEMBER, expr->loc);
            member->as.member.base = expr; member->as.member.arrow = arrow;
            member->as.member.name = name == NULL ? cinder_arena_strndup(&ast->arena, "", 0U) : cinder_arena_strndup(&ast->arena, name->text, name->length); expr = member;
        } else if (take(ast, TOK_PLUSPLUS)) {
            CinderExpr *unary = new_expr(ast, EX_UNARY, expr->loc); unary->as.unary.op = TOK_PLUSPLUS; unary->as.unary.value = expr; unary->as.unary.postfix = true; expr = unary;
        } else if (take(ast, TOK_MINUSMINUS)) {
            CinderExpr *unary = new_expr(ast, EX_UNARY, expr->loc); unary->as.unary.op = TOK_MINUSMINUS; unary->as.unary.value = expr; unary->as.unary.postfix = true; expr = unary;
        } else break;
    }
    return expr;
}

static CinderExpr *parse_compound_literal(CinderAst *ast, CinderType *type, CinderLoc loc) {
    if (type->kind == TYPE_ARRAY && !type->complete) { type = cinder_type_array(ast->types, type->base, 0U); type->complete = false; }
    CinderDecl *decl = new_decl(ast, DECL_VAR, loc);
    char name[64]; int length = snprintf(name, sizeof(name), ".LCO.%zu", ast->literal_count++);
    if (length < 0 || (size_t)length >= sizeof(name)) abort();
    decl->name = cinder_arena_strndup(&ast->arena, name, (size_t)length);
    decl->type = type; decl->is_static = ast->literal_scope == NULL;
    decl->is_definition = true; decl->has_definition = true;
    decl->initializer = parse_initializer(ast, 0U); decl->initializer_index = ast->cursor;
    CinderExpr *expr = new_expr(ast, EX_COMPOUND_LITERAL, loc);
    expr->as.compound_literal = decl; expr->type = type; expr->is_lvalue = true;
    if (ast->literal_scope != NULL) cinder_vec_push((CinderVec *)&ast->literal_scope->literal_objects, &decl);
    else cinder_vec_push((CinderVec *)&ast->static_literals, &decl);
    return parse_postfix_tail(ast, expr);
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
        if (is(ast, '(') && ast->cursor + 1U < ast->tokens->tokens.len && is_type_start(ast, &ast->tokens->tokens.data[ast->cursor + 1U])) {
            ++ast->cursor; CinderLoc loc = previous(ast)->loc;
            expr->queried_type = parse_type_name(ast); (void)expect(ast, ')', "')'");
            if (kind == TOK_KW_SIZEOF && is(ast, '{')) {
                expr->as.unary.value = parse_compound_literal(ast, expr->queried_type, loc); expr->queried_type = NULL;
            }
        } else if (kind == TOK_KW_ALIGNOF) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "_Alignof requires a type name");
        else expr->as.unary.value = parse_unary(ast);
        expr->parse_index = ast->cursor;
        return expr;
    }
    if (is(ast, '(') && ast->cursor + 1U < ast->tokens->tokens.len && is_type_start(ast, &ast->tokens->tokens.data[ast->cursor + 1U])) {
        ++ast->cursor;
        CinderLoc loc = previous(ast)->loc;
        CinderType *type = parse_type_name(ast); (void)expect(ast, ')', "')'");
        if (is(ast, '{')) return parse_compound_literal(ast, type, loc);
        CinderExpr *expr = new_expr(ast, EX_CAST, loc); expr->as.cast.cast_type = type;
        expr->as.cast.value = parse_unary(ast); return expr;
    }
    return parse_postfix_tail(ast, parse_primary(ast));
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

static void parse_static_assert(CinderAst *ast) {
    CinderToken *keyword = expect(ast, TOK_KW_STATIC_ASSERT, "'_Static_assert'");
    CinderLoc loc = keyword == NULL ? peek(ast)->loc : keyword->loc;
    (void)expect(ast, '(', "'('");
    CinderExpr *expr = parse_conditional(ast);
    int64_t value = 0; CinderType *type;
    bool constant = cinder_constant_integer(ast, expr, &value, &type);
    CinderConstantExpr retained = {expr, ast->current_function, value};
    cinder_vec_push((CinderVec *)&ast->constant_exprs, &retained);
    (void)expect(ast, ',', "','");
    CinderExpr *message = NULL;
    if (is(ast, TOK_STRING)) message = parse_primary(ast);
    else cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "static assertion requires a string literal message");
    (void)expect(ast, ')', "')'"); (void)expect(ast, ';', "';'");
    if (!constant) cinder_diag(ast->diags, CINDER_ERROR, loc, "static assertion requires an integer constant expression");
    else if (value == 0) {
        CinderBytes text = {NULL, 0U, 0U};
        if (message != NULL) for (size_t i = 0U; i < message->literal_length; ++i) {
            unsigned char byte = (unsigned char)message->as.string[i];
            if (byte >= 32U && byte <= 126U) cinder_bytes_put8(&text, byte);
            else {
                const char digits[] = "0123456789abcdef";
                cinder_bytes_put8(&text, '\\'); cinder_bytes_put8(&text, 'x');
                cinder_bytes_put8(&text, (uint8_t)digits[byte >> 4U]); cinder_bytes_put8(&text, (uint8_t)digits[byte & 15U]);
            }
        }
        cinder_bytes_put8(&text, 0U);
        cinder_diag(ast->diags, CINDER_ERROR, loc, "static assertion failed: %s", (char *)text.data);
        free(text.data);
    }
}

static CinderStmt *parse_compound(CinderAst *ast) {
    CinderLoc loc = peek(ast)->loc;
    (void)expect(ast, '{', "'{'");
    size_t saved = ast->bindings.len; ++ast->scope_depth;
    CinderStmt *stmt = new_stmt(ast, ST_BLOCK, loc);
    CinderStmt *outer = ast->literal_scope; ast->literal_scope = stmt;
    stmt->as.block.items.data = NULL; stmt->as.block.items.len = 0U; stmt->as.block.items.cap = 0U;
    while (!is(ast, TOK_EOF) && !is(ast, '}')) {
        CinderStmt *item = parse_statement(ast);
        if (item != NULL) cinder_vec_push((CinderVec *)&stmt->as.block.items, &item);
    }
    (void)expect(ast, '}', "'}'");
    --ast->scope_depth; ast->bindings.len = saved;
    ast->literal_scope = outer;
    return stmt;
}

static bool identifier_label(CinderAst *ast) {
    return is(ast, TOK_IDENTIFIER) && ast->cursor + 1U < ast->tokens->tokens.len && ast->tokens->tokens.data[ast->cursor + 1U].kind == ':';
}

static CinderStmt *parse_associated(CinderAst *ast) {
    if ((declaration_start(ast, peek(ast)) && !identifier_label(ast)) || is(ast, TOK_KW_STATIC_ASSERT)) cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "associated substatement cannot be a declaration");
    if (is(ast, '{')) return parse_compound(ast);
    CinderStmt *scope = new_stmt(ast, ST_BLOCK, peek(ast)->loc);
    CinderStmt *outer = ast->literal_scope; ast->literal_scope = scope;
    size_t saved = ast->bindings.len; ++ast->scope_depth;
    CinderStmt *body = parse_statement(ast);
    --ast->scope_depth; ast->bindings.len = saved; ast->literal_scope = outer;
    cinder_vec_push((CinderVec *)&scope->as.block.items, &body);
    return scope;
}

static CinderDecl *parse_local_decl(CinderAst *ast) {
    CinderLoc loc = peek(ast)->loc;
    DeclarationSpec spec = {0}; CinderType *base = parse_declaration_specifiers(ast, &spec);
    bool alias = (spec.storage & STORAGE_TYPEDEF) != 0U, is_static = (spec.storage & STORAGE_STATIC) != 0U, is_extern = (spec.storage & STORAGE_EXTERN) != 0U;
    if (is(ast, ';') && (base->kind == TYPE_STRUCT || base->kind == TYPE_UNION || base->kind == TYPE_ENUM)) {
        if (spec.alignment_specified) cinder_diag(ast->diags, CINDER_WARNING, loc, "alignment specifier has no object or member to align");
        if (spec.function_specifiers != 0U) cinder_diag(ast->diags, CINDER_ERROR, loc, "function specifier requires a declarator");
        ++ast->cursor; return NULL;
    }
    CinderDecl *first = NULL, *tail = NULL;
    do {
        char *name = NULL; CinderLoc name_loc;
        CinderType *type = parse_declarator(ast, base, &name, &name_loc, NULL);
        if (!alias && (is_extern || type->kind == TYPE_FUNCTION)) type = local_linkage_type(ast, name, type);
        if (!alias && type == base && type->kind == TYPE_ARRAY && !type->complete) { type = cinder_type_array(ast->types, type->base, 0U); type->complete = false; }
        CinderDecl *decl = new_decl(ast, alias ? DECL_TYPEDEF : type->kind == TYPE_FUNCTION ? DECL_FUNCTION : DECL_VAR, loc);
        decl->name = name; decl->loc = name_loc; decl->type = type; decl->is_static = is_static; decl->is_extern = is_extern;
        declaration_attributes(ast, decl, &spec);
        decl->declaration_complete = type->complete;
        CinderParseBinding *binding = bind_name(ast, name, type, alias ? PARSE_TYPEDEF : PARSE_OBJECT, 0, name_loc);
        if (binding != NULL) binding->decl = decl;
        if (take(ast, '=')) {
            decl->initializer = parse_initializer(ast, 0U); decl->initializer_index = ast->cursor;
            (void)cinder_infer_initializer_shape(ast, decl, 0U);
            if (alias || type->kind == TYPE_FUNCTION) cinder_diag(ast->diags, CINDER_ERROR, loc, "typedef/function cannot have an initializer");
        }
        if (tail == NULL) first = decl; else tail->next = decl;
        tail = decl;
    } while (take(ast, ','));
    (void)expect(ast, ';', "';'");
    return first;
}

static CinderStmt *parse_statement_impl(CinderAst *ast);

static CinderStmt *parse_statement(CinderAst *ast) {
    if (ast->statement_depth >= 512U) {
        CinderLoc loc = peek(ast)->loc;
        cinder_diag(ast->diags, CINDER_ERROR, loc, "statement nesting exceeds the profile limit");
        while (!is(ast, ';') && !is(ast, '}') && !is(ast, TOK_EOF)) ++ast->cursor;
        (void)take(ast, ';'); return new_stmt(ast, ST_EMPTY, loc);
    }
    ++ast->statement_depth; CinderStmt *stmt = parse_statement_impl(ast); --ast->statement_depth; return stmt;
}

static CinderStmt *parse_statement_impl(CinderAst *ast) {
    CinderToken *token = peek(ast);
    if (is(ast, TOK_KW_CASE) || is(ast, TOK_KW_DEFAULT)) {
        bool is_case = take(ast, TOK_KW_CASE); if (!is_case) ++ast->cursor;
        CinderStmt *stmt = new_stmt(ast, is_case ? ST_CASE : ST_DEFAULT, token->loc);
        if (is_case) {
            CinderExpr *expr = parse_conditional(ast); CinderType *type = NULL; int64_t value = 0;
            stmt->as.case_label.expression = expr;
            if (!cinder_constant_integer(ast, expr, &value, &type)) cinder_diag(ast->diags, CINDER_ERROR, token->loc, "case label requires an integer constant expression");
            stmt->as.case_label.value = value;
        }
        (void)expect(ast, ':', "':'");
        if (ast->label_depth >= 128U) { cinder_diag(ast->diags, CINDER_ERROR, token->loc, "label nesting exceeds the profile limit"); return stmt; }
        if ((declaration_start(ast, peek(ast)) && !identifier_label(ast)) || is(ast, TOK_KW_STATIC_ASSERT) || is(ast, '}') || is(ast, TOK_EOF)) cinder_diag(ast->diags, CINDER_ERROR, token->loc, "C17 label requires a statement");
        if (!is(ast, '}') && !is(ast, TOK_EOF)) { ++ast->label_depth; stmt->as.case_label.body = parse_statement(ast); --ast->label_depth; }
        return stmt;
    }
    if (identifier_label(ast)) {
        CinderStmt *stmt = new_stmt(ast, ST_LABEL, token->loc);
        stmt->as.label.name = cinder_arena_strndup(&ast->arena, token->text, token->length); ast->cursor += 2U;
        if (ast->label_depth >= 128U) { cinder_diag(ast->diags, CINDER_ERROR, token->loc, "label nesting exceeds the profile limit"); return stmt; }
        if ((declaration_start(ast, peek(ast)) && !identifier_label(ast)) || is(ast, TOK_KW_STATIC_ASSERT) || is(ast, '}') || is(ast, TOK_EOF)) cinder_diag(ast->diags, CINDER_ERROR, token->loc, "C17 label requires a statement");
        if (!is(ast, '}') && !is(ast, TOK_EOF)) { ++ast->label_depth; stmt->as.label.body = parse_statement(ast); --ast->label_depth; }
        return stmt;
    }
    if (take(ast, TOK_KW_GOTO)) {
        CinderStmt *stmt = new_stmt(ast, ST_GOTO, token->loc);
        CinderToken *name = expect(ast, TOK_IDENTIFIER, "label identifier");
        if (name != NULL) stmt->as.jump.name = cinder_arena_strndup(&ast->arena, name->text, name->length);
        (void)expect(ast, ';', "';'"); return stmt;
    }
    if (is(ast, '{')) return parse_compound(ast);
    if (take(ast, ';')) return new_stmt(ast, ST_EMPTY, token->loc);
    if (is(ast, TOK_KW_STATIC_ASSERT)) { parse_static_assert(ast); return new_stmt(ast, ST_EMPTY, token->loc); }
    if (declaration_start(ast, token)) {
        CinderStmt *stmt = new_stmt(ast, ST_DECL, token->loc); stmt->as.decl = parse_local_decl(ast);
        if (stmt->as.decl == NULL) stmt->kind = ST_EMPTY;
        return stmt;
    }
    if (take(ast, TOK_KW_RETURN)) {
        CinderStmt *stmt = new_stmt(ast, ST_RETURN, token->loc);
        stmt->as.ret.value = is(ast, ';') ? NULL : parse_expression(ast);
        (void)expect(ast, ';', "';'"); return stmt;
    }
    if (take(ast, TOK_KW_IF)) {
        CinderStmt *stmt = new_stmt(ast, ST_IF, token->loc), *outer = ast->literal_scope; ast->literal_scope = stmt;
        size_t saved = ast->bindings.len; ++ast->scope_depth;
        (void)expect(ast, '(', "'('"); stmt->as.if_stmt.condition = parse_expression(ast); (void)expect(ast, ')', "')'");
        stmt->as.if_stmt.then_branch = parse_associated(ast); stmt->as.if_stmt.else_branch = take(ast, TOK_KW_ELSE) ? parse_associated(ast) : NULL;
        --ast->scope_depth; ast->bindings.len = saved; ast->literal_scope = outer; return stmt;
    }
    if (take(ast, TOK_KW_SWITCH)) {
        CinderStmt *stmt = new_stmt(ast, ST_SWITCH, token->loc), *outer = ast->literal_scope; ast->literal_scope = stmt;
        size_t saved = ast->bindings.len; ++ast->scope_depth;
        (void)expect(ast, '(', "'('"); stmt->as.selection.control = parse_expression(ast); (void)expect(ast, ')', "')'");
        stmt->as.selection.body = parse_associated(ast);
        --ast->scope_depth; ast->bindings.len = saved; ast->literal_scope = outer; return stmt;
    }
    if (take(ast, TOK_KW_WHILE)) {
        CinderStmt *stmt = new_stmt(ast, ST_WHILE, token->loc), *outer = ast->literal_scope; ast->literal_scope = stmt;
        size_t saved = ast->bindings.len; ++ast->scope_depth;
        (void)expect(ast, '(', "'('"); stmt->as.loop.condition = parse_expression(ast); (void)expect(ast, ')', "')'"); stmt->as.loop.body = parse_associated(ast);
        --ast->scope_depth; ast->bindings.len = saved; ast->literal_scope = outer; return stmt;
    }
    if (take(ast, TOK_KW_DO)) {
        CinderStmt *stmt = new_stmt(ast, ST_DO, token->loc);
        CinderStmt *outer = ast->literal_scope; ast->literal_scope = stmt;
        size_t saved = ast->bindings.len; ++ast->scope_depth;
        stmt->as.loop.body = parse_associated(ast);
        (void)expect(ast, TOK_KW_WHILE, "'while'");
        (void)expect(ast, '(', "'('");
        stmt->as.loop.condition = parse_expression(ast);
        (void)expect(ast, ')', "')'");
        (void)expect(ast, ';', "';'");
        --ast->scope_depth; ast->bindings.len = saved; ast->literal_scope = outer; return stmt;
    }
    if (take(ast, TOK_KW_FOR)) {
        CinderStmt *stmt = new_stmt(ast, ST_FOR, token->loc); (void)expect(ast, '(', "'('");
        CinderStmt *outer = ast->literal_scope; ast->literal_scope = stmt;
        size_t saved = ast->bindings.len; ++ast->scope_depth;
        if (declaration_start(ast, peek(ast))) stmt->as.for_stmt.init = new_stmt(ast, ST_DECL, peek(ast)->loc), stmt->as.for_stmt.init->as.decl = parse_local_decl(ast);
        else if (!is(ast, ';')) { stmt->as.for_stmt.init = new_stmt(ast, ST_EXPR, peek(ast)->loc); stmt->as.for_stmt.init->as.expr = parse_expression(ast); (void)expect(ast, ';', "';'"); }
        else { take(ast, ';'); stmt->as.for_stmt.init = NULL; }
        stmt->as.for_stmt.condition = is(ast, ';') ? NULL : parse_expression(ast); (void)expect(ast, ';', "';'");
        stmt->as.for_stmt.step = is(ast, ')') ? NULL : parse_expression(ast); (void)expect(ast, ')', "')'"); stmt->as.for_stmt.body = parse_associated(ast);
        --ast->scope_depth; ast->bindings.len = saved; ast->literal_scope = outer; return stmt;
    }
    if (take(ast, TOK_KW_BREAK)) { CinderStmt *stmt = new_stmt(ast, ST_BREAK, token->loc); (void)expect(ast, ';', "';'"); return stmt; }
    if (take(ast, TOK_KW_CONTINUE)) { CinderStmt *stmt = new_stmt(ast, ST_CONTINUE, token->loc); (void)expect(ast, ';', "';'"); return stmt; }
    CinderStmt *stmt = new_stmt(ast, ST_EXPR, token->loc); stmt->as.expr = is(ast, ';') ? NULL : parse_expression(ast); (void)expect(ast, ';', "';'"); return stmt;
}

int cinder_parse(CinderAst *ast) {
    while (!is(ast, TOK_EOF)) {
        if (is(ast, TOK_KW_STATIC_ASSERT)) { parse_static_assert(ast); continue; }
        if (!declaration_start(ast, peek(ast))) {
            cinder_diag(ast->diags, CINDER_ERROR, peek(ast)->loc, "expected a declaration"); ++ast->cursor; continue;
        }
        CinderLoc loc = peek(ast)->loc; DeclarationSpec spec = {0};
        CinderType *base = parse_declaration_specifiers(ast, &spec);
        bool alias = (spec.storage & STORAGE_TYPEDEF) != 0U, is_static = (spec.storage & STORAGE_STATIC) != 0U, is_extern = (spec.storage & STORAGE_EXTERN) != 0U;
        if ((spec.storage & (STORAGE_AUTO | STORAGE_REGISTER)) != 0U) cinder_diag(ast->diags, CINDER_ERROR, loc, "file declaration cannot have automatic/register storage");
        if ((base->kind == TYPE_STRUCT || base->kind == TYPE_UNION || base->kind == TYPE_ENUM) && take(ast, ';')) {
            if (spec.alignment_specified) cinder_diag(ast->diags, CINDER_WARNING, loc, "alignment specifier has no object or member to align");
            if (spec.function_specifiers != 0U) cinder_diag(ast->diags, CINDER_ERROR, loc, "function specifier requires a declarator");
            continue;
        }
        bool definition = false;
        do {
            char *name = NULL; CinderLoc name_loc;
            CinderType *type = parse_declarator(ast, base, &name, &name_loc, NULL);
            if (!alias && type == base && type->kind == TYPE_ARRAY && !type->complete) { type = cinder_type_array(ast->types, type->base, 0U); type->complete = false; }
            if (!alias) type = file_composite_type(ast, name, type);
            CinderDecl *decl = new_decl(ast, alias ? DECL_TYPEDEF : type->kind == TYPE_FUNCTION ? DECL_FUNCTION : DECL_VAR, loc);
            decl->name = name; decl->loc = name_loc; decl->type = type; decl->is_static = is_static; decl->is_extern = is_extern;
            declaration_attributes(ast, decl, &spec);
            decl->declaration_complete = type->complete;
            CinderParseBinding *binding = bind_name(ast, name, type, alias ? PARSE_TYPEDEF : PARSE_OBJECT, 0, name_loc);
            if (binding != NULL) binding->decl = decl;
            if (decl->kind == DECL_FUNCTION) {
                for (size_t i = 0U; i < type->params.len; ++i) {
                    CinderDecl *param = new_decl(ast, DECL_VAR, name_loc); param->name = type->params.data[i].name; param->type = type->params.data[i].type;
                    param->is_register = (type->params.data[i].declaration_flags & CINDER_PARAM_REGISTER) != 0U; param->parameter_adjusted = (type->params.data[i].declaration_flags & CINDER_PARAM_ADJUSTED) != 0U;
                    cinder_vec_push((CinderVec *)&decl->params, &param);
                }
                if (is(ast, '{')) {
                    size_t saved = ast->bindings.len; ++ast->scope_depth;
                    for (size_t p = 0U; p < decl->params.len; ++p) {
                        CinderDecl *param = decl->params.data[p];
                        if (param->name == NULL) cinder_diag(ast->diags, CINDER_ERROR, name_loc, "function definition requires parameter names");
                        CinderParseBinding *parameter_binding = bind_name(ast, param->name, param->type, PARSE_OBJECT, 0, name_loc);
                        if (parameter_binding != NULL) parameter_binding->decl = param;
                    }
                    --ast->scope_depth;
                    decl->is_definition = true; ast->current_function = decl; decl->body = parse_compound(ast); ast->current_function = NULL; definition = true;
                    ast->bindings.len = saved;
                }
            } else if (take(ast, '=')) {
                decl->initializer = parse_initializer(ast, 0U);
                decl->initializer_index = ast->cursor;
                (void)cinder_infer_initializer_shape(ast, decl, 0U);
                if (alias) cinder_diag(ast->diags, CINDER_ERROR, loc, "typedef cannot have an initializer");
            }
            cinder_vec_push((CinderVec *)&ast->declarations, &decl);
            if (definition) break;
        } while (take(ast, ','));
        if (!definition) (void)expect(ast, ';', "';'");
    }
    return ast->diags->errors == 0U ? 0 : 1;
}
