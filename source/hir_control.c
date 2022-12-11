#include "hir_private.h"

#include <stdlib.h>
#include <string.h>

static size_t memo_index(const CinderHIR *hir, const void *source) {
    size_t index = (size_t)((uintptr_t)source >> 4U) & (hir->memo_capacity - 1U);
    while (hir->memo[index].source != NULL && hir->memo[index].source != source) index = (index + 1U) & (hir->memo_capacity - 1U);
    return index;
}

static CinderHIRMemo *memo(CinderHIR *hir, const void *source) {
    if (hir->memo_capacity == 0U || hir->memo_count >= hir->memo_capacity / 2U) {
        size_t capacity = hir->memo_capacity == 0U ? 256U : hir->memo_capacity * 2U;
        CinderHIRMemo *old = hir->memo; size_t old_capacity = hir->memo_capacity;
        hir->memo = cinder_alloc(capacity * sizeof(*hir->memo)); memset(hir->memo, 0, capacity * sizeof(*hir->memo)); hir->memo_capacity = capacity;
        for (size_t i = 0U; i < old_capacity; ++i) if (old[i].source != NULL) hir->memo[memo_index(hir, old[i].source)] = old[i];
        free(old);
    }
    CinderHIRMemo *entry = &hir->memo[memo_index(hir, source)];
    if (entry->source == NULL) { entry->source = source; ++hir->memo_count; }
    return entry;
}

static char *name(CinderHIR *hir, const char *text) { return text == NULL ? NULL : cinder_arena_strndup(&hir->arena, text, strlen(text)); }
static void *array(CinderHIR *hir, size_t count, size_t width, size_t alignment) {
    if (count == 0U) return NULL;
    if (count > SIZE_MAX / width) abort();
    return cinder_arena_alloc(&hir->arena, count * width, alignment);
}

CinderHIRDecl *cinder_hir_declaration(CinderHIR *hir, const CinderDecl *source, unsigned depth, CinderDiagnostics *diags) {
    if (source == NULL) return NULL;
    CinderHIRMemo *entry = memo(hir, source); if (entry->owned != NULL) return entry->owned;
    if (depth >= 1024U || hir->declarations.len >= 1000000U) { cinder_diag(diags, CINDER_ERROR, source->loc, "typed HIR declaration limit exceeded"); return NULL; }
    CinderHIRDecl *decl = cinder_arena_alloc(&hir->arena, sizeof(*decl), _Alignof(CinderHIRDecl)); memset(decl, 0, sizeof(*decl));
    decl->id = (uint32_t)hir->declarations.len; entry->owned = decl; cinder_vec_push((CinderVec *)&hir->declarations, &decl);
    decl->kind = source->kind; decl->loc = source->loc; decl->name = name(hir, source->name); decl->storage_symbol = name(hir, source->storage_symbol); decl->type = source->type;
    decl->is_static = source->is_static; decl->is_extern = source->is_extern; decl->is_definition = source->is_definition; decl->is_noreturn = source->is_noreturn;
    decl->literal_evaluated = source->literal_evaluated; decl->alignment = source->alignment; decl->lowering_slot = -1;
    decl->has_initializer = source->initializer != NULL; decl->initializer_list = source->initializer != NULL && source->initializer->kind == EX_INIT_LIST;
    if (!decl->initializer_list) decl->initializer = cinder_hir_expression(hir, source->initializer, true, 0U, diags);
    decl->params.data = array(hir, source->params.len, sizeof(*decl->params.data), _Alignof(CinderHIRDecl *)); decl->params.len = decl->params.cap = source->params.len;
    for (size_t p = 0U; p < source->params.len; ++p) decl->params.data[p] = cinder_hir_declaration(hir, source->params.data[p], depth + 1U, diags);
    size_t active = 0U;
    for (size_t a = 0U; a < source->init_actions.len; ++a) if (source->init_actions.data[a].zero || source->init_actions.data[a].value != NULL) ++active;
    decl->init_actions.data = array(hir, active, sizeof(*decl->init_actions.data), _Alignof(CinderHIRInitAction)); decl->init_actions.cap = active;
    for (size_t a = 0U; a < source->init_actions.len; ++a) {
        const CinderInitAction *old = &source->init_actions.data[a];
        if (!old->zero && old->value == NULL) continue;
        CinderHIRInitAction *action = &decl->init_actions.data[decl->init_actions.len++];
        action->offset = old->offset; action->type = old->type; action->zero = old->zero; action->bit_offset = old->bit_offset; action->bit_width = old->bit_width;
        action->value = cinder_hir_expression(hir, old->value, true, 0U, diags);
    }
    decl->canonical = cinder_hir_declaration(hir, source->canonical, depth + 1U, diags);
    decl->next = cinder_hir_declaration(hir, source->next, depth + 1U, diags); return decl;
}

