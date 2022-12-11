#include "hir_private.h"

#include <stdlib.h>
#include <string.h>

void cinder_hir_init(CinderHIR *hir, CinderTypeContext *types) {
    memset(hir, 0, sizeof(*hir)); hir->types = types; cinder_arena_init(&hir->arena, 32768U);
}

void cinder_hir_destroy(CinderHIR *hir) {
    free(hir->expressions.data); free(hir->bindings); free(hir->declarations.data); free(hir->roots.data); free(hir->statements.data); free(hir->memo);
    if (hir->static_objects != NULL) { cinder_ir_destroy(hir->static_objects); free(hir->static_objects); } cinder_arena_destroy(&hir->arena); memset(hir, 0, sizeof(*hir));
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

static CinderHIRExpr *node(CinderHIR *hir, const CinderExpr *source, CinderHIRKind kind, CinderDiagnostics *diags) {
    CinderHIRExpr *result = cinder_arena_alloc(&hir->arena, sizeof(*result), _Alignof(CinderHIRExpr)); memset(result, 0, sizeof(*result));
    result->kind = kind; result->loc = source->loc; result->type = source->type;
    result->is_lvalue = source->is_lvalue; result->bit_offset = source->bit_offset; result->bit_width = source->bit_width;
    result->is_place = source->is_lvalue || source->kind == EX_MEMBER || (source->kind == EX_NAME && source->type->kind == TYPE_FUNCTION);
    result->resolved_decl = cinder_hir_declaration(hir, source->resolved_decl, 0U, diags); result->literal_length = source->literal_length;
    result->id = (uint32_t)hir->expressions.len; cinder_vec_push((CinderVec *)&hir->expressions, &result); return result;
}

CinderHIRExpr *cinder_hir_expression(CinderHIR *hir, const CinderExpr *source, bool value, unsigned depth, CinderDiagnostics *diags) {
    if (source == NULL) return NULL;
    if (depth >= 1024U || hir->expressions.len >= 1000000U) { cinder_diag(diags, CINDER_ERROR, source->loc, "typed HIR expression limit exceeded"); return NULL; }
    CinderHIRBinding *cached = binding(hir, source);
    CinderHIRExpr *prior = value ? cached->value : cached->place; if (prior != NULL) return prior;
    if (source->type == NULL || source->type->kind == TYPE_ERROR) { cinder_diag(diags, CINDER_FATAL, source->loc, "typed HIR requires semantic expression types"); return NULL; }
    if (source->kind == EX_GENERIC) {
        size_t selected = source->as.generic.selected;
        if (selected >= source->as.generic.associations.len) { cinder_diag(diags, CINDER_FATAL, source->loc, "typed HIR requires a selected generic association"); return NULL; }
        CinderHIRExpr *result = cinder_hir_expression(hir, source->as.generic.associations.data[selected].value, value, depth + 1U, diags);
        cached = binding(hir, source); if (value) cached->value = result; else cached->place = result; return result;
    }
    bool scalar_place = (source->is_lvalue || source->kind == EX_MEMBER) && source->type->kind != TYPE_ARRAY && source->type->kind != TYPE_STRUCT && source->type->kind != TYPE_UNION && source->type->kind != TYPE_FUNCTION;
    if (value && scalar_place) {
        CinderHIRExpr *place = cinder_hir_expression(hir, source, false, depth + 1U, diags);
        CinderHIRExpr *result = node(hir, source, HIR_LOAD, diags); result->is_lvalue = false; result->is_place = false; result->as.unary.value = place;
        binding(hir, source)->value = result; return result;
    }
    CinderHIRExpr *result = NULL;
    switch (source->kind) {
        case EX_INT: case EX_CHAR: result = node(hir, source, HIR_INT, diags); result->as.integer = source->as.integer; break;
        case EX_FLOAT: result = node(hir, source, HIR_FLOAT, diags); result->as.floating = source->as.floating; break;
        case EX_STRING: result = node(hir, source, HIR_STRING, diags); result->as.string = cinder_arena_strndup(&hir->arena, source->as.string, source->literal_length + 1U); break;
        case EX_NAME: result = node(hir, source, HIR_NAME, diags); result->as.name = cinder_arena_strndup(&hir->arena, source->as.name, strlen(source->as.name)); break;
        case EX_SIZEOF: case EX_ALIGNOF: case EX_OFFSETOF:
            result = node(hir, source, HIR_INT, diags);
            result->as.integer = (int64_t)(source->kind == EX_OFFSETOF ? source->as.offset.value : source->kind == EX_SIZEOF ? source->queried_type->size : source->queried_type->align); break;
        case EX_BINARY:
            result = node(hir, source, HIR_BINARY, diags); result->as.binary.op = source->as.binary.op;
            result->as.binary.left = cinder_hir_expression(hir, source->as.binary.left, true, depth + 1U, diags);
            result->as.binary.right = cinder_hir_expression(hir, source->as.binary.right, true, depth + 1U, diags); break;
        case EX_UNARY: case EX_DECAY: {
            int op = source->as.unary.op;
            bool place = source->kind == EX_DECAY || op == '&' || op == TOK_PLUSPLUS || op == TOK_MINUSMINUS;
            result = node(hir, source, source->kind == EX_DECAY ? HIR_DECAY : op == '&' ? HIR_ADDRESS : HIR_UNARY, diags);
            result->as.unary.op = op; result->as.unary.postfix = source->as.unary.postfix;
            result->as.unary.value = cinder_hir_expression(hir, source->as.unary.value, !place, depth + 1U, diags); break;
        }
        case EX_ASSIGN:
            result = node(hir, source, HIR_ASSIGN, diags); result->as.assign.op = source->as.assign.op; result->as.assign.operation_type = source->as.assign.operation_type;
            result->as.assign.target = cinder_hir_expression(hir, source->as.assign.target, false, depth + 1U, diags);
            result->as.assign.value = cinder_hir_expression(hir, source->as.assign.value, true, depth + 1U, diags); break;
        case EX_CALL:
            if (source->as.call.callee->kind == EX_NAME) {
                const char *name = source->as.call.callee->as.name;
                bool start = strcmp(name, "__cinder_va_start") == 0, copy = strcmp(name, "__cinder_va_copy") == 0, end = strcmp(name, "__cinder_va_end") == 0;
                if (start || copy || end) {
                    result = node(hir, source, start ? HIR_VA_START : copy ? HIR_VA_COPY : HIR_VA_END, diags);
                    CinderHIRExpr *destination = cinder_hir_expression(hir, source->as.call.args.data[0], true, depth + 1U, diags);
                    if (copy) { result->as.binary.left = destination; result->as.binary.right = cinder_hir_expression(hir, source->as.call.args.data[1], true, depth + 1U, diags); }
                    else result->as.unary.value = destination;
                    break;
                }
            }
            result = node(hir, source, HIR_CALL, diags); result->as.call.callee = cinder_hir_expression(hir, source->as.call.callee, true, depth + 1U, diags);
            if (source->as.call.args.len != 0U) result->as.call.args.data = cinder_arena_alloc(&hir->arena, source->as.call.args.len * sizeof(*result->as.call.args.data), _Alignof(CinderHIRExpr *));
            result->as.call.args.len = result->as.call.args.cap = source->as.call.args.len;
            for (size_t i = 0U; i < source->as.call.args.len; ++i) result->as.call.args.data[i] = cinder_hir_expression(hir, source->as.call.args.data[i], true, depth + 1U, diags);
            break;
        case EX_VA_ARG:
            result = node(hir, source, HIR_VA_ARG, diags); result->as.va_arg.type = source->as.va_arg.type;
            result->as.va_arg.list = cinder_hir_expression(hir, source->as.va_arg.list, true, depth + 1U, diags); break;
        case EX_CONDITIONAL:
            result = node(hir, source, HIR_CONDITIONAL, diags);
            result->as.conditional.condition = cinder_hir_expression(hir, source->as.conditional.condition, true, depth + 1U, diags);
            result->as.conditional.yes = cinder_hir_expression(hir, source->as.conditional.yes, true, depth + 1U, diags);
            result->as.conditional.no = cinder_hir_expression(hir, source->as.conditional.no, true, depth + 1U, diags); break;
        case EX_CAST:
            result = node(hir, source, HIR_CAST, diags); result->as.cast.cast_type = source->as.cast.cast_type;
            result->as.cast.value = cinder_hir_expression(hir, source->as.cast.value, true, depth + 1U, diags); break;
        case EX_INDEX:
            result = node(hir, source, HIR_INDEX, diags);
            result->as.index.base = cinder_hir_expression(hir, source->as.index.base, true, depth + 1U, diags);
            result->as.index.index = cinder_hir_expression(hir, source->as.index.index, true, depth + 1U, diags); break;
        case EX_MEMBER:
            result = node(hir, source, HIR_MEMBER, diags); result->as.member.arrow = source->as.member.arrow; result->as.member.field = source->as.member.field;
            result->as.member.base = cinder_hir_expression(hir, source->as.member.base, true, depth + 1U, diags);
            result->as.member.name = cinder_arena_strndup(&hir->arena, source->as.member.name, strlen(source->as.member.name));
            if (source->as.member.path.len != 0U) { size_t size = source->as.member.path.len * sizeof(size_t); result->as.member.path.data = cinder_arena_alloc(&hir->arena, size, _Alignof(size_t)); memcpy(result->as.member.path.data, source->as.member.path.data, size); }
            result->as.member.path.len = result->as.member.path.cap = source->as.member.path.len; break;
        case EX_COMPOUND_LITERAL: result = node(hir, source, HIR_COMPOUND_LITERAL, diags); result->as.compound_literal = cinder_hir_declaration(hir, source->as.compound_literal, 0U, diags); break;
        case EX_INIT_LIST: cinder_diag(diags, CINDER_FATAL, source->loc, "initializer syntax must become typed initializer actions before HIR"); break;
        case EX_GENERIC: break;
    }
    cached = binding(hir, source); if (value) cached->value = result; else cached->place = result; return result;
}

int cinder_build_hir(CinderHIR *hir, CinderAst *ast, CinderDiagnostics *diags) {
    if (cinder_hir_control_unit(hir, ast, diags) != 0) return 1;
    return cinder_verify_hir(hir, diags);
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
    if (diags->errors == before) (void)cinder_verify_hir_control(hir, diags);
    if (diags->errors == before && !acyclic(hir)) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "typed HIR contains a cyclic expression graph");
    return diags->errors == before ? 0 : 1;
}

