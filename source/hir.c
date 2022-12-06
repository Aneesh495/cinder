#include "cinder.h"

#include <stdlib.h>
#include <string.h>

void cinder_hir_init(CinderHIR *hir, CinderTypeContext *types) {
    memset(hir, 0, sizeof(*hir)); hir->types = types; cinder_arena_init(&hir->arena, 32768U);
}

void cinder_hir_destroy(CinderHIR *hir) {
    free(hir->expressions.data); free(hir->bindings); cinder_arena_destroy(&hir->arena); memset(hir, 0, sizeof(*hir));
}

static size_t binding_index(const CinderHIR *hir, const CinderExpr *source) {
    size_t index = (size_t)((uintptr_t)source >> 4U) & (hir->binding_capacity - 1U);
    while (hir->bindings[index].source != NULL && hir->bindings[index].source != source) index = (index + 1U) & (hir->binding_capacity - 1U);
    return index;
}

static CinderHIRBinding *binding(CinderHIR *hir, const CinderExpr *source) {
    if (hir->binding_capacity == 0U || hir->binding_count >= hir->binding_capacity / 2U) {
        size_t capacity = hir->binding_capacity == 0U ? 256U : hir->binding_capacity * 2U;
        CinderHIRBinding *old = hir->bindings; size_t old_capacity = hir->binding_capacity;
        hir->bindings = cinder_alloc(capacity * sizeof(*hir->bindings)); memset(hir->bindings, 0, capacity * sizeof(*hir->bindings)); hir->binding_capacity = capacity;
        for (size_t i = 0U; i < old_capacity; ++i) if (old[i].source != NULL) hir->bindings[binding_index(hir, old[i].source)] = old[i];
        free(old);
    }
    CinderHIRBinding *result = &hir->bindings[binding_index(hir, source)];
    if (result->source == NULL) { result->source = source; ++hir->binding_count; }
    return result;
}

CinderHIRExpr *cinder_hir_value(const CinderHIR *hir, const CinderExpr *source) {
    if (source == NULL || hir->binding_capacity == 0U) return NULL;
    const CinderHIRBinding *entry = &hir->bindings[binding_index(hir, source)];
    return entry->source == source ? entry->value : NULL;
}

static CinderHIRExpr *node(CinderHIR *hir, const CinderExpr *source, CinderHIRKind kind) {
    CinderHIRExpr *result = cinder_arena_alloc(&hir->arena, sizeof(*result), _Alignof(CinderHIRExpr)); memset(result, 0, sizeof(*result));
    result->id = (uint32_t)hir->expressions.len; result->kind = kind; result->loc = source->loc; result->type = source->type;
    result->is_lvalue = source->is_lvalue; result->bit_offset = source->bit_offset; result->bit_width = source->bit_width;
    result->is_place = source->is_lvalue || source->kind == EX_MEMBER || (source->kind == EX_NAME && source->type->kind == TYPE_FUNCTION);
    result->resolved_decl = source->resolved_decl; result->literal_length = source->literal_length;
    cinder_vec_push((CinderVec *)&hir->expressions, &result); return result;
}