static CinderHIRStmt *statement(CinderHIR *hir, const CinderStmt *source, unsigned depth, CinderDiagnostics *diags) {
    if (source == NULL) return NULL;
    CinderHIRMemo *entry = memo(hir, source); if (entry->owned != NULL) return entry->owned;
    if (depth >= 1024U || hir->statements.len >= 1000000U) { cinder_diag(diags, CINDER_ERROR, source->loc, "typed HIR statement limit exceeded"); return NULL; }
    CinderHIRStmt *stmt = cinder_arena_alloc(&hir->arena, sizeof(*stmt), _Alignof(CinderHIRStmt)); memset(stmt, 0, sizeof(*stmt));
    stmt->id = (uint32_t)hir->statements.len; entry->owned = stmt; cinder_vec_push((CinderVec *)&hir->statements, &stmt);
    stmt->kind = source->kind; stmt->loc = source->loc; stmt->control_scope = source->control_scope;
    stmt->literal_objects.data = array(hir, source->literal_objects.len, sizeof(*stmt->literal_objects.data), _Alignof(CinderHIRDecl *)); stmt->literal_objects.len = stmt->literal_objects.cap = source->literal_objects.len;
    for (size_t i = 0U; i < source->literal_objects.len; ++i) stmt->literal_objects.data[i] = cinder_hir_declaration(hir, source->literal_objects.data[i], 0U, diags);
    switch (source->kind) {
        case ST_EXPR: stmt->as.expr = cinder_hir_expression(hir, source->as.expr, true, 0U, diags); break;
        case ST_RETURN: stmt->as.ret.value = cinder_hir_expression(hir, source->as.ret.value, true, 0U, diags); break;
        case ST_DECL: stmt->as.decl = cinder_hir_declaration(hir, source->as.decl, 0U, diags); break;
        case ST_BLOCK:
            stmt->as.block.items.data = array(hir, source->as.block.items.len, sizeof(*stmt->as.block.items.data), _Alignof(CinderHIRStmt *)); stmt->as.block.items.len = stmt->as.block.items.cap = source->as.block.items.len;
            for (size_t i = 0U; i < source->as.block.items.len; ++i) stmt->as.block.items.data[i] = statement(hir, source->as.block.items.data[i], depth + 1U, diags);
            break;
        case ST_IF:
            stmt->as.if_stmt.condition = cinder_hir_expression(hir, source->as.if_stmt.condition, true, 0U, diags);
            stmt->as.if_stmt.then_branch = statement(hir, source->as.if_stmt.then_branch, depth + 1U, diags); stmt->as.if_stmt.else_branch = statement(hir, source->as.if_stmt.else_branch, depth + 1U, diags); break;
        case ST_WHILE: case ST_DO:
            stmt->as.loop.condition = cinder_hir_expression(hir, source->as.loop.condition, true, 0U, diags); stmt->as.loop.body = statement(hir, source->as.loop.body, depth + 1U, diags); break;
        case ST_FOR:
            stmt->as.for_stmt.init = statement(hir, source->as.for_stmt.init, depth + 1U, diags); stmt->as.for_stmt.condition = cinder_hir_expression(hir, source->as.for_stmt.condition, true, 0U, diags);
            stmt->as.for_stmt.step = cinder_hir_expression(hir, source->as.for_stmt.step, true, 0U, diags); stmt->as.for_stmt.body = statement(hir, source->as.for_stmt.body, depth + 1U, diags); break;
        case ST_LABEL: stmt->as.label.name = name(hir, source->as.label.name); stmt->as.label.body = statement(hir, source->as.label.body, depth + 1U, diags); break;
        case ST_GOTO: stmt->as.jump.name = name(hir, source->as.jump.name); stmt->as.jump.target = statement(hir, source->as.jump.target, depth + 1U, diags); break;
        case ST_SWITCH:
            stmt->as.selection.control = cinder_hir_expression(hir, source->as.selection.control, true, 0U, diags); stmt->as.selection.body = statement(hir, source->as.selection.body, depth + 1U, diags);
            stmt->as.selection.cases.data = array(hir, source->as.selection.cases.len, sizeof(*stmt->as.selection.cases.data), _Alignof(CinderHIRStmt *)); stmt->as.selection.cases.len = stmt->as.selection.cases.cap = source->as.selection.cases.len;
            for (size_t i = 0U; i < source->as.selection.cases.len; ++i) stmt->as.selection.cases.data[i] = statement(hir, source->as.selection.cases.data[i], depth + 1U, diags);
            stmt->as.selection.default_label = statement(hir, source->as.selection.default_label, depth + 1U, diags); break;
        case ST_CASE: case ST_DEFAULT: stmt->as.case_label.value = source->as.case_label.value; stmt->as.case_label.body = statement(hir, source->as.case_label.body, depth + 1U, diags); break;
        case ST_EMPTY: case ST_BREAK: case ST_CONTINUE: break;
    }
    return stmt;
}