void cinder_dump_hir(const CinderHIR *hir, CinderSourceManager *sources, FILE *out) {
    static const char *const names[] = {"integer", "float", "string-place", "binding", "binary", "unary", "update", "call", "va-arg", "choice", "convert", "decay", "index-place", "member-place", "literal-place", "load", "address", "va-start", "va-copy", "va-end"};
    fputs("typed HIR 2\n", out);
    for (size_t d = 0U; d < hir->declarations.len; ++d) {
        const CinderHIRDecl *decl = hir->declarations.data[d];
        fprintf(out, "d%u declaration name=%s type=%s alignment=%zu", decl->id, decl->name == NULL ? "<anonymous>" : decl->name, cinder_type_name(decl->type), decl->alignment);
        if (decl->canonical != NULL) fprintf(out, " canonical=d%u", decl->canonical->id);
        if (decl->body != NULL) fprintf(out, " body=s%u scopes=%zu", decl->body->id, decl->control.scopes.len);
        fputc('\n', out);
        for (size_t a = 0U; a < decl->init_actions.len; ++a) {
            const CinderHIRInitAction *action = &decl->init_actions.data[a];
            fprintf(out, "  initialize offset=%zu type=%s zero=%u bits=%u:%u", action->offset, cinder_type_name(action->type), action->zero ? 1U : 0U, action->bit_offset, action->bit_width);
            if (action->value != NULL) fprintf(out, " value=h%u", action->value->id);
            fputc('\n', out);
        }
    }
    for (size_t s = 0U; s < hir->statements.len; ++s) {
        const CinderHIRStmt *stmt = hir->statements.data[s];
        static const char *const statements[] = {"empty", "expression", "block", "return", "if", "while", "do", "for", "break", "continue", "declaration", "label", "goto", "switch", "case", "default"};
        fprintf(out, "s%u statement kind=%s scope=%d", stmt->id, statements[stmt->kind], stmt->control_scope);
        if (stmt->kind == ST_BLOCK) for (size_t i = 0U; i < stmt->as.block.items.len; ++i) fprintf(out, " s%u", stmt->as.block.items.data[i]->id);
        if (stmt->kind == ST_RETURN && stmt->as.ret.value != NULL) fprintf(out, " value=h%u", stmt->as.ret.value->id);
        if (stmt->kind == ST_GOTO) fprintf(out, " target=s%u", stmt->as.jump.target->id);
        if (stmt->kind == ST_DECL) fprintf(out, " declaration=d%u", stmt->as.decl->id);
        if (stmt->kind == ST_EXPR) fprintf(out, " value=h%u", stmt->as.expr->id);
        if (stmt->kind == ST_IF) { fprintf(out, " condition=h%u then=s%u", stmt->as.if_stmt.condition->id, stmt->as.if_stmt.then_branch->id); if (stmt->as.if_stmt.else_branch != NULL) fprintf(out, " else=s%u", stmt->as.if_stmt.else_branch->id); }
        if (stmt->kind == ST_WHILE || stmt->kind == ST_DO) fprintf(out, " condition=h%u body=s%u", stmt->as.loop.condition->id, stmt->as.loop.body->id);
        if (stmt->kind == ST_FOR) { if (stmt->as.for_stmt.init != NULL) fprintf(out, " init=s%u", stmt->as.for_stmt.init->id); if (stmt->as.for_stmt.condition != NULL) fprintf(out, " condition=h%u", stmt->as.for_stmt.condition->id); if (stmt->as.for_stmt.step != NULL) fprintf(out, " step=h%u", stmt->as.for_stmt.step->id); fprintf(out, " body=s%u", stmt->as.for_stmt.body->id); }
        if (stmt->kind == ST_LABEL) fprintf(out, " label=%s body=s%u", stmt->as.label.name, stmt->as.label.body->id);
        if (stmt->kind == ST_CASE || stmt->kind == ST_DEFAULT) fprintf(out, " case=%lld body=s%u", (long long)stmt->as.case_label.value, stmt->as.case_label.body->id);
        if (stmt->kind == ST_SWITCH) { fprintf(out, " control=h%u body=s%u", stmt->as.selection.control->id, stmt->as.selection.body->id); for (size_t c = 0U; c < stmt->as.selection.cases.len; ++c) fprintf(out, " case=s%u", stmt->as.selection.cases.data[c]->id); if (stmt->as.selection.default_label != NULL) fprintf(out, " default=s%u", stmt->as.selection.default_label->id); }
        fputc('\n', out);
    }
    for (size_t g = 0U; g < hir->static_objects->globals.len; ++g) {
        const CinderIRGlobal *object = &hir->static_objects->globals.data[g];
        fprintf(out, "object name=%s type=%s size=%zu bytes=%zu relocations=%zu\n", object->name, cinder_type_name(object->type), object->type->size, object->byte_count, object->addresses.len);
    }
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
