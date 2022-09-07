#include "cinder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CinderSymbol *scope_add(CinderScope *scope, const char *name, CinderType *type, CinderDecl *decl, bool function) {
    CinderSymbol symbol;
    symbol.name = cinder_strndup(name, strlen(name)); symbol.type = type; symbol.decl = decl; symbol.is_function = function;
    cinder_vec_push((CinderVec *)&scope->symbols, &symbol);
    return &scope->symbols.data[scope->symbols.len - 1U];
}

CinderSymbol *cinder_scope_lookup(CinderScope *scope, const char *name) {
    for (CinderScope *current = scope; current != NULL; current = current->parent) {
        for (size_t i = current->symbols.len; i > 0U; --i) if (strcmp(current->symbols.data[i - 1U].name, name) == 0) return &current->symbols.data[i - 1U];
    }
    return NULL;
}

void cinder_sema_init(CinderSema *sema, CinderAst *ast, CinderTypeContext *types, CinderDiagnostics *diags) {
    sema->ast = ast; sema->types = types; sema->diags = diags; sema->function_body = NULL; sema->function = NULL; sema->unevaluated_depth = 0U;
    sema->globals.symbols.data = NULL; sema->globals.symbols.len = 0U; sema->globals.symbols.cap = 0U; sema->globals.parent = NULL;
}

void cinder_sema_destroy(CinderSema *sema) {
    for (size_t i = 0U; i < sema->globals.symbols.len; ++i) free(sema->globals.symbols.data[i].name);
    free(sema->globals.symbols.data);
}

static bool integer_type(const CinderType *type) {
    return type != NULL && (type->kind == TYPE_BOOL || type->kind == TYPE_CHAR || type->kind == TYPE_SHORT || type->kind == TYPE_INT || type->kind == TYPE_LONG || type->kind == TYPE_LLONG || type->kind == TYPE_ENUM);
}

static bool floating_type(const CinderType *type) { return type != NULL && (type->kind == TYPE_FLOAT || type->kind == TYPE_DOUBLE); }
static bool numeric_type(const CinderType *type) { return integer_type(type) || floating_type(type); }
static bool scalar_type(const CinderType *type) { return numeric_type(type) || (type != NULL && type->kind == TYPE_POINTER); }
static bool pointer_compatible(const CinderType *target, const CinderType *value) {
    if (target->kind != TYPE_POINTER || value->kind != TYPE_POINTER) return false;
    const CinderType *a = target->base, *b = value->base;
    if ((b->qualifiers & ~a->qualifiers) != 0U) return false;
    CinderType x = *a, y = *b; x.qualifiers = 0U; y.qualifiers = 0U;
    if (cinder_type_compatible(&x, &y)) return true;
    return (a->kind == TYPE_VOID && b->kind != TYPE_FUNCTION) || (b->kind == TYPE_VOID && a->kind != TYPE_FUNCTION);
}
static bool value_compatible(const CinderType *target, const CinderType *value) {
    if (target->kind == TYPE_POINTER || value->kind == TYPE_POINTER) return (target->kind == TYPE_BOOL && value->kind == TYPE_POINTER) || pointer_compatible(target, value);
    CinderType a = *target, b = *value; a.qualifiers = 0U; b.qualifiers = 0U;
    return cinder_type_equal(&a, &b) || (numeric_type(target) && numeric_type(value));
}

static CinderExpr *convert_expr(CinderSema *sema, CinderExpr *value, CinderType *type) {
    if (value == NULL || cinder_type_equal(value->type, type)) return value;
    if ((type->kind == TYPE_STRUCT || type->kind == TYPE_UNION) && value_compatible(type, value->type)) return value;
    CinderExpr *cast = cinder_arena_alloc(&sema->ast->arena, sizeof(*cast), _Alignof(CinderExpr));
    memset(cast, 0, sizeof(*cast)); cast->kind = EX_CAST; cast->loc = value->loc; cast->type = type; cast->parse_index = value->parse_index;
    cast->as.cast.cast_type = type; cast->as.cast.value = value;
    return cast;
}

static CinderSymbol *scope_here(CinderScope *scope, const char *name) {
    for (size_t i = scope->symbols.len; i > 0U; --i)
        if (strcmp(scope->symbols.data[i - 1U].name, name) == 0) return &scope->symbols.data[i - 1U];
    return NULL;
}

static CinderType *sema_expr(CinderSema *sema, CinderExpr *expr, CinderScope *scope);

static bool register_object(const CinderExpr *expr, unsigned depth) {
    if (expr == NULL || depth >= 256U) return false;
    if (expr->kind == EX_NAME) return expr->resolved_decl != NULL && expr->resolved_decl->is_register;
    if (expr->kind == EX_MEMBER && !expr->as.member.arrow) return register_object(expr->as.member.base, depth + 1U);
    if (expr->kind == EX_GENERIC && expr->as.generic.selected < expr->as.generic.associations.len) return register_object(expr->as.generic.associations.data[expr->as.generic.selected].value, depth + 1U);
    return false;
}

static CinderType *sema_value(CinderSema *sema, CinderExpr **expression, CinderScope *scope) {
    CinderType *type = sema_expr(sema, *expression, scope);
    if (type->kind == TYPE_ARRAY || type->kind == TYPE_FUNCTION) {
        if (type->kind == TYPE_ARRAY && register_object(*expression, 0U)) cinder_diag(sema->diags, CINDER_ERROR, (*expression)->loc, "register array cannot be converted to a pointer");
        CinderExpr *decay = cinder_arena_alloc(&sema->ast->arena, sizeof(*decay), _Alignof(CinderExpr));
        memset(decay, 0, sizeof(*decay)); decay->kind = EX_DECAY; decay->loc = (*expression)->loc;
        decay->as.unary.value = *expression; decay->type = cinder_type_pointer(sema->types, type->kind == TYPE_ARRAY ? type->base : type);
        *expression = decay; return decay->type;
    }
    return type;
}

static bool null_constant(CinderSema *sema, const CinderExpr *expr) {
    if (!integer_type(expr->type)) return false;
    CinderDiagnostics temporary; cinder_diags_init(&temporary);
    CinderDiagnostics *saved = sema->ast->diags; sema->ast->diags = &temporary;
    int64_t value = 1; CinderType *type = NULL;
    bool zero = cinder_constant_integer(sema->ast, expr, &value, &type) && value == 0;
    sema->ast->diags = saved; cinder_diags_destroy(&temporary); return zero;
}
static bool assignment_compatible(CinderSema *sema, const CinderType *target, const CinderExpr *value) {
    return value_compatible(target, value->type) || (target->kind == TYPE_POINTER && null_constant(sema, value));
}
static bool pointer_step_type(const CinderType *type, size_t position) {
    return type->kind == TYPE_POINTER && type->base->complete && type->base->completion_index <= position && type->base->size != 0U && type->base->kind != TYPE_FUNCTION && type->base->kind != TYPE_VOID;
}

static bool string_array_initializer(CinderSema *sema, CinderDecl *decl) {
    CinderExpr *value = decl->initializer;
    if (value == NULL || value->kind != EX_STRING || decl->type->kind != TYPE_ARRAY || decl->type->base->kind != TYPE_CHAR) return false;
    (void)sema_expr(sema, value, &sema->globals);
    if (!decl->type->complete && decl->type->array_len == 0U) {
        decl->type->array_len = value->literal_length + 1U;
        decl->type->size = decl->type->array_len;
        decl->type->complete = true; decl->type->completion_index = decl->initializer_index;
    }
    if (decl->type->array_len < value->literal_length) cinder_diag(sema->diags, CINDER_ERROR, value->loc, "string initializer exceeds character array bound");
    return true;
}