static void control(CinderHIR *hir, CinderHIRDecl *decl, const CinderControlMap *source, CinderDiagnostics *diags) {
    decl->control.scopes.data = array(hir, source->scopes.len, sizeof(*decl->control.scopes.data), _Alignof(CinderHIRControlScope)); decl->control.scopes.len = decl->control.scopes.cap = source->scopes.len;
    for (size_t s = 0U; s < source->scopes.len; ++s) {
        const CinderControlScope *old = &source->scopes.data[s]; CinderHIRControlScope *scope = &decl->control.scopes.data[s];
        scope->owner = statement(hir, old->owner, 0U, diags); scope->parent = old->parent;
        scope->objects.data = array(hir, old->objects.len, sizeof(*scope->objects.data), _Alignof(CinderHIRDecl *)); scope->objects.len = scope->objects.cap = old->objects.len;
        for (size_t i = 0U; i < old->objects.len; ++i) scope->objects.data[i] = cinder_hir_declaration(hir, old->objects.data[i], 0U, diags);
    }
    decl->control.labels.data = array(hir, source->labels.len, sizeof(*decl->control.labels.data), _Alignof(CinderHIRStmt *)); decl->control.labels.len = decl->control.labels.cap = source->labels.len;
    for (size_t i = 0U; i < source->labels.len; ++i) decl->control.labels.data[i] = statement(hir, source->labels.data[i], 0U, diags);
    decl->control.cases.data = array(hir, source->cases.len, sizeof(*decl->control.cases.data), _Alignof(CinderHIRStmt *)); decl->control.cases.len = decl->control.cases.cap = source->cases.len;
    for (size_t i = 0U; i < source->cases.len; ++i) decl->control.cases.data[i] = statement(hir, source->cases.data[i], 0U, diags);
}

static void seal_type_names(CinderTypeContext *types) {
    for (size_t t = 0U; t < types->all_types.len; ++t) {
        CinderType *type = types->all_types.data[t];
        if (type->tag != NULL) type->tag = cinder_arena_strndup(&types->arena, type->tag, strlen(type->tag));
        for (size_t p = 0U; p < type->params.len; ++p) if (type->params.data[p].name != NULL) type->params.data[p].name = cinder_arena_strndup(&types->arena, type->params.data[p].name, strlen(type->params.data[p].name));
        for (size_t f = 0U; f < type->fields.len; ++f) if (type->fields.data[f].name != NULL) type->fields.data[f].name = cinder_arena_strndup(&types->arena, type->fields.data[f].name, strlen(type->fields.data[f].name));
    }
}