static CinderHIRExpr *expression(CinderHIR *hir, const CinderExpr *source, bool value, unsigned depth, CinderDiagnostics *diags) {
    if (source == NULL) return NULL;
    if (depth >= 1024U || hir->expressions.len >= 1000000U) { cinder_diag(diags, CINDER_ERROR, source->loc, "typed HIR expression limit exceeded"); return NULL; }
    CinderHIRBinding *cached = binding(hir, source);
    CinderHIRExpr *prior = value ? cached->value : cached->place; if (prior != NULL) return prior;
    if (source->type == NULL || source->type->kind == TYPE_ERROR) { cinder_diag(diags, CINDER_FATAL, source->loc, "typed HIR requires semantic expression types"); return NULL; }
    if (source->kind == EX_GENERIC) {
        size_t selected = source->as.generic.selected;
        if (selected >= source->as.generic.associations.len) { cinder_diag(diags, CINDER_FATAL, source->loc, "typed HIR requires a selected generic association"); return NULL; }
        CinderHIRExpr *result = expression(hir, source->as.generic.associations.data[selected].value, value, depth + 1U, diags);
        cached = binding(hir, source); if (value) cached->value = result; else cached->place = result; return result;
    }
    bool scalar_place = (source->is_lvalue || source->kind == EX_MEMBER) && source->type->kind != TYPE_ARRAY && source->type->kind != TYPE_STRUCT && source->type->kind != TYPE_UNION && source->type->kind != TYPE_FUNCTION;
    if (value && scalar_place) {
        CinderHIRExpr *place = expression(hir, source, false, depth + 1U, diags);
        CinderHIRExpr *result = node(hir, source, HIR_LOAD); result->is_lvalue = false; result->is_place = false; result->as.unary.value = place;
        binding(hir, source)->value = result; return result;
    }
    CinderHIRExpr *result = NULL;
    switch (source->kind) {
        case EX_INT: case EX_CHAR: result = node(hir, source, HIR_INT); result->as.integer = source->as.integer; break;
        case EX_FLOAT: result = node(hir, source, HIR_FLOAT); result->as.floating = source->as.floating; break;
        case EX_STRING: result = node(hir, source, HIR_STRING); result->as.string = cinder_arena_strndup(&hir->arena, source->as.string, source->literal_length + 1U); break;
        case EX_NAME: result = node(hir, source, HIR_NAME); result->as.name = cinder_arena_strndup(&hir->arena, source->as.name, strlen(source->as.name)); break;
        case EX_SIZEOF: case EX_ALIGNOF: case EX_OFFSETOF:
            result = node(hir, source, HIR_INT);
            result->as.integer = (int64_t)(source->kind == EX_OFFSETOF ? source->as.offset.value : source->kind == EX_SIZEOF ? source->queried_type->size : source->queried_type->align); break;
        case EX_BINARY:
            result = node(hir, source, HIR_BINARY); result->as.binary.op = source->as.binary.op;
            result->as.binary.left = expression(hir, source->as.binary.left, true, depth + 1U, diags);
            result->as.binary.right = expression(hir, source->as.binary.right, true, depth + 1U, diags); break;
        case EX_UNARY: case EX_DECAY: {
            int op = source->as.unary.op;
            bool place = source->kind == EX_DECAY || op == '&' || op == TOK_PLUSPLUS || op == TOK_MINUSMINUS;
            result = node(hir, source, source->kind == EX_DECAY ? HIR_DECAY : op == '&' ? HIR_ADDRESS : HIR_UNARY);
            result->as.unary.op = op; result->as.unary.postfix = source->as.unary.postfix;
            result->as.unary.value = expression(hir, source->as.unary.value, !place, depth + 1U, diags); break;
        }
        case EX_ASSIGN:
            result = node(hir, source, HIR_ASSIGN); result->as.assign.op = source->as.assign.op; result->as.assign.operation_type = source->as.assign.operation_type;
            result->as.assign.target = expression(hir, source->as.assign.target, false, depth + 1U, diags);
            result->as.assign.value = expression(hir, source->as.assign.value, true, depth + 1U, diags); break;
        case EX_CALL:
            if (source->as.call.callee->kind == EX_NAME) {
                const char *name = source->as.call.callee->as.name;
                bool start = strcmp(name, "__cinder_va_start") == 0, copy = strcmp(name, "__cinder_va_copy") == 0, end = strcmp(name, "__cinder_va_end") == 0;
                if (start || copy || end) {
                    result = node(hir, source, start ? HIR_VA_START : copy ? HIR_VA_COPY : HIR_VA_END);
                    CinderHIRExpr *destination = expression(hir, source->as.call.args.data[0], true, depth + 1U, diags);
                    if (copy) { result->as.binary.left = destination; result->as.binary.right = expression(hir, source->as.call.args.data[1], true, depth + 1U, diags); }
                    else result->as.unary.value = destination;
                    break;
                }
            }
            result = node(hir, source, HIR_CALL); result->as.call.callee = expression(hir, source->as.call.callee, true, depth + 1U, diags);
            if (source->as.call.args.len != 0U) result->as.call.args.data = cinder_arena_alloc(&hir->arena, source->as.call.args.len * sizeof(*result->as.call.args.data), _Alignof(CinderHIRExpr *));
            result->as.call.args.len = result->as.call.args.cap = source->as.call.args.len;
            for (size_t i = 0U; i < source->as.call.args.len; ++i) result->as.call.args.data[i] = expression(hir, source->as.call.args.data[i], true, depth + 1U, diags);
            break;
        case EX_VA_ARG:
            result = node(hir, source, HIR_VA_ARG); result->as.va_arg.type = source->as.va_arg.type;
            result->as.va_arg.list = expression(hir, source->as.va_arg.list, true, depth + 1U, diags); break;
        case EX_CONDITIONAL:
            result = node(hir, source, HIR_CONDITIONAL);
            result->as.conditional.condition = expression(hir, source->as.conditional.condition, true, depth + 1U, diags);
            result->as.conditional.yes = expression(hir, source->as.conditional.yes, true, depth + 1U, diags);
            result->as.conditional.no = expression(hir, source->as.conditional.no, true, depth + 1U, diags); break;
        case EX_CAST:
            result = node(hir, source, HIR_CAST); result->as.cast.cast_type = source->as.cast.cast_type;
            result->as.cast.value = expression(hir, source->as.cast.value, true, depth + 1U, diags); break;
        case EX_INDEX:
            result = node(hir, source, HIR_INDEX);
            result->as.index.base = expression(hir, source->as.index.base, true, depth + 1U, diags);
            result->as.index.index = expression(hir, source->as.index.index, true, depth + 1U, diags); break;
        case EX_MEMBER:
            result = node(hir, source, HIR_MEMBER); result->as.member.arrow = source->as.member.arrow; result->as.member.field = source->as.member.field;
            result->as.member.base = expression(hir, source->as.member.base, true, depth + 1U, diags);
            result->as.member.name = cinder_arena_strndup(&hir->arena, source->as.member.name, strlen(source->as.member.name));
            if (source->as.member.path.len != 0U) { size_t size = source->as.member.path.len * sizeof(size_t); result->as.member.path.data = cinder_arena_alloc(&hir->arena, size, _Alignof(size_t)); memcpy(result->as.member.path.data, source->as.member.path.data, size); }
            result->as.member.path.len = result->as.member.path.cap = source->as.member.path.len; break;
        case EX_COMPOUND_LITERAL: result = node(hir, source, HIR_COMPOUND_LITERAL); result->as.compound_literal = source->as.compound_literal; break;
        case EX_INIT_LIST: cinder_diag(diags, CINDER_FATAL, source->loc, "initializer syntax must become typed initializer actions before HIR"); break;
        case EX_GENERIC: break;
    }
    cached = binding(hir, source); if (value) cached->value = result; else cached->place = result; return result;
}