static bool aggregate_type(const CinderType *type) { return type->kind == TYPE_ARRAY || type->kind == TYPE_STRUCT || type->kind == TYPE_UNION; }

static bool contains_const(const CinderType *type, unsigned depth) {
    if ((type->qualifiers & 1U) != 0U || depth >= 64U) return true;
    if (type->kind == TYPE_ARRAY) return contains_const(type->base, depth + 1U);
    if (type->kind == TYPE_STRUCT || type->kind == TYPE_UNION)
        for (size_t f = 0U; f < type->fields.len; ++f) if (contains_const(type->fields.data[f].type, depth + 1U)) return true;
    return false;
}

static void init_action(CinderDecl *decl, size_t offset, CinderType *type, CinderExpr *value) {
    for (size_t i = 0U; i < decl->init_actions.len; ++i) {
        CinderInitAction *previous = &decl->init_actions.data[i];
        if (previous->offset == offset && previous->type->size == type->size) { previous->value = NULL; previous->zero = false; }
    }
    CinderInitAction action = {offset, type, value, false}; cinder_vec_push((CinderVec *)&decl->init_actions, &action);
}

typedef struct { size_t begin; size_t end; } InitSpan;

static int compare_init_span(const void *left, const void *right) {
    const InitSpan *a = left, *b = right;
    return a->begin < b->begin ? -1 : a->begin > b->begin ? 1 : 0;
}

static void initializer_zero_gaps(CinderSema *sema, CinderDecl *decl, size_t first, CinderType *type, size_t offset) {
    CINDER_VEC_TYPE(InitSpan) spans = {NULL, 0U, 0U};
    CINDER_VEC_TYPE(CinderInitAction) zeros = {NULL, 0U, 0U};
    for (size_t a = first; a < decl->init_actions.len; ++a) {
        const CinderInitAction *action = &decl->init_actions.data[a];
        if (action->value == NULL && !action->zero) continue;
        InitSpan span = {action->offset, action->offset + action->type->size}; cinder_vec_push((CinderVec *)&spans, &span);
    }
    if (spans.len != 0U) qsort(spans.data, spans.len, sizeof(*spans.data), compare_init_span);
    size_t cursor = offset, end = offset + type->size;
    for (size_t a = 0U; a <= spans.len; ++a) {
        size_t next = a < spans.len ? spans.data[a].begin : end;
        if (cursor < next) {
            CinderInitAction zero = {cursor, cinder_type_array(sema->types, sema->types->char_type, next - cursor), NULL, true};
            cinder_vec_push((CinderVec *)&zeros, &zero);
        }
        if (a < spans.len && spans.data[a].end > cursor) cursor = spans.data[a].end;
    }
    size_t original = decl->init_actions.len;
    for (size_t z = 0U; z < zeros.len; ++z) cinder_vec_push((CinderVec *)&decl->init_actions, &zeros.data[z]);
    if (zeros.len != 0U) {
        memmove(decl->init_actions.data + first + zeros.len, decl->init_actions.data + first, (original - first) * sizeof(*decl->init_actions.data));
        memcpy(decl->init_actions.data + first, zeros.data, zeros.len * sizeof(*zeros.data));
    }
    free(zeros.data); free(spans.data);
}

static void sema_init_object(CinderSema *sema, CinderDecl *decl, CinderExpr **expression, CinderType *type, size_t offset, CinderScope *scope, unsigned nesting) {
    CinderExpr *expr = *expression;
    if (nesting >= 64U) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "initializer object nesting exceeds the profile limit"); return; }
    if (expr->kind != EX_INIT_LIST) {
        if (expr->kind == EX_STRING && type->kind == TYPE_ARRAY && type->base->kind == TYPE_CHAR) {
            if (!type->complete) { type->array_len = expr->literal_length + 1U; type->size = type->array_len; type->complete = true; type->completion_index = decl->initializer_index; }
            if (expr->literal_length > type->size) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "string initializer exceeds its array subobject");
        } else {
            (void)sema_value(sema, expression, scope); expr = *expression;
            if (!assignment_compatible(sema, type, expr)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "initializer has incompatible subobject type");
            else if (!aggregate_type(type)) *expression = convert_expr(sema, expr, type);
        }
        init_action(decl, offset, type, *expression); return;
    }
    expr->type = type;
    if (type->kind == TYPE_ARRAY && type->base->kind == TYPE_CHAR && expr->as.initializer.entries.len == 1U && expr->as.initializer.entries.data[0].designators.len == 0U && expr->as.initializer.entries.data[0].value->kind == EX_STRING) {
        sema_init_object(sema, decl, &expr->as.initializer.entries.data[0].value, type, offset, scope, nesting + 1U); return;
    }
    size_t first = decl->init_actions.len;
    if (aggregate_type(type) && type->complete) {
        for (size_t a = 0U; a < first; ++a) {
            CinderInitAction *old = &decl->init_actions.data[a];
            if (old->offset >= offset && old->offset - offset <= type->size && old->type->size <= type->size - (old->offset - offset)) { old->value = NULL; old->zero = false; }
        }
    }
    CinderInitFrame frames[64]; size_t depth = 1U; frames[0] = (CinderInitFrame){type, 0U, offset};
    bool inferred = type->kind == TYPE_ARRAY && !type->complete; size_t extent = 0U;
    for (size_t e = 0U; e < expr->as.initializer.entries.len; ++e) {
        CinderInitEntry *entry = &expr->as.initializer.entries.data[e];
        if (entry->designators.len != 0U) {
            depth = 1U; frames[0] = (CinderInitFrame){type, 0U, offset};
            for (size_t d = 0U; d < entry->designators.len; ++d) {
                CinderInitDesignator *designator = &entry->designators.data[d]; CinderInitFrame *frame = &frames[depth - 1U];
                if (designator->member != NULL && (frame->type->kind == TYPE_STRUCT || frame->type->kind == TYPE_UNION)) {
                    size_t field = 0U;
                    while (field < frame->type->fields.len && (frame->type->fields.data[field].name == NULL || strcmp(frame->type->fields.data[field].name, designator->member) != 0)) ++field;
                    if (field == frame->type->fields.len) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "initializer designator names no member"); break; }
                    if (field >= cinder_init_child_count(frame->type)) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "flexible array member cannot be initialized"); break; }
                    frame->index = field;
                } else if (designator->index != NULL && frame->type->kind == TYPE_ARRAY) {
                    int64_t index; CinderType *index_type;
                    (void)sema_expr(sema, designator->index, scope);
                    if (!cinder_constant_integer(sema->ast, designator->index, &index, &index_type) || index < 0 || (uint64_t)index >= cinder_init_child_count(frame->type) || frame->type->base->size == 0U || (uint64_t)index > 64U * 1024U * 1024U / frame->type->base->size) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "array designator requires an in-range constant index"); break; }
                    frame->index = (size_t)index;
                } else { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "initializer designator does not match its object"); break; }
                if (d + 1U < entry->designators.len) {
                    size_t child_offset; CinderType *child = cinder_init_child(*frame, &child_offset);
                    if (!aggregate_type(child) || depth == CINDER_ARRAY_LEN(frames)) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "initializer designator path is too deep or not an aggregate"); break; }
                    frames[depth++] = (CinderInitFrame){child, 0U, child_offset};
                }
            }
        }
        if (sema->diags->errors != 0U) return;
        if (frames[depth - 1U].index >= cinder_init_child_count(frames[depth - 1U].type)) { cinder_diag(sema->diags, CINDER_ERROR, entry->value->loc, "initializer has excess elements"); continue; }
        if (inferred && frames[0].index >= extent) extent = frames[0].index + 1U;
        size_t child_offset; CinderType *child = cinder_init_child(frames[depth - 1U], &child_offset);
        if (entry->value->kind != EX_INIT_LIST && !(entry->value->kind == EX_STRING && child->kind == TYPE_ARRAY && child->base->kind == TYPE_CHAR)) {
            (void)sema_expr(sema, entry->value, scope);
            while (aggregate_type(child) && !value_compatible(child, entry->value->type)) {
                if (depth == CINDER_ARRAY_LEN(frames) || cinder_init_child_count(child) == 0U || (!child->complete && child != type)) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "initializer requires a complete aggregate subobject"); return; }
                frames[depth++] = (CinderInitFrame){child, 0U, child_offset}; child = cinder_init_child(frames[depth - 1U], &child_offset);
            }
        }
        if (child_offset > 64U * 1024U * 1024U || child->size > 64U * 1024U * 1024U - child_offset) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "initializer exceeds the target object limit"); return; }
        sema_init_object(sema, decl, &entry->value, child, child_offset, scope, nesting + 1U); cinder_init_advance(frames, &depth);
    }
    if (inferred) {
        if (extent == 0U || type->base->size == 0U || extent > 64U * 1024U * 1024U / type->base->size) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "initializer cannot infer a bounded complete array"); return; }
        type->array_len = extent; type->size = extent * type->base->size; type->complete = true; type->completion_index = decl->initializer_index;
    }
    if (aggregate_type(type) && type->complete) initializer_zero_gaps(sema, decl, first, type, offset);
}