int cinder_hir_control_unit(CinderHIR *hir, CinderAst *ast, CinderDiagnostics *diags) {
    unsigned before = diags->errors;
    for (size_t i = 0U; i < ast->declarations.len; ++i) {
        CinderDecl *source = ast->declarations.data[i]; CinderHIRDecl *decl = cinder_hir_declaration(hir, source, 0U, diags); cinder_vec_push((CinderVec *)&hir->roots, &decl);
        if (decl != NULL && source->kind == DECL_FUNCTION && source->is_definition) {
            CinderControlMap map; cinder_control_init(&map);
            if (cinder_control_build(&map, source->body, diags)) { decl->body = statement(hir, source->body, 0U, diags); control(hir, decl, &map, diags); }
            cinder_control_destroy(&map);
        }
    }
    for (size_t i = 0U; i < ast->static_literals.len; ++i) (void)cinder_hir_declaration(hir, ast->static_literals.data[i], 0U, diags);
    for (size_t i = 0U; i < ast->stored_objects.len; ++i) (void)cinder_hir_declaration(hir, ast->stored_objects.data[i], 0U, diags);
    hir->static_objects = cinder_alloc(sizeof(*hir->static_objects)); cinder_ir_init(hir->static_objects, hir->types);
    if (diags->errors == before) (void)cinder_prepare_static_objects(hir->static_objects, ast, diags);
    seal_type_names(hir->types); return diags->errors == before ? 0 : 1;
}

static bool declaration_owned(const CinderHIR *hir, const CinderHIRDecl *decl) { return decl != NULL && decl->id < hir->declarations.len && hir->declarations.data[decl->id] == decl; }
static bool statement_owned(const CinderHIR *hir, const CinderHIRStmt *stmt) { return stmt != NULL && stmt->id < hir->statements.len && hir->statements.data[stmt->id] == stmt; }
static bool expression_owned(const CinderHIR *hir, const CinderHIRExpr *expr) { return expr != NULL && expr->id < hir->expressions.len && hir->expressions.data[expr->id] == expr; }

static size_t children(const CinderHIRStmt *stmt, CinderHIRStmt **out) {
    size_t count = 0U;
    switch (stmt->kind) {
        case ST_IF: if (stmt->as.if_stmt.then_branch != NULL) out[count++] = stmt->as.if_stmt.then_branch; if (stmt->as.if_stmt.else_branch != NULL) out[count++] = stmt->as.if_stmt.else_branch; break;
        case ST_WHILE: case ST_DO: if (stmt->as.loop.body != NULL) out[count++] = stmt->as.loop.body; break;
        case ST_FOR: if (stmt->as.for_stmt.init != NULL) out[count++] = stmt->as.for_stmt.init; if (stmt->as.for_stmt.body != NULL) out[count++] = stmt->as.for_stmt.body; break;
        case ST_LABEL: if (stmt->as.label.body != NULL) out[count++] = stmt->as.label.body; break;
        case ST_CASE: case ST_DEFAULT: if (stmt->as.case_label.body != NULL) out[count++] = stmt->as.case_label.body; break;
        case ST_SWITCH: if (stmt->as.selection.body != NULL) out[count++] = stmt->as.selection.body; break;
        case ST_EMPTY: case ST_EXPR: case ST_BLOCK: case ST_RETURN: case ST_BREAK: case ST_CONTINUE: case ST_DECL: case ST_GOTO: break;
    }
    return count;
}

static bool tree_acyclic(const CinderHIR *hir) {
    size_t count = hir->statements.len;
    unsigned char *state = cinder_alloc(count); memset(state, 0, count);
    typedef struct { size_t node; size_t next; } Frame;
    Frame *stack = cinder_alloc(count * sizeof(*stack)); bool valid = true;
    for (size_t root = 0U; root < count && valid; ++root) {
        if (state[root] != 0U) continue;
        size_t depth = 1U; stack[0] = (Frame){root, 0U}; state[root] = 1U;
        while (depth != 0U && valid) {
            Frame *frame = &stack[depth - 1U]; CinderHIRStmt *stmt = hir->statements.data[frame->node], *small[2]; size_t n = children(stmt, small);
            size_t total = stmt->kind == ST_BLOCK ? stmt->as.block.items.len : n;
            if (frame->next == total) { state[frame->node] = 2U; --depth; continue; }
            CinderHIRStmt *child = stmt->kind == ST_BLOCK ? stmt->as.block.items.data[frame->next++] : small[frame->next++];
            if (state[child->id] == 1U) { valid = false; break; }
            if (state[child->id] == 0U) { state[child->id] = 1U; stack[depth++] = (Frame){child->id, 0U}; }
        }
    }
    free(state); free(stack); return valid;
}