static void declaration(CinderHIR *hir, CinderDecl *decl, CinderDiagnostics *diags) {
    for (; decl != NULL; decl = decl->next) {
        if (decl->initializer != NULL && decl->initializer->kind != EX_INIT_LIST) (void)expression(hir, decl->initializer, true, 0U, diags);
        for (size_t i = 0U; i < decl->init_actions.len; ++i) (void)expression(hir, decl->init_actions.data[i].value, true, 0U, diags);
    }
}

static void statement(CinderHIR *hir, CinderStmt *stmt, unsigned depth, CinderDiagnostics *diags) {
    if (stmt == NULL) return;
    if (depth >= 1024U) { cinder_diag(diags, CINDER_ERROR, stmt->loc, "typed HIR statement limit exceeded"); return; }
    for (size_t i = 0U; i < stmt->literal_objects.len; ++i) if (stmt->literal_objects.data[i]->literal_evaluated) declaration(hir, stmt->literal_objects.data[i], diags);
    switch (stmt->kind) {
        case ST_EXPR: (void)expression(hir, stmt->as.expr, true, 0U, diags); break;
        case ST_RETURN: (void)expression(hir, stmt->as.ret.value, true, 0U, diags); break;
        case ST_DECL: declaration(hir, stmt->as.decl, diags); break;
        case ST_BLOCK: for (size_t i = 0U; i < stmt->as.block.items.len; ++i) statement(hir, stmt->as.block.items.data[i], depth + 1U, diags); break;
        case ST_IF:
            (void)expression(hir, stmt->as.if_stmt.condition, true, 0U, diags); statement(hir, stmt->as.if_stmt.then_branch, depth + 1U, diags); statement(hir, stmt->as.if_stmt.else_branch, depth + 1U, diags); break;
        case ST_WHILE: case ST_DO: (void)expression(hir, stmt->as.loop.condition, true, 0U, diags); statement(hir, stmt->as.loop.body, depth + 1U, diags); break;
        case ST_FOR:
            statement(hir, stmt->as.for_stmt.init, depth + 1U, diags); (void)expression(hir, stmt->as.for_stmt.condition, true, 0U, diags); (void)expression(hir, stmt->as.for_stmt.step, true, 0U, diags); statement(hir, stmt->as.for_stmt.body, depth + 1U, diags); break;
        case ST_LABEL: statement(hir, stmt->as.label.body, depth + 1U, diags); break;
        case ST_SWITCH: (void)expression(hir, stmt->as.selection.control, true, 0U, diags); statement(hir, stmt->as.selection.body, depth + 1U, diags); break;
        case ST_CASE: case ST_DEFAULT: statement(hir, stmt->as.case_label.body, depth + 1U, diags); break;
        case ST_EMPTY: case ST_BREAK: case ST_CONTINUE: case ST_GOTO: break;
    }
}