static CinderDecl *visible_linkage_declaration(CinderSema *sema, CinderScope *scope, const CinderDecl *decl) {
    for (CinderScope *current = scope; current != NULL && current != &sema->globals; current = current->parent) {
        CinderSymbol *prior = scope_here(current, decl->name);
        if (prior != NULL) return prior->decl;
    }
    CinderDecl *visible = NULL;
    for (size_t i = 0U; i < sema->ast->declarations.len; ++i) {
        CinderDecl *prior = sema->ast->declarations.data[i];
        if (prior->name != NULL && prior->parse_index < decl->parse_index && strcmp(prior->name, decl->name) == 0 && (visible == NULL || prior->parse_index > visible->parse_index)) visible = prior;
    }
    return visible;
}

static bool declaration_has_linkage(const CinderDecl *decl) {
    return decl != NULL && (decl->kind == DECL_FUNCTION || (decl->kind == DECL_VAR && (decl->is_extern || (decl->canonical != NULL && decl->storage_symbol == NULL))));
}

static void sema_local_decl(CinderSema *sema, CinderDecl *first, CinderScope *scope, CinderScope *parameter_scope) {
    for (CinderDecl *decl = first; decl != NULL; decl = decl->next) {
        if (decl->kind == DECL_TYPEDEF || decl->name == NULL) continue;
        if (decl->kind == DECL_VAR && !cinder_object_alignment_valid(decl->type, decl->alignment)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "object alignment is invalid or weaker than its type");
        if (decl->kind == DECL_FUNCTION) {
            CinderSymbol *global = cinder_scope_lookup(&sema->globals, decl->name);
            CinderDecl *prior = visible_linkage_declaration(sema, scope, decl);
            bool internal = declaration_has_linkage(prior) && (prior->canonical == NULL ? prior : prior->canonical)->is_static;
            if (decl->is_static) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "block-scope function declaration cannot be static");
            if (global != NULL && !cinder_type_compatible(global->type, decl->type)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "conflicting block-scope function declaration of '%s'", decl->name);
            if (global != NULL && global->decl->is_static != internal) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "conflicting linkage for '%s'", decl->name);
            if (global == NULL) global = scope_add(&sema->globals, decl->name, decl->type, decl, true);
            decl->canonical = global->is_function ? global->decl : decl;
            if (decl->canonical != NULL) decl->canonical->is_noreturn = decl->canonical->is_noreturn || decl->is_noreturn;
        }
        bool string_array = string_array_initializer(sema, decl);
        bool list = decl->initializer != NULL && decl->initializer->kind == EX_INIT_LIST;
        if (decl->kind == DECL_VAR && decl->is_static) {
            char name[64]; int length = snprintf(name, sizeof(name), ".LST.%zu", sema->ast->stored_objects.len);
            if (length < 0 || (size_t)length >= sizeof(name)) cinder_diag(sema->diags, CINDER_FATAL, decl->loc, "static object symbol exceeds the profile limit");
            else decl->storage_symbol = cinder_arena_strndup(&sema->ast->arena, name, (size_t)length);
            decl->canonical = decl; decl->has_definition = true;
            cinder_vec_push((CinderVec *)&sema->ast->stored_objects, &decl);
        }
        if (decl->kind == DECL_VAR && decl->is_extern) {
            if (decl->initializer != NULL) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "block-scope extern object cannot have an initializer");
            CinderDecl *prior = visible_linkage_declaration(sema, scope, decl);
            bool internal = declaration_has_linkage(prior) && (prior->canonical == NULL ? prior : prior->canonical)->is_static;
            CinderSymbol *linked = cinder_scope_lookup(&sema->globals, decl->name);
            if (linked == NULL) {
                linked = scope_add(&sema->globals, decl->name, decl->type, decl, false);
                decl->canonical = decl;
                cinder_vec_push((CinderVec *)&sema->ast->stored_objects, &decl);
            } else {
                if (linked->is_function || !cinder_type_compatible(linked->type, decl->type)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "conflicting block-scope extern object '%s'", decl->name);
                if (linked->decl->is_static != internal) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "conflicting linkage for '%s'", decl->name);
                decl->canonical = linked->decl == NULL ? decl : linked->decl;
                if (decl->canonical->canonical != NULL) decl->canonical = decl->canonical->canonical;
                if (cinder_type_compatible(linked->type, decl->type)) { decl->canonical->type = cinder_type_composite(sema->types, linked->type, decl->type); linked->type = decl->canonical->type; }
                if (decl->alignment != 0U && decl->canonical->alignment != 0U && decl->alignment != decl->canonical->alignment) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "conflicting object alignment declarations");
                if (decl->alignment > decl->canonical->alignment) decl->canonical->alignment = decl->alignment;
            }
        }
        if (decl->kind == DECL_VAR && !decl->is_extern && (decl->type->kind == TYPE_VOID || (!decl->declaration_complete && !string_array && !(list && decl->type->kind == TYPE_ARRAY && decl->type->base->complete)))) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "local object requires a complete object type");
        CinderSymbol *old = scope_here(scope, decl->name);
        bool compatible_extern = old != NULL && decl->kind == DECL_VAR && decl->is_extern && old->decl != NULL && old->decl->is_extern && cinder_type_compatible(old->type, decl->type);
        bool compatible_function = old != NULL && old->is_function && decl->kind == DECL_FUNCTION && cinder_type_compatible(old->type, decl->type);
        if ((old != NULL && !compatible_function && !compatible_extern) || (parameter_scope != NULL && scope_here(parameter_scope, decl->name) != NULL)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "redeclaration of '%s'", decl->name);
        else if (compatible_function || compatible_extern) old->type = cinder_type_composite(sema->types, old->type, decl->type);
        else scope_add(scope, decl->name, decl->type, decl, decl->kind == DECL_FUNCTION);
        if (list || (decl->initializer != NULL && (decl->type->kind == TYPE_STRUCT || decl->type->kind == TYPE_UNION))) sema_init_object(sema, decl, &decl->initializer, decl->type, 0U, scope, 0U);
        else if (decl->initializer != NULL && !string_array) {
            (void)sema_value(sema, &decl->initializer, scope);
            if (!assignment_compatible(sema, decl->type, decl->initializer)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "initializer for '%s' has incompatible type", decl->name);
            else decl->initializer = convert_expr(sema, decl->initializer, decl->type);
        }
    }
}