int cinder_verify_hir_control(const CinderHIR *hir, CinderDiagnostics *diags) {
    unsigned before = diags->errors;
    for (size_t d = 0U; d < hir->declarations.len; ++d) {
        const CinderHIRDecl *decl = hir->declarations.data[d];
        if (decl->id != d || decl->type == NULL || decl->kind < DECL_VAR || decl->kind > DECL_TYPEDEF || (decl->canonical != NULL && !declaration_owned(hir, decl->canonical)) || (decl->next != NULL && !declaration_owned(hir, decl->next))) cinder_diag(diags, CINDER_FATAL, decl->loc, "invalid owned HIR declaration");
        if (decl->type == NULL) continue;
        if (decl->initializer != NULL && !expression_owned(hir, decl->initializer)) cinder_diag(diags, CINDER_FATAL, decl->loc, "HIR initializer expression is not owned");
        for (size_t a = 0U; a < decl->init_actions.len; ++a) {
            const CinderHIRInitAction *action = &decl->init_actions.data[a];
            size_t extent = action->bit_width != 0U ? ((size_t)action->bit_offset + action->bit_width + 7U) / 8U : action->type == NULL ? 0U : action->type->size;
            if (action->type == NULL || action->offset > decl->type->size || extent > decl->type->size - action->offset || (!action->zero && !expression_owned(hir, action->value))) cinder_diag(diags, CINDER_FATAL, decl->loc, "invalid typed HIR initializer action");
        }
        for (size_t p = 0U; p < decl->params.len; ++p) if (!declaration_owned(hir, decl->params.data[p])) cinder_diag(diags, CINDER_FATAL, decl->loc, "HIR parameter is not an owned declaration");
        if (decl->body != NULL && !statement_owned(hir, decl->body)) cinder_diag(diags, CINDER_FATAL, decl->loc, "HIR body is not an owned statement");
        for (size_t s = 0U; s < decl->control.scopes.len; ++s) {
            const CinderHIRControlScope *scope = &decl->control.scopes.data[s];
            if (!statement_owned(hir, scope->owner) || scope->parent < -1 || (scope->parent >= 0 && (size_t)scope->parent >= s)) cinder_diag(diags, CINDER_FATAL, decl->loc, "invalid HIR scope parent or owner");
            for (size_t o = 0U; o < scope->objects.len; ++o) if (!declaration_owned(hir, scope->objects.data[o])) cinder_diag(diags, CINDER_FATAL, decl->loc, "HIR scope storage is not owned");
        }
        for (size_t l = 0U; l < decl->control.labels.len; ++l) if (!statement_owned(hir, decl->control.labels.data[l]) || decl->control.labels.data[l]->kind != ST_LABEL || decl->control.labels.data[l]->as.label.name == NULL) cinder_diag(diags, CINDER_FATAL, decl->loc, "HIR label table has an invalid owned label");
        for (size_t c = 0U; c < decl->control.cases.len; ++c) if (!statement_owned(hir, decl->control.cases.data[c]) || (decl->control.cases.data[c]->kind != ST_CASE && decl->control.cases.data[c]->kind != ST_DEFAULT)) cinder_diag(diags, CINDER_FATAL, decl->loc, "HIR case table has an invalid owned label");
    }
    for (size_t s = 0U; s < hir->statements.len; ++s) {
        const CinderHIRStmt *stmt = hir->statements.data[s];
        if (stmt->id != s || stmt->kind < ST_EMPTY || stmt->kind > ST_DEFAULT) cinder_diag(diags, CINDER_FATAL, stmt->loc, "invalid HIR statement identity/opcode");
        if (stmt->kind == ST_GOTO && (!statement_owned(hir, stmt->as.jump.target) || stmt->as.jump.target->kind != ST_LABEL || stmt->as.jump.name == NULL || stmt->as.jump.target->as.label.name == NULL || strcmp(stmt->as.jump.name, stmt->as.jump.target->as.label.name) != 0)) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR jump does not name an owned matching label");
        if (stmt->kind == ST_DECL && !declaration_owned(hir, stmt->as.decl)) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR declaration statement is not owned");
        CinderHIRStmt *small[2]; size_t n = children(stmt, small);
        if (stmt->kind == ST_BLOCK) { if (stmt->as.block.items.len > 1000000U || (stmt->as.block.items.len != 0U && stmt->as.block.items.data == NULL)) { cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR block vector exceeds its bounds"); continue; } }
        size_t total = stmt->kind == ST_BLOCK ? stmt->as.block.items.len : n;
        for (size_t i = 0U; i < total; ++i) if (!statement_owned(hir, stmt->kind == ST_BLOCK ? stmt->as.block.items.data[i] : small[i])) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR child statement is not owned");
        const CinderHIRExpr *condition = stmt->kind == ST_IF ? stmt->as.if_stmt.condition : stmt->kind == ST_WHILE || stmt->kind == ST_DO ? stmt->as.loop.condition : stmt->kind == ST_FOR ? stmt->as.for_stmt.condition : stmt->kind == ST_SWITCH ? stmt->as.selection.control : NULL;
        if (condition != NULL && !expression_owned(hir, condition)) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR control expression is not owned");
        if ((stmt->kind == ST_IF || stmt->kind == ST_WHILE || stmt->kind == ST_DO || stmt->kind == ST_SWITCH) && condition == NULL) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR control requires a condition");
        if (stmt->kind == ST_IF && stmt->as.if_stmt.then_branch == NULL) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR conditional requires a selected statement");
        if ((stmt->kind == ST_WHILE || stmt->kind == ST_DO) && stmt->as.loop.body == NULL) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR loop requires a body");
        if (stmt->kind == ST_FOR && stmt->as.for_stmt.body == NULL) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR for loop requires a body");
        if (stmt->kind == ST_SWITCH) {
            if (stmt->as.selection.body == NULL) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR switch requires a body");
            for (size_t c = 0U; c < stmt->as.selection.cases.len; ++c) if (!statement_owned(hir, stmt->as.selection.cases.data[c]) || stmt->as.selection.cases.data[c]->kind != ST_CASE) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR switch case is not an owned case label");
            if (stmt->as.selection.default_label != NULL && (!statement_owned(hir, stmt->as.selection.default_label) || stmt->as.selection.default_label->kind != ST_DEFAULT)) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR switch default is not an owned default label");
        }
        if (stmt->kind == ST_EXPR && !expression_owned(hir, stmt->as.expr)) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR statement expression is not owned");
        if (stmt->kind == ST_RETURN && stmt->as.ret.value != NULL && !expression_owned(hir, stmt->as.ret.value)) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR return expression is not owned");
        if (stmt->kind == ST_FOR && stmt->as.for_stmt.step != NULL && !expression_owned(hir, stmt->as.for_stmt.step)) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR loop step is not owned");
        for (size_t l = 0U; l < stmt->literal_objects.len; ++l) if (!declaration_owned(hir, stmt->literal_objects.data[l])) cinder_diag(diags, CINDER_FATAL, stmt->loc, "HIR literal storage is not owned");
    }
    for (size_t r = 0U; r < hir->roots.len; ++r) if (!declaration_owned(hir, hir->roots.data[r])) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "HIR unit root is not owned");
    for (size_t i = 0U; i < hir->expressions.len; ++i) {
        const CinderHIRExpr *expr = hir->expressions.data[i];
        if (expr->resolved_decl != NULL && !declaration_owned(hir, expr->resolved_decl)) cinder_diag(diags, CINDER_FATAL, expr->loc, "HIR binding is not an owned declaration");
        if (expr->kind == HIR_COMPOUND_LITERAL && !declaration_owned(hir, expr->as.compound_literal)) cinder_diag(diags, CINDER_FATAL, expr->loc, "HIR compound storage is not owned");
    }
    if (diags->errors == before && !tree_acyclic(hir)) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "HIR structured control contains a cycle");
    if (diags->errors == before) {
        size_t count = hir->declarations.len; unsigned char *state = cinder_alloc(count); memset(state, 0, count);
        for (size_t d = 0U; d < count; ++d) {
            const CinderHIRDecl *decl = hir->declarations.data[d];
            while (decl != NULL && state[decl->id] == 0U) { state[decl->id] = 1U; decl = decl->next; }
            if (decl != NULL && state[decl->id] == 1U) { cinder_diag(diags, CINDER_FATAL, decl->loc, "HIR declaration list contains a cycle"); break; }
            decl = hir->declarations.data[d]; while (decl != NULL && state[decl->id] == 1U) { state[decl->id] = 2U; decl = decl->next; }
        }
        free(state);
    }
    if (diags->errors == before && hir->static_objects != NULL) (void)cinder_verify_ir(hir->static_objects, diags);
    return diags->errors == before ? 0 : 1;
}