int cinder_build_hir(CinderHIR *hir, CinderAst *ast, CinderDiagnostics *diags) {
    unsigned before = diags->errors;
    for (size_t i = 0U; i < ast->declarations.len; ++i) { CinderDecl *decl = ast->declarations.data[i]; declaration(hir, decl, diags); if (decl->is_definition) statement(hir, decl->body, 0U, diags); }
    for (size_t i = 0U; i < ast->static_literals.len; ++i) declaration(hir, ast->static_literals.data[i], diags);
    for (size_t i = 0U; i < ast->stored_objects.len; ++i) declaration(hir, ast->stored_objects.data[i], diags);
    return diags->errors == before ? cinder_verify_hir(hir, diags) : 1;
}

static size_t operands(const CinderHIRExpr *expr, CinderHIRExpr **out) {
    switch (expr->kind) {
        case HIR_LOAD: case HIR_ADDRESS: case HIR_UNARY: case HIR_DECAY: case HIR_VA_START: case HIR_VA_END: out[0] = expr->as.unary.value; return 1U;
        case HIR_CAST: out[0] = expr->as.cast.value; return 1U;
        case HIR_BINARY: case HIR_VA_COPY: out[0] = expr->as.binary.left; out[1] = expr->as.binary.right; return 2U;
        case HIR_ASSIGN: out[0] = expr->as.assign.target; out[1] = expr->as.assign.value; return 2U;
        case HIR_INDEX: out[0] = expr->as.index.base; out[1] = expr->as.index.index; return 2U;
        case HIR_MEMBER: out[0] = expr->as.member.base; return 1U;
        case HIR_CONDITIONAL: out[0] = expr->as.conditional.condition; out[1] = expr->as.conditional.yes; out[2] = expr->as.conditional.no; return 3U;
        case HIR_CALL: out[0] = expr->as.call.callee; return 1U;
        case HIR_VA_ARG: out[0] = expr->as.va_arg.list; return 1U;
        case HIR_INT: case HIR_FLOAT: case HIR_STRING: case HIR_NAME: case HIR_COMPOUND_LITERAL: return 0U;
    }
    return 0U;
}

static bool owned(const CinderHIR *hir, const CinderHIRExpr *expr) { return expr != NULL && expr->id < hir->expressions.len && hir->expressions.data[expr->id] == expr; }
static bool scalar(const CinderType *type) { return type != NULL && ((type->kind >= TYPE_BOOL && type->kind <= TYPE_POINTER) || type->kind == TYPE_ENUM); }

typedef struct { size_t parent; size_t next; } HIREdge;

static bool acyclic(const CinderHIR *hir) {
    size_t count = hir->expressions.len, edge_count = 0U;
    for (size_t i = 0U; i < count; ++i) {
        CinderHIRExpr *children[3]; const CinderHIRExpr *expr = hir->expressions.data[i];
        size_t n = operands(expr, children) + (expr->kind == HIR_CALL ? expr->as.call.args.len : 0U);
        if (edge_count > SIZE_MAX - n) return false;
        edge_count += n;
    }
    if (count > SIZE_MAX / sizeof(size_t) || edge_count > SIZE_MAX / sizeof(HIREdge)) return false;
    size_t *heads = cinder_alloc(count * sizeof(size_t)), *waiting = cinder_alloc(count * sizeof(size_t)), *queue = cinder_alloc(count * sizeof(size_t));
    HIREdge *edges = cinder_alloc(edge_count * sizeof(*edges)); size_t at = 0U, begin = 0U, end = 0U;
    for (size_t i = 0U; i < count; ++i) { heads[i] = SIZE_MAX; waiting[i] = 0U; }
    for (size_t i = 0U; i < count; ++i) {
        CinderHIRExpr *children[3]; const CinderHIRExpr *expr = hir->expressions.data[i]; size_t n = operands(expr, children);
        size_t arguments = expr->kind == HIR_CALL ? expr->as.call.args.len : 0U;
        waiting[i] = n + arguments;
        for (size_t j = 0U; j < n + arguments; ++j) {
            const CinderHIRExpr *child = j < n ? children[j] : expr->as.call.args.data[j - n];
            edges[at].parent = i; edges[at].next = heads[child->id]; heads[child->id] = at++;
        }
        if (waiting[i] == 0U) queue[end++] = i;
    }
    while (begin < end) {
        size_t child = queue[begin++];
        for (size_t edge = heads[child]; edge != SIZE_MAX; edge = edges[edge].next) if (--waiting[edges[edge].parent] == 0U) queue[end++] = edges[edge].parent;
    }
    free(heads); free(waiting); free(queue); free(edges); return end == count;
}