static int compare_cases(const void *left, const void *right) {
    const CinderStmt *a = *(CinderStmt *const *)left, *b = *(CinderStmt *const *)right;
    uint64_t av = (uint64_t)a->as.case_label.value, bv = (uint64_t)b->as.case_label.value;
    return av < bv ? -1 : av > bv;
}

static void sema_stmt(CinderSema *sema, CinderStmt *stmt, CinderScope *scope, CinderType *return_type, unsigned loop_depth, CinderStmt *enclosing_switch) {
    if (stmt == NULL) return;
    switch (stmt->kind) {
        case ST_EXPR: if (stmt->as.expr != NULL) (void)sema_value(sema, &stmt->as.expr, scope); break;
        case ST_RETURN:
            if (stmt->as.ret.value == NULL) { if (return_type->kind != TYPE_VOID) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "non-void function must return a value"); }
            else if ((void)sema_value(sema, &stmt->as.ret.value, scope), !assignment_compatible(sema, return_type, stmt->as.ret.value)) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "return expression is incompatible with %s", cinder_type_name(return_type));
            if (stmt->as.ret.value != NULL && assignment_compatible(sema, return_type, stmt->as.ret.value)) stmt->as.ret.value = convert_expr(sema, stmt->as.ret.value, return_type);
            break;
        case ST_BLOCK: {
            CinderScope child = { {NULL, 0U, 0U}, scope };
            for (size_t i = 0U; i < stmt->as.block.items.len; ++i) {
                CinderStmt *item = stmt->as.block.items.data[i];
                if (item->kind == ST_DECL) {
                    sema_local_decl(sema, item->as.decl, &child, stmt == sema->function_body ? scope : NULL);
                } else sema_stmt(sema, item, &child, return_type, loop_depth, enclosing_switch);
            }
            for (size_t i = 0U; i < child.symbols.len; ++i) free(child.symbols.data[i].name);
            free(child.symbols.data);
            break;
        }
        case ST_IF:
            if (!scalar_type(sema_value(sema, &stmt->as.if_stmt.condition, scope))) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "if condition must be scalar");
            sema_stmt(sema, stmt->as.if_stmt.then_branch, scope, return_type, loop_depth, enclosing_switch); sema_stmt(sema, stmt->as.if_stmt.else_branch, scope, return_type, loop_depth, enclosing_switch); break;
        case ST_WHILE:
        case ST_DO:
            if (!scalar_type(sema_value(sema, &stmt->as.loop.condition, scope))) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "loop condition must be scalar");
            sema_stmt(sema, stmt->as.loop.body, scope, return_type, loop_depth + 1U, enclosing_switch); break;
        case ST_FOR: {
            CinderScope child = {{NULL, 0U, 0U}, scope};
            if (stmt->as.for_stmt.init != NULL) {
                if (stmt->as.for_stmt.init->kind == ST_DECL) {
                    for (CinderDecl *decl = stmt->as.for_stmt.init->as.decl; decl != NULL; decl = decl->next) if (decl->kind != DECL_VAR || decl->is_static || decl->is_extern) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "for initializer requires an automatic object declaration");
                    sema_local_decl(sema, stmt->as.for_stmt.init->as.decl, &child, NULL);
                } else sema_stmt(sema, stmt->as.for_stmt.init, &child, return_type, loop_depth, enclosing_switch);
            }
            if (stmt->as.for_stmt.condition != NULL && !scalar_type(sema_value(sema, &stmt->as.for_stmt.condition, &child))) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "for condition must be scalar");
            if (stmt->as.for_stmt.step != NULL) (void)sema_expr(sema, stmt->as.for_stmt.step, &child);
            sema_stmt(sema, stmt->as.for_stmt.body, &child, return_type, loop_depth + 1U, enclosing_switch);
            for (size_t i = 0U; i < child.symbols.len; ++i) free(child.symbols.data[i].name);
            free(child.symbols.data);
            break;
        }
        case ST_SWITCH: {
            CinderType *type = sema_value(sema, &stmt->as.selection.control, scope);
            if (!integer_type(type)) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "switch condition must have integer type");
            else stmt->as.selection.control = convert_expr(sema, stmt->as.selection.control, cinder_integer_promote(sema->types, type));
            sema_stmt(sema, stmt->as.selection.body, scope, return_type, loop_depth, stmt);
            if (stmt->as.selection.cases.len > 1U) qsort(stmt->as.selection.cases.data, stmt->as.selection.cases.len, sizeof(*stmt->as.selection.cases.data), compare_cases);
            for (size_t i = 1U; i < stmt->as.selection.cases.len; ++i) if (stmt->as.selection.cases.data[i - 1U]->as.case_label.value == stmt->as.selection.cases.data[i]->as.case_label.value) cinder_diag(sema->diags, CINDER_ERROR, stmt->as.selection.cases.data[i]->loc, "duplicate case value after conversion to switch type");
            break;
        }
        case ST_CASE:
            (void)sema_expr(sema, stmt->as.case_label.expression, scope);
            if (enclosing_switch == NULL) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "case label requires an enclosing switch");
            else {
                CinderType *type = enclosing_switch->as.selection.control->type;
                if (integer_type(type)) {
                    stmt->as.case_label.expression = convert_expr(sema, stmt->as.case_label.expression, type);
                    int64_t value; CinderType *constant_type;
                    if (!cinder_constant_integer(sema->ast, stmt->as.case_label.expression, &value, &constant_type)) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "case label requires an integer constant expression");
                    else stmt->as.case_label.value = value;
                }
                if (enclosing_switch->as.selection.cases.len >= 4096U) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "switch case table exceeds the profile limit");
                else cinder_vec_push((CinderVec *)&enclosing_switch->as.selection.cases, &stmt);
            }
            sema_stmt(sema, stmt->as.case_label.body, scope, return_type, loop_depth, enclosing_switch); break;
        case ST_DEFAULT:
            if (enclosing_switch == NULL) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "default label requires an enclosing switch");
            else if (enclosing_switch->as.selection.default_label != NULL) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "duplicate default label in switch");
            else enclosing_switch->as.selection.default_label = stmt;
            sema_stmt(sema, stmt->as.case_label.body, scope, return_type, loop_depth, enclosing_switch); break;
        case ST_BREAK:
            if (loop_depth == 0U && enclosing_switch == NULL) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "break requires an enclosing loop or switch");
            break;
        case ST_CONTINUE:
            if (loop_depth == 0U) cinder_diag(sema->diags, CINDER_ERROR, stmt->loc, "continue requires an enclosing loop");
            break;
        case ST_EMPTY:
        case ST_DECL: break;
        case ST_LABEL: sema_stmt(sema, stmt->as.label.body, scope, return_type, loop_depth, enclosing_switch); break;
        case ST_GOTO: break;
    }
}

