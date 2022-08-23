#include "cinder.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

void cinder_control_init(CinderControlMap *map) { memset(map, 0, sizeof(*map)); }

void cinder_control_destroy(CinderControlMap *map) {
    for (size_t i = 0U; i < map->scopes.len; ++i) free(map->scopes.data[i].objects.data);
    free(map->scopes.data); free(map->labels.data); free(map->jumps.data); free(map->cases.data);
    memset(map, 0, sizeof(*map));
}

static bool owns_scope(const CinderStmt *stmt) {
    return stmt->kind == ST_BLOCK || stmt->kind == ST_IF || stmt->kind == ST_SWITCH || stmt->kind == ST_WHILE || stmt->kind == ST_DO || stmt->kind == ST_FOR || stmt->literal_objects.len != 0U;
}

static void scope_object(CinderControlMap *map, int scope, CinderDecl *decl) {
    if (scope < 0 || decl->kind != DECL_VAR || decl->name == NULL || decl->is_static || decl->is_extern) return;
    cinder_vec_push((CinderVec *)&map->scopes.data[scope].objects, &decl);
}

static void visit(CinderControlMap *map, CinderStmt *stmt, int scope, unsigned depth, CinderDiagnostics *diags) {
    if (stmt == NULL) return;
    if (depth >= 1024U) { cinder_diag(diags, CINDER_ERROR, stmt->loc, "control-flow nesting exceeds the profile limit"); return; }
    if (owns_scope(stmt)) {
        if (map->scopes.len >= (size_t)INT_MAX) { cinder_diag(diags, CINDER_ERROR, stmt->loc, "control-flow scope table exceeds the profile limit"); return; }
        CinderControlScope child; memset(&child, 0, sizeof(child)); child.owner = stmt; child.parent = scope;
        scope = (int)map->scopes.len; cinder_vec_push((CinderVec *)&map->scopes, &child);
        for (size_t i = 0U; i < stmt->literal_objects.len; ++i) if (stmt->literal_objects.data[i]->literal_evaluated) scope_object(map, scope, stmt->literal_objects.data[i]);
    }
    stmt->control_scope = scope;
    switch (stmt->kind) {
        case ST_LABEL:
            if (map->labels.len >= 65536U) { cinder_diag(diags, CINDER_ERROR, stmt->loc, "function label table exceeds the profile limit"); return; }
            cinder_vec_push((CinderVec *)&map->labels, &stmt);
            visit(map, stmt->as.label.body, scope, depth + 1U, diags); break;
        case ST_GOTO: cinder_vec_push((CinderVec *)&map->jumps, &stmt); break;
        case ST_CASE: case ST_DEFAULT:
            cinder_vec_push((CinderVec *)&map->cases, &stmt);
            visit(map, stmt->as.case_label.body, scope, depth + 1U, diags); break;
        case ST_SWITCH: visit(map, stmt->as.selection.body, scope, depth + 1U, diags); break;
        case ST_DECL:
            for (CinderDecl *decl = stmt->as.decl; decl != NULL; decl = decl->next) scope_object(map, scope, decl);
            break;
        case ST_BLOCK:
            for (size_t i = 0U; i < stmt->as.block.items.len; ++i) visit(map, stmt->as.block.items.data[i], scope, depth + 1U, diags);
            break;
        case ST_IF:
            visit(map, stmt->as.if_stmt.then_branch, scope, depth + 1U, diags); visit(map, stmt->as.if_stmt.else_branch, scope, depth + 1U, diags); break;
        case ST_WHILE: case ST_DO: visit(map, stmt->as.loop.body, scope, depth + 1U, diags); break;
        case ST_FOR:
            visit(map, stmt->as.for_stmt.init, scope, depth + 1U, diags); visit(map, stmt->as.for_stmt.body, scope, depth + 1U, diags); break;
        case ST_EMPTY: case ST_EXPR: case ST_RETURN: case ST_BREAK: case ST_CONTINUE: break;
    }
}

static int compare_labels(const void *left, const void *right) {
    const CinderStmt *a = *(CinderStmt *const *)left, *b = *(CinderStmt *const *)right;
    return strcmp(a->as.label.name, b->as.label.name);
}

bool cinder_control_build(CinderControlMap *map, CinderStmt *body, CinderDiagnostics *diags) {
    unsigned before = diags->errors;
    visit(map, body, -1, 0U, diags);
    if (map->labels.len != 0U) qsort(map->labels.data, map->labels.len, sizeof(*map->labels.data), compare_labels);
    for (size_t l = 1U; l < map->labels.len; ++l) if (strcmp(map->labels.data[l - 1U]->as.label.name, map->labels.data[l]->as.label.name) == 0) cinder_diag(diags, CINDER_ERROR, map->labels.data[l]->loc, "duplicate function label '%s'", map->labels.data[l]->as.label.name);
    for (size_t j = 0U; j < map->jumps.len; ++j) {
        CinderStmt *jump = map->jumps.data[j]; jump->as.jump.target = NULL;
        if (jump->as.jump.name == NULL) continue;
        size_t begin = 0U, end = map->labels.len;
        while (begin < end) {
            size_t middle = begin + (end - begin) / 2U; int order = strcmp(jump->as.jump.name, map->labels.data[middle]->as.label.name);
            if (order == 0) { jump->as.jump.target = map->labels.data[middle]; break; }
            if (order < 0) end = middle; else begin = middle + 1U;
        }
        if (jump->as.jump.target == NULL) cinder_diag(diags, CINDER_ERROR, jump->loc, "undefined function label '%s'", jump->as.jump.name);
    }
    return diags->errors == before;
}