int cinder_verify_hir(const CinderHIR *hir, CinderDiagnostics *diags) {
    unsigned before = diags->errors;
    for (size_t i = 0U; i < hir->expressions.len; ++i) {
        const CinderHIRExpr *expr = hir->expressions.data[i]; CinderHIRExpr *children[3]; size_t count = operands(expr, children);
        if (expr->id != i || expr->kind < HIR_INT || expr->kind > HIR_VA_END || expr->type == NULL || expr->type->kind == TYPE_ERROR) cinder_diag(diags, CINDER_FATAL, expr->loc, "invalid typed HIR identity/opcode/type");
        for (size_t j = 0U; j < count; ++j) if (!owned(hir, children[j]) || children[j] == expr) cinder_diag(diags, CINDER_FATAL, expr->loc, "typed HIR operand does not name an owned expression");
        if (expr->kind == HIR_LOAD && owned(hir, expr->as.unary.value) && (!expr->as.unary.value->is_place || expr->is_place || expr->is_lvalue || !cinder_type_equal(expr->type, expr->as.unary.value->type) || !scalar(expr->type))) cinder_diag(diags, CINDER_FATAL, expr->loc, "typed HIR load requires a matching scalar place");
        if (expr->kind == HIR_ASSIGN && owned(hir, expr->as.assign.target) && !expr->as.assign.target->is_lvalue) cinder_diag(diags, CINDER_FATAL, expr->loc, "typed HIR update requires a place");
        if (expr->kind == HIR_ADDRESS && owned(hir, expr->as.unary.value) && (!expr->as.unary.value->is_place || expr->as.unary.value->bit_width != 0U || expr->type == NULL || expr->type->kind != TYPE_POINTER || !cinder_type_equal(expr->type->base, expr->as.unary.value->type))) cinder_diag(diags, CINDER_FATAL, expr->loc, "typed HIR address requires an addressable matching place");
        if (expr->kind == HIR_CAST && (expr->type == NULL || !cinder_type_equal(expr->type, expr->as.cast.cast_type))) cinder_diag(diags, CINDER_FATAL, expr->loc, "typed HIR conversion has a mismatched target type");
        if (expr->kind == HIR_CALL) {
            if (expr->as.call.args.len > 65536U || (expr->as.call.args.len != 0U && expr->as.call.args.data == NULL)) { cinder_diag(diags, CINDER_FATAL, expr->loc, "typed HIR argument vector exceeds its bounds"); continue; }
            for (size_t a = 0U; a < expr->as.call.args.len; ++a) if (!owned(hir, expr->as.call.args.data[a])) cinder_diag(diags, CINDER_FATAL, expr->loc, "typed HIR call argument is not owned");
        }
    }
    if (diags->errors == before && !acyclic(hir)) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "typed HIR contains a cyclic expression graph");
    return diags->errors == before ? 0 : 1;
}

void cinder_dump_hir(const CinderHIR *hir, CinderSourceManager *sources, FILE *out) {
    static const char *const names[] = {"integer", "float", "string-place", "binding", "binary", "unary", "update", "call", "va-arg", "choice", "convert", "decay", "index-place", "member-place", "literal-place", "load", "address", "va-start", "va-copy", "va-end"};
    fputs("typed expression HIR 1\n", out);
    for (size_t i = 0U; i < hir->expressions.len; ++i) {
        CinderHIRExpr *expr = hir->expressions.data[i]; CinderHIRExpr *children[3]; size_t count = operands(expr, children); CinderLoc loc = expr->loc; cinder_loc_linecol(sources, &loc);
        fprintf(out, "h%u %s type=%s category=%s", expr->id, names[expr->kind], cinder_type_name(expr->type), expr->is_place ? "place" : "value");
        if (expr->kind == HIR_INT) fprintf(out, " constant=%lld", (long long)expr->as.integer);
        if (expr->kind == HIR_NAME) fprintf(out, " name=%s", expr->as.name);
        for (size_t j = 0U; j < count; ++j) fprintf(out, " h%u", children[j]->id);
        if (expr->kind == HIR_CALL) for (size_t a = 0U; a < expr->as.call.args.len; ++a) fprintf(out, " h%u", expr->as.call.args.data[a]->id);
        fprintf(out, " source=%s:%u:%u\n", cinder_loc_name(sources, loc), loc.line, loc.column);
    }
}