static CinderType *sema_expr(CinderSema *sema, CinderExpr *expr, CinderScope *scope) {
    if (expr == NULL) return sema->types->void_type;
    switch (expr->kind) {
        case EX_OFFSETOF:
            ++sema->unevaluated_depth;
            for (size_t i = 0U; i < expr->as.offset.path.len; ++i) {
                CinderExpr *index = expr->as.offset.path.data[i].index;
                if (index != NULL) (void)sema_expr(sema, index, scope);
            }
            --sema->unevaluated_depth;
            if (!cinder_offsetof_value(sema->ast, expr, &expr->as.offset.value)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "offsetof requires a complete aggregate and an addressable constant member designator");
            expr->type = sema->types->ulong_type; return expr->type;
        case EX_GENERIC: {
            ++sema->unevaluated_depth;
            CinderType *control = sema_value(sema, &expr->as.generic.control, scope);
            --sema->unevaluated_depth;
            size_t selected = cinder_generic_selection(control, expr);
            for (size_t i = 0U; i < expr->as.generic.associations.len; ++i) {
                CinderGenericAssociation *association = &expr->as.generic.associations.data[i];
                CinderType *type = association->type;
                if (type != NULL && (!type->complete || type->completion_index > association->parse_index || type->size == 0U || type->kind == TYPE_FUNCTION || type->kind == TYPE_VOID)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "generic association requires a complete object type");
                for (size_t j = 0U; j < i; ++j) {
                    CinderType *other = expr->as.generic.associations.data[j].type;
                    if ((type == NULL && other == NULL) || (type != NULL && other != NULL && cinder_type_compatible(type, other))) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "generic associations duplicate a default or compatible type");
                }
                if (i != selected) ++sema->unevaluated_depth;
                (void)sema_expr(sema, association->value, scope);
                if (i != selected) --sema->unevaluated_depth;
            }
            expr->as.generic.selected = selected;
            if (selected >= expr->as.generic.associations.len) {
                cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "generic selection has no unique matching association or default");
                expr->type = sema->types->error_type; return expr->type;
            }
            CinderExpr *value = expr->as.generic.associations.data[selected].value;
            expr->type = value->type; expr->is_lvalue = value->is_lvalue; return expr->type;
        }
        case EX_COMPOUND_LITERAL: {
            CinderDecl *decl = expr->as.compound_literal;
            if (sema->unevaluated_depth == 0U) decl->literal_evaluated = true;
            if (!decl->initializer_checked) {
                decl->initializer_checked = true;
                bool inferred = decl->type->kind == TYPE_ARRAY && !decl->type->complete && decl->type->base->complete;
                if (decl->type->kind == TYPE_VOID || decl->type->kind == TYPE_FUNCTION || (!decl->type->complete && !inferred)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "compound literal requires a complete object or inferable array type");
                else sema_init_object(sema, decl, &decl->initializer, decl->type, 0U, scope, 0U);
            }
            expr->type = decl->type; expr->is_lvalue = true; return expr->type;
        }
        case EX_INIT_LIST:
            if (expr->type == NULL) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "initializer list requires an object context"); expr->type = sema->types->error_type; }
            return expr->type;
        case EX_INT: case EX_CHAR: if (expr->type == NULL) expr->type = sema->types->int_type; expr->is_lvalue = false; return expr->type;
        case EX_FLOAT: if (expr->type == NULL) expr->type = sema->types->double_type; expr->is_lvalue = false; return expr->type;
        case EX_STRING: expr->is_lvalue = true; return expr->type;
        case EX_NAME: {
            CinderSymbol *symbol = cinder_scope_lookup(scope, expr->as.name);
            if (symbol == NULL || !expr->name_visible) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "use of undeclared identifier '%s'", expr->as.name); expr->type = sema->types->error_type; return expr->type; }
            if (expr->type == NULL) expr->type = symbol->type;
            expr->resolved_decl = symbol->decl;
            expr->is_lvalue = !symbol->is_function; return expr->type;
        }
        case EX_BINARY: {
            CinderType *left = sema_value(sema, &expr->as.binary.left, scope);
            CinderType *right = sema_value(sema, &expr->as.binary.right, scope);
            int op = expr->as.binary.op;
            if (op == ',') { expr->type = right; return right; }
            if (op == TOK_ANDAND || op == TOK_OROR) {
                if (!scalar_type(left) || !scalar_type(right)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "logical operator requires scalar operands");
                expr->type = sema->types->int_type; return expr->type;
            }
            if (left->kind == TYPE_POINTER || right->kind == TYPE_POINTER) {
                bool add = op == '+', sub = op == '-';
                if (add && integer_type(left) && right->kind == TYPE_POINTER) {
                    CinderExpr *swap = expr->as.binary.left; expr->as.binary.left = expr->as.binary.right; expr->as.binary.right = swap;
                    CinderType *swap_type = left; left = right; right = swap_type;
                }
                if ((add || sub) && pointer_step_type(left, expr->parse_index) && integer_type(right)) {
                    expr->as.binary.right = convert_expr(sema, expr->as.binary.right, cinder_integer_promote(sema->types, right)); expr->type = left; return left;
                }
                if (sub && pointer_step_type(left, expr->parse_index) && pointer_compatible(left, right)) { expr->type = sema->types->long_type; return expr->type; }
                bool compare = op == TOK_EQEQ || op == TOK_NEQ || op == '<' || op == '>' || op == TOK_LE || op == TOK_GE;
                bool equal = op == TOK_EQEQ || op == TOK_NEQ;
                if (compare && left->kind == TYPE_POINTER && right->kind == TYPE_POINTER && (pointer_compatible(left, right) || pointer_compatible(right, left)) && (equal || (pointer_step_type(left, expr->parse_index) && pointer_step_type(right, expr->parse_index)))) {
                    CinderType *common = pointer_compatible(left, right) ? left : right;
                    expr->as.binary.left = convert_expr(sema, expr->as.binary.left, common); expr->as.binary.right = convert_expr(sema, expr->as.binary.right, common);
                    expr->type = sema->types->int_type; return expr->type;
                }
                if (equal && ((left->kind == TYPE_POINTER && null_constant(sema, expr->as.binary.right)) || (right->kind == TYPE_POINTER && null_constant(sema, expr->as.binary.left)))) {
                    CinderType *common = left->kind == TYPE_POINTER ? left : right;
                    expr->as.binary.left = convert_expr(sema, expr->as.binary.left, common); expr->as.binary.right = convert_expr(sema, expr->as.binary.right, common);
                    expr->type = sema->types->int_type; return expr->type;
                }
                cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "invalid pointer operands"); expr->type = sema->types->error_type; return expr->type;
            }
            if (!numeric_type(left) || !numeric_type(right)) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "operator requires arithmetic operands"); expr->type = sema->types->error_type; return expr->type; }
            if (op == TOK_ANDAND || op == TOK_OROR) { expr->type = sema->types->int_type; return expr->type; }
            bool bits = op == '&' || op == '|' || op == '^' || op == '%' || op == TOK_SHL || op == TOK_SHR;
            if (bits && (!integer_type(left) || !integer_type(right))) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "operator requires integer operands");
            CinderType *common = cinder_arithmetic_type(sema->types, left, right);
            if (op == TOK_SHL || op == TOK_SHR) {
                common = cinder_integer_promote(sema->types, left);
                expr->as.binary.right = convert_expr(sema, expr->as.binary.right, cinder_integer_promote(sema->types, right));
            } else expr->as.binary.right = convert_expr(sema, expr->as.binary.right, common);
            expr->as.binary.left = convert_expr(sema, expr->as.binary.left, common);
            bool comparison = op == TOK_EQEQ || op == TOK_NEQ || op == '<' || op == '>' || op == TOK_LE || op == TOK_GE;
            expr->type = comparison ? sema->types->int_type : common; return expr->type;
        }
        case EX_UNARY: {
            int op = expr->as.unary.op;
            CinderType *value = op == '&' || op == TOK_PLUSPLUS || op == TOK_MINUSMINUS ? sema_expr(sema, expr->as.unary.value, scope) : sema_value(sema, &expr->as.unary.value, scope);
            bool update = op == TOK_PLUSPLUS || op == TOK_MINUSMINUS;
            if (op == '&' && register_object(expr->as.unary.value, 0U)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "cannot take the address of a register object or member");
            if ((op == '&' || update) && !expr->as.unary.value->is_lvalue && !(op == '&' && value->kind == TYPE_FUNCTION)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "unary operator requires an assignable lvalue");
            if (update && !numeric_type(value) && !pointer_step_type(value, expr->parse_index)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "increment requires arithmetic or a complete object pointer");
            if (update && (value->qualifiers & 1U) != 0U) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "increment cannot modify a const-qualified object");
            if (op == '&') expr->type = cinder_type_pointer(sema->types, value);
            else if (op == '*') {
                if (value->kind != TYPE_POINTER) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "cannot dereference a non-pointer");
                expr->type = value->kind == TYPE_POINTER ? value->base : sema->types->error_type; expr->is_lvalue = true;
            } else if (op == '!') { if (!scalar_type(value)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "logical negation requires a scalar"); expr->type = sema->types->int_type; }
            else if (update) expr->type = value;
            else {
                if (!numeric_type(value) || (op == '~' && !integer_type(value))) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "invalid operand type for unary operator");
                expr->type = integer_type(value) ? cinder_integer_promote(sema->types, value) : value;
                expr->as.unary.value = convert_expr(sema, expr->as.unary.value, expr->type);
            }
            return expr->type;
        }
        case EX_ASSIGN: {
            CinderType *target = sema_expr(sema, expr->as.assign.target, scope); CinderType *value = sema_value(sema, &expr->as.assign.value, scope);
            if (!expr->as.assign.target->is_lvalue || target->kind == TYPE_ARRAY || target->kind == TYPE_FUNCTION || contains_const(target, 0U)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "assignment requires a modifiable lvalue");
            if (expr->as.assign.op == '=' && !assignment_compatible(sema, target, expr->as.assign.value)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "assignment types are incompatible");
            if (expr->as.assign.op == '=') expr->as.assign.value = convert_expr(sema, expr->as.assign.value, target);
            else if (target->kind == TYPE_POINTER && pointer_step_type(target, expr->parse_index) && integer_type(value) && (expr->as.assign.op == TOK_PLUSEQ || expr->as.assign.op == TOK_MINUSEQ)) {
                expr->as.assign.operation_type = target; expr->as.assign.value = convert_expr(sema, expr->as.assign.value, cinder_integer_promote(sema->types, value));
            } else if (numeric_type(target) && numeric_type(value)) {
                bool shift = expr->as.assign.op == TOK_LSHIFT_EQ || expr->as.assign.op == TOK_RSHIFT_EQ;
                bool bits = shift || expr->as.assign.op == TOK_PERCENTEQ || expr->as.assign.op == TOK_ANDEQ || expr->as.assign.op == TOK_OREQ || expr->as.assign.op == TOK_XOREQ;
                if (bits && (!integer_type(target) || !integer_type(value))) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "compound operator requires integer operands");
                expr->as.assign.operation_type = shift ? cinder_integer_promote(sema->types, target) : cinder_arithmetic_type(sema->types, target, value);
                expr->as.assign.value = convert_expr(sema, expr->as.assign.value, shift ? cinder_integer_promote(sema->types, value) : expr->as.assign.operation_type);
            }
            if (expr->as.assign.op != '=' && expr->as.assign.operation_type == NULL) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "invalid compound assignment operands");
            expr->type = target; return target;
        }
        case EX_VA_ARG:
            if (!cinder_va_pointer_type(sema_value(sema, &expr->as.va_arg.list, scope))) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "va_arg requires a mutable va_list");
            expr->type = expr->as.va_arg.type;
            if (!expr->type->complete || expr->type->size == 0U || expr->type->kind == TYPE_ARRAY || expr->type->kind == TYPE_FUNCTION) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "va_arg requires a complete object type");
            return expr->type;
        case EX_CALL: {
            if (expr->as.call.callee->kind == EX_NAME) {
                const char *name = expr->as.call.callee->as.name;
                bool start = strcmp(name, "__cinder_va_start") == 0, copy = strcmp(name, "__cinder_va_copy") == 0, end = strcmp(name, "__cinder_va_end") == 0;
                if (start || copy || end) {
                    size_t count = end ? 1U : 2U;
                    if (expr->as.call.args.len != count) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "variadic builtin has incorrect arity");
                    else {
                        if (!cinder_va_pointer_type(sema_value(sema, &expr->as.call.args.data[0], scope))) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "variadic builtin requires a mutable va_list");
                        if (copy && !cinder_va_pointer_type(sema_value(sema, &expr->as.call.args.data[1], scope))) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "va_copy requires a va_list source");
                        if (start) {
                            CinderExpr *last = expr->as.call.args.data[1];
                            if (sema->function == NULL || !sema->function->type->variadic || sema->function->params.len == 0U || last->kind != EX_NAME || strcmp(last->as.name, sema->function->params.data[sema->function->params.len - 1U]->name) != 0) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "va_start requires the final named parameter of a variadic function");
                            else {
                                (void)sema_expr(sema, last, scope);
                                CinderDecl *parameter = sema->function->params.data[sema->function->params.len - 1U];
                                if (last->resolved_decl != parameter) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "va_start operand must resolve to the final parameter declaration");
                                CinderType *type = parameter->type;
                                bool promoted = type->kind == TYPE_BOOL || type->kind == TYPE_CHAR || type->kind == TYPE_SHORT || type->kind == TYPE_FLOAT;
                                if (sema->unevaluated_depth == 0U && (parameter->is_register || parameter->parameter_adjusted || promoted)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "va_start final parameter has register storage, adjusted array/function type, or default promotion");
                            }
                        }
                    }
                    expr->type = sema->types->void_type; return expr->type;
                }
            }
            CinderType *callee = sema_expr(sema, expr->as.call.callee, scope);
            if (callee->kind == TYPE_FUNCTION && expr->as.call.callee->kind != EX_NAME) callee = sema_value(sema, &expr->as.call.callee, scope);
            if (callee->kind != TYPE_FUNCTION) { if (callee->kind == TYPE_POINTER && callee->base->kind == TYPE_FUNCTION) callee = callee->base; else cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "called object is not a function"); }
            if (callee->kind == TYPE_FUNCTION) {
                if (expr->as.call.args.len < callee->params.len) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "too few arguments for function prototype");
                for (size_t i = 0U; i < expr->as.call.args.len; ++i) {
                    CinderType *arg = sema_value(sema, &expr->as.call.args.data[i], scope);
                    if (i < callee->params.len && !assignment_compatible(sema, callee->params.data[i].type, expr->as.call.args.data[i])) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "argument %zu has incompatible type", i + 1U);
                    else if (i >= callee->params.len && !callee->variadic) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "too many arguments for non-variadic function");
                    if (i < callee->params.len) expr->as.call.args.data[i] = convert_expr(sema, expr->as.call.args.data[i], callee->params.data[i].type);
                    else if (numeric_type(arg)) expr->as.call.args.data[i] = convert_expr(sema, expr->as.call.args.data[i], arg->kind == TYPE_FLOAT ? sema->types->double_type : integer_type(arg) ? cinder_integer_promote(sema->types, arg) : arg);
                }
                expr->type = callee->return_type;
            } else expr->type = sema->types->error_type;
            return expr->type;
        }
        case EX_CONDITIONAL: {
            if (!scalar_type(sema_value(sema, &expr->as.conditional.condition, scope))) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "conditional condition must be scalar");
            CinderType *yes = sema_value(sema, &expr->as.conditional.yes, scope); CinderType *no = sema_value(sema, &expr->as.conditional.no, scope);
            expr->type = numeric_type(yes) && numeric_type(no) ? cinder_arithmetic_type(sema->types, yes, no) : yes;
            if (yes->kind == TYPE_POINTER && null_constant(sema, expr->as.conditional.no)) expr->type = yes;
            else if (no->kind == TYPE_POINTER && null_constant(sema, expr->as.conditional.yes)) expr->type = no;
            else if (yes->kind == TYPE_POINTER && no->kind == TYPE_POINTER && pointer_compatible(no, yes)) expr->type = no;
            if (!assignment_compatible(sema, expr->type, expr->as.conditional.yes) || !assignment_compatible(sema, expr->type, expr->as.conditional.no)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "conditional arms have incompatible types");
            expr->as.conditional.yes = convert_expr(sema, expr->as.conditional.yes, expr->type);
            expr->as.conditional.no = convert_expr(sema, expr->as.conditional.no, expr->type);
            return expr->type;
        }
        case EX_CAST: {
            CinderType *from = sema_value(sema, &expr->as.cast.value, scope);
            CinderType *to = expr->as.cast.cast_type;
            if (to->kind != TYPE_VOID && !(numeric_type(from) && numeric_type(to)) && !(from->kind == TYPE_POINTER && (to->kind == TYPE_POINTER || integer_type(to))) && !(integer_type(from) && to->kind == TYPE_POINTER)) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "invalid cast between non-scalar types");
            expr->type = to; expr->is_lvalue = false; return to;
        }
        case EX_DECAY: return expr->type;
        case EX_INDEX: {
            CinderType *base = sema_value(sema, &expr->as.index.base, scope), *index = sema_value(sema, &expr->as.index.index, scope);
            if (integer_type(base) && index->kind == TYPE_POINTER) { CinderExpr *swap = expr->as.index.base; expr->as.index.base = expr->as.index.index; expr->as.index.index = swap; CinderType *t = base; base = index; index = t; }
            if (!pointer_step_type(base, expr->parse_index) || !integer_type(index)) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "subscript requires a complete object pointer and integer index"); expr->type = sema->types->error_type; }
            else { expr->type = base->base; expr->as.index.index = convert_expr(sema, expr->as.index.index, cinder_integer_promote(sema->types, index)); }
            expr->is_lvalue = true; return expr->type;
        }
        case EX_MEMBER: {
            CinderType *base = expr->as.member.arrow ? sema_value(sema, &expr->as.member.base, scope) : sema_expr(sema, expr->as.member.base, scope);
            if (expr->as.member.arrow) base = base->kind == TYPE_POINTER ? base->base : sema->types->error_type;
            if ((base->kind != TYPE_STRUCT && base->kind != TYPE_UNION) || (!base->complete || base->completion_index > expr->parse_index)) { cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "member access requires a complete structure or union"); expr->type = sema->types->error_type; return expr->type; }
            for (size_t field = 0U; field < base->fields.len; ++field) if (base->fields.data[field].name != NULL && strcmp(base->fields.data[field].name, expr->as.member.name) == 0) {
                expr->as.member.field = field; expr->type = cinder_type_qualified(sema->types, base->fields.data[field].type, base->fields.data[field].type->qualifiers | base->qualifiers);
                expr->is_lvalue = expr->as.member.arrow || expr->as.member.base->is_lvalue; return expr->type;
            }
            cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "unknown member '%s'", expr->as.member.name); expr->type = sema->types->error_type; return expr->type;
        }
        case EX_ALIGNOF: case EX_SIZEOF: {
            CinderType *queried = expr->queried_type;
            if (queried == NULL) {
                ++sema->unevaluated_depth;
                queried = sema_expr(sema, expr->as.unary.value, scope);
                --sema->unevaluated_depth;
            }
            if (queried == NULL || !queried->complete || queried->completion_index > expr->parse_index || queried->kind == TYPE_VOID || queried->kind == TYPE_FUNCTION) cinder_diag(sema->diags, CINDER_ERROR, expr->loc, "size/alignment requires a complete object type");
            expr->queried_type = queried; expr->type = sema->types->ulong_type; return expr->type;
        }
    }
    return sema->types->error_type;
}

/* Bound/enumerator expressions outlive their parser scopes. Each name carries
 * its source-point binding type; reconstruct only those visible bindings.
 * This validates unevaluated operands without admitting later declarations. */
static bool constant_scope(CinderExpr *expr, CinderScope *scope, unsigned depth) {
    if (expr == NULL) return true;
    if (depth >= 256U) return false;
    switch (expr->kind) {
        case EX_OFFSETOF:
            for (size_t i = 0U; i < expr->as.offset.path.len; ++i)
                if (!constant_scope(expr->as.offset.path.data[i].index, scope, depth + 1U)) return false;
            return true;
        case EX_GENERIC:
            if (!constant_scope(expr->as.generic.control, scope, depth + 1U)) return false;
            for (size_t i = 0U; i < expr->as.generic.associations.len; ++i)
                if (!constant_scope(expr->as.generic.associations.data[i].value, scope, depth + 1U)) return false;
            return true;
        case EX_NAME:
            if (expr->name_visible && expr->type != NULL && scope_here(scope, expr->as.name) == NULL)
                scope_add(scope, expr->as.name, expr->type, expr->resolved_decl, expr->type->kind == TYPE_FUNCTION);
            return true;
        case EX_BINARY: return constant_scope(expr->as.binary.left, scope, depth + 1U) && constant_scope(expr->as.binary.right, scope, depth + 1U);
        case EX_ASSIGN: return constant_scope(expr->as.assign.target, scope, depth + 1U) && constant_scope(expr->as.assign.value, scope, depth + 1U);
        case EX_UNARY: case EX_DECAY: return constant_scope(expr->as.unary.value, scope, depth + 1U);
        case EX_SIZEOF: case EX_ALIGNOF: return expr->queried_type != NULL || constant_scope(expr->as.unary.value, scope, depth + 1U);
        case EX_CAST: return constant_scope(expr->as.cast.value, scope, depth + 1U);
        case EX_VA_ARG: return constant_scope(expr->as.va_arg.list, scope, depth + 1U);
        case EX_CALL:
            if (!constant_scope(expr->as.call.callee, scope, depth + 1U)) return false;
            for (size_t i = 0U; i < expr->as.call.args.len; ++i)
                if (!constant_scope(expr->as.call.args.data[i], scope, depth + 1U)) return false;
            return true;
        case EX_CONDITIONAL: return constant_scope(expr->as.conditional.condition, scope, depth + 1U) && constant_scope(expr->as.conditional.yes, scope, depth + 1U) && constant_scope(expr->as.conditional.no, scope, depth + 1U);
        case EX_INDEX: return constant_scope(expr->as.index.base, scope, depth + 1U) && constant_scope(expr->as.index.index, scope, depth + 1U);
        case EX_MEMBER: return constant_scope(expr->as.member.base, scope, depth + 1U);
        case EX_COMPOUND_LITERAL: return constant_scope(expr->as.compound_literal->initializer, scope, depth + 1U);
        case EX_INIT_LIST:
            for (size_t i = 0U; i < expr->as.initializer.entries.len; ++i) {
                CinderInitEntry *entry = &expr->as.initializer.entries.data[i];
                if (!constant_scope(entry->value, scope, depth + 1U)) return false;
                for (size_t d = 0U; d < entry->designators.len; ++d)
                    if (!constant_scope(entry->designators.data[d].index, scope, depth + 1U)) return false;
            }
            return true;
        case EX_INT: case EX_FLOAT: case EX_CHAR: case EX_STRING: return true;
    }
    return false;
}

static void sema_constants(CinderSema *sema) {
    for (size_t i = 0U; i < sema->ast->constant_exprs.len; ++i) {
        CinderConstantExpr *constant = &sema->ast->constant_exprs.data[i];
        CinderScope scope = {{NULL, 0U, 0U}, NULL};
        if (!constant_scope(constant->expression, &scope, 0U)) cinder_diag(sema->diags, CINDER_ERROR, constant->expression->loc, "constant expression nesting exceeds the profile limit");
        else {
            sema->function = constant->function;
            (void)sema_expr(sema, constant->expression, &scope);
            sema->function = NULL;
            int64_t value; CinderType *type;
            if (sema->diags->errors == 0U && (!cinder_constant_integer(sema->ast, constant->expression, &value, &type) || value != constant->value)) cinder_diag(sema->diags, CINDER_ERROR, constant->expression->loc, "constant expression disagrees after semantic typing");
        }
        for (size_t n = 0U; n < scope.symbols.len; ++n) free(scope.symbols.data[n].name);
        free(scope.symbols.data);
    }
}

int cinder_sema_run(CinderSema *sema) {
    /* Initializer bounds belong to the definition's source point. Build a
     * temporary lookup table before composing later redeclarations. */
    for (size_t i = 0U; i < sema->ast->declarations.len; ++i) {
        CinderDecl *decl = sema->ast->declarations.data[i];
        if (decl->kind != DECL_TYPEDEF && decl->name != NULL && scope_here(&sema->globals, decl->name) == NULL) scope_add(&sema->globals, decl->name, decl->type, decl, decl->kind == DECL_FUNCTION);
    }
    for (size_t i = 0U; i < sema->ast->declarations.len; ++i) {
        CinderDecl *decl = sema->ast->declarations.data[i];
        if (decl->kind == DECL_VAR) (void)string_array_initializer(sema, decl);
        if (decl->kind == DECL_VAR && decl->initializer != NULL && decl->initializer->kind == EX_INIT_LIST) sema_init_object(sema, decl, &decl->initializer, decl->type, 0U, &sema->globals, 0U);
    }
    for (size_t i = 0U; i < sema->ast->static_literals.len; ++i) {
        CinderDecl *decl = sema->ast->static_literals.data[i];
        CinderExpr literal; memset(&literal, 0, sizeof(literal)); literal.kind = EX_COMPOUND_LITERAL; literal.loc = decl->loc; literal.as.compound_literal = decl;
        (void)sema_expr(sema, &literal, &sema->globals);
    }
    for (size_t i = 0U; i < sema->globals.symbols.len; ++i) free(sema->globals.symbols.data[i].name);
    free(sema->globals.symbols.data); sema->globals.symbols.data = NULL; sema->globals.symbols.len = 0U; sema->globals.symbols.cap = 0U;
    for (size_t i = 0U; i < sema->ast->declarations.len; ++i) {
        CinderDecl *decl = sema->ast->declarations.data[i];
        if (decl->kind == DECL_TYPEDEF || decl->name == NULL) continue;
        if (decl->kind == DECL_VAR && decl->type->kind == TYPE_VOID && (!decl->is_extern || decl->initializer != NULL)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "global definition cannot have void type");
        if (decl->kind == DECL_VAR && decl->is_static && !decl->is_extern && decl->initializer == NULL && !decl->declaration_complete) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "internal tentative definition requires a complete type");
        CinderSymbol *old = cinder_scope_lookup(&sema->globals, decl->name);
        if (old != NULL && !cinder_type_compatible(old->type, decl->type)) { cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "conflicting declaration of '%s'", decl->name); continue; }
        CinderDecl *canonical = old == NULL ? decl : old->decl;
        decl->canonical = canonical;
        if (decl->kind == DECL_FUNCTION) canonical->is_noreturn = canonical->is_noreturn || decl->is_noreturn;
        if (decl->kind == DECL_VAR) {
            if (!cinder_object_alignment_valid(decl->type, decl->alignment)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "object alignment is invalid or weaker than its type");
            if (decl->alignment != 0U && canonical->alignment != 0U && decl->alignment != canonical->alignment) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "conflicting object alignment declarations");
            if (decl->alignment > canonical->alignment) canonical->alignment = decl->alignment;
        }
        bool definition = decl->kind == DECL_FUNCTION ? decl->is_definition : decl->initializer != NULL;
        if (old == NULL) {
            scope_add(&sema->globals, decl->name, decl->type, decl, decl->kind == DECL_FUNCTION);
            canonical->emission = decl;
        } else {
            if ((decl->is_static && !canonical->is_static) || (decl->kind == DECL_VAR && canonical->is_static && !decl->is_static && !decl->is_extern)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "conflicting linkage for '%s'", decl->name);
            if (definition && canonical->has_definition) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "multiple definitions of '%s'", decl->name);
            canonical->type = cinder_type_composite(sema->types, canonical->type, decl->type); old->type = canonical->type;
            if (definition) canonical->emission = decl;
            if (decl->kind == DECL_FUNCTION && canonical->is_static) decl->is_static = true;
        }
        canonical->has_definition = canonical->has_definition || definition;
        canonical->tentative = canonical->tentative || (decl->kind == DECL_VAR && !decl->is_extern && decl->initializer == NULL);
    }
    for (size_t i = 0U; i < sema->ast->declarations.len; ++i) {
        CinderDecl *decl = sema->ast->declarations.data[i];
        if (decl->kind == DECL_TYPEDEF || decl->name == NULL) continue;
        bool string_array = string_array_initializer(sema, decl);
        if (decl->initializer != NULL && decl->initializer->kind == EX_INIT_LIST) { /* Planned before declaration composition. */ }
        else if (decl->initializer != NULL && !string_array) {
            (void)sema_value(sema, &decl->initializer, &sema->globals);
            if (!assignment_compatible(sema, decl->type, decl->initializer)) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "global initializer for '%s' has incompatible type", decl->name);
        }
        if (decl->body != NULL) {
            CinderScope scope = { {NULL, 0U, 0U}, &sema->globals };
            for (size_t p = 0U; p < decl->params.len; ++p) scope_add(&scope, decl->params.data[p]->name, decl->params.data[p]->type, decl->params.data[p], false);
            sema->function_body = decl->body;
            sema->function = decl;
            sema_stmt(sema, decl->body, &scope, decl->type->return_type, 0U, NULL);
            CinderControlMap control; cinder_control_init(&control);
            (void)cinder_control_build(&control, decl->body, sema->diags); cinder_control_destroy(&control);
            sema->function_body = NULL;
            sema->function = NULL;
            for (size_t p = 0U; p < scope.symbols.len; ++p) free(scope.symbols.data[p].name);
            free(scope.symbols.data);
        }
    }
    for (size_t i = 0U; i < sema->ast->declarations.len; ++i) {
        CinderDecl *decl = sema->ast->declarations.data[i];
        if (decl->kind == DECL_VAR && (!decl->is_extern || decl->initializer != NULL) && decl->canonical != NULL && decl->canonical->alignment != 0U && !decl->has_alignment) cinder_diag(sema->diags, CINDER_ERROR, decl->loc, "aligned object definition must specify its alignment");
    }
    sema_constants(sema);
    for (size_t i = 0U; i < sema->globals.symbols.len; ++i) {
        CinderSymbol *symbol = &sema->globals.symbols.data[i]; CinderDecl *decl = symbol->decl;
        if (decl->kind == DECL_VAR && decl->tentative && !decl->has_definition && !decl->type->complete && decl->type->kind == TYPE_ARRAY && decl->type->base->complete) {
            decl->type = cinder_type_array(sema->types, decl->type->base, 1U); symbol->type = decl->type;
        }
    }
    return sema->diags->errors == 0U ? 0 : 1;
}
