#include "cinder.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int64_t value; CinderInterpClass classification; } Expected;
static uint64_t rng;
static uint64_t random_bits(void) { rng ^= rng << 13U; rng ^= rng >> 7U; rng ^= rng << 17U; return rng; }
static int64_t signed_value(uint64_t value) { return value <= (uint64_t)INT64_MAX ? (int64_t)value : -1 - (int64_t)~value; }

static CinderIRInst instruction(CinderIROp op, CinderValueId dst, CinderValueId left, CinderValueId right, CinderType *type) {
    CinderIRInst value; memset(&value, 0, sizeof(value));
    value.op = op; value.dst = dst; value.left = left; value.right = right; value.slot = -1;
    value.type = type; value.source_type = type; return value;
}
static CinderIRFunction function(CinderIRModule *module, const char *name, CinderType *result) {
    CinderIRFunction fn; memset(&fn, 0, sizeof(fn)); fn.types = module->types; fn.global = true;
    fn.name = cinder_strndup(name, strlen(name)); CinderParamVec params = {NULL, 0U, 0U};
    fn.type = cinder_type_function(module->types, result, &params); return fn;
}
static void block(CinderIRFunction *fn) {
    CinderIRBlock value; memset(&value, 0, sizeof(value)); value.id = (CinderBlockId)fn->blocks.len;
    value.name = cinder_strndup("case", 4U); value.terminator.value = CINDER_INVALID_VALUE; value.terminator.condition = CINDER_INVALID_VALUE;
    cinder_vec_push((CinderVec *)&fn->blocks, &value);
}
static void add(CinderIRFunction *fn, size_t at, CinderIRInst value) {
    cinder_vec_push((CinderVec *)&fn->blocks.data[at].instructions, &value);
    if (value.dst != CINDER_INVALID_VALUE && fn->value_count <= value.dst) fn->value_count = (size_t)value.dst + 1U;
}
static void constant(CinderIRFunction *fn, size_t at, CinderValueId id, CinderType *type, int64_t integer, double fp) {
    CinderIRInst value = instruction(cinder_ir_floating(type) ? IR_FCONST : IR_CONST, id, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, type);
    value.integer = integer; value.floating = fp; add(fn, at, value);
}
static void edge(CinderIRFunction *fn, CinderBlockId from, CinderBlockId to) {
    cinder_vec_push((CinderVec *)&fn->blocks.data[from].successors, &to);
    cinder_vec_push((CinderVec *)&fn->blocks.data[to].predecessors, &from);
}
static void returned(CinderIRFunction *fn, size_t at, CinderValueId value) { fn->blocks.data[at].terminator.kind = TERM_RETURN; fn->blocks.data[at].terminator.value = value; }
static void jump(CinderIRFunction *fn, CinderBlockId from, CinderBlockId to) { fn->blocks.data[from].terminator.kind = TERM_JUMP; fn->blocks.data[from].terminator.target = to; edge(fn, from, to); }
static void branch(CinderIRFunction *fn, CinderBlockId from, CinderValueId condition, CinderBlockId yes, CinderBlockId no) {
    fn->blocks.data[from].terminator.kind = TERM_BRANCH; fn->blocks.data[from].terminator.condition = condition;
    fn->blocks.data[from].terminator.yes = yes; fn->blocks.data[from].terminator.no = no; edge(fn, from, yes); edge(fn, from, no);
}

static Expected arithmetic(CinderIRModule *module, unsigned index, unsigned family) {
    CinderTypeContext *types = module->types;
    CinderType *type = family == 0U ? types->uchar_type : family == 1U ? types->schar_type : family == 2U ? types->ushort_type : family == 3U ? types->int_type : family == 4U ? types->ulong_type : types->long_type;
    CinderIRFunction fn = function(module, "main", type); block(&fn);
    int64_t a = 0, b = 0, expected = 0; CinderIROp op = IR_ADD; CinderInterpClass classification = INTERP_DEFINED;
    if (family == 0U) { a = (int64_t)(index & 255U); b = (int64_t)((index >> 8U) & 255U); expected = (a + b) & 255; }
    else if (family == 1U) {
        a = (int64_t)(random_bits() % 256U) - 128; b = (int64_t)(random_bits() % 256U) - 128; expected = a + b;
        if (expected < -128 || expected > 127) classification = INTERP_SIGNED_OVERFLOW;
    } else if (family == 2U) { op = IR_MUL; a = (int64_t)(random_bits() & 65535U); b = (int64_t)(random_bits() & 65535U); expected = (a * b) & 65535; }
    else if (family == 3U) {
        op = index % 2U == 0U ? IR_DIV_S : IR_MOD_S; a = (int64_t)(random_bits() % 200001U) - 100000; b = (int64_t)(random_bits() % 65U) - 32;
        if (index % 31U == 0U) { a = -INT64_C(2147483647) - 1; b = -1; classification = INTERP_SIGNED_OVERFLOW; }
        else if (b == 0) classification = INTERP_DIVISION_ZERO;
        else expected = op == IR_DIV_S ? a / b : a % b;
    } else if (family == 4U) {
        op = index % 2U == 0U ? IR_SHL : IR_SHR_U; a = signed_value(random_bits()); b = (int64_t)(random_bits() % 70U);
        if (b >= 64) classification = INTERP_INVALID_SHIFT;
        else expected = signed_value(op == IR_SHL ? (uint64_t)a << (unsigned)b : (uint64_t)a >> (unsigned)b);
    } else {
        op = IR_MUL; a = (int64_t)(random_bits() % 2000000001U) - 1000000000; b = (int64_t)(random_bits() % 2000000001U) - 1000000000; expected = a * b;
        if (index % 29U == 0U) { a = INT64_MAX; b = 2; classification = INTERP_SIGNED_OVERFLOW; }
    }
    constant(&fn, 0U, 0U, type, a, 0.0); constant(&fn, 0U, 1U, type, b, 0.0);
    add(&fn, 0U, instruction(op, 2U, 0U, 1U, type)); returned(&fn, 0U, 2U);
    cinder_vec_push((CinderVec *)&module->functions, &fn); return (Expected){expected, classification};
}

static Expected floating(CinderIRModule *module, unsigned index, unsigned family) {
    CinderTypeContext *types = module->types;
    CinderType *fp = family == 6U ? types->float_type : types->double_type;
    CinderType *target = family == 7U ? types->ulong_type : types->int_type;
    CinderIRFunction fn = function(module, "main", target); block(&fn);
    Expected expected = {0, INTERP_DEFINED};
    if (family == 6U) {
        float a = (float)((int64_t)(random_bits() % 1024U) - 512) / 4.0f;
        float b = (float)((int64_t)(random_bits() % 1024U) - 512) / 4.0f;
        unsigned choice = index % 4U;
        float observed = choice == 0U ? a + b : choice == 1U ? a - b : choice == 2U ? a * b : b == 0.0f ? 0.0f : a / b;
        CinderIROp op = choice == 0U ? IR_FADD : choice == 1U ? IR_FSUB : choice == 2U ? IR_FMUL : IR_FDIV;
        if (choice == 3U && b == 0.0f) expected.classification = INTERP_CONVERSION_RANGE;
        else expected.value = (int64_t)observed;
        constant(&fn, 0U, 0U, fp, 0, (double)a); constant(&fn, 0U, 1U, fp, 0, (double)b);
        add(&fn, 0U, instruction(op, 2U, 0U, 1U, fp));
    } else {
        double observed = 0x1p63 + (double)(random_bits() % 1000000U) * 2048.0;
        if (index % 37U == 0U) { observed = 0x1p64; expected.classification = INTERP_CONVERSION_RANGE; }
        else if (index % 41U == 0U) { observed = -1.0; expected.classification = INTERP_CONVERSION_RANGE; }
        else expected.value = signed_value((uint64_t)observed);
        constant(&fn, 0U, 2U, fp, 0, observed);
    }
    CinderIRInst convert = instruction(IR_CONVERT, 3U, 2U, CINDER_INVALID_VALUE, target); convert.source_type = fp;
    add(&fn, 0U, convert); returned(&fn, 0U, 3U); cinder_vec_push((CinderVec *)&module->functions, &fn); return expected;
}

static Expected memory_join(CinderIRModule *module, unsigned index) {
    CinderType *type = module->types->int_type; CinderIRFunction fn = function(module, "main", type);
    fn.local_count = 1U; cinder_vec_push((CinderVec *)&fn.local_types, &type);
    for (unsigned b = 0U; b < 4U; ++b) block(&fn);
    bool chosen = (random_bits() & 1U) != 0U, initialize_both = (index / 9U) % 3U != 0U;
    int64_t a = (int64_t)(random_bits() % 40001U) - 20000, b = (int64_t)(random_bits() % 40001U) - 20000;
    constant(&fn, 0U, 0U, type, chosen ? 1 : 0, 0.0); branch(&fn, 0U, 0U, 1U, 2U);
    constant(&fn, 1U, 1U, type, a, 0.0); CinderIRInst store = instruction(IR_LOCAL_STORE, CINDER_INVALID_VALUE, 1U, CINDER_INVALID_VALUE, type); store.slot = 0; add(&fn, 1U, store); jump(&fn, 1U, 3U);
    if (initialize_both) { constant(&fn, 2U, 2U, type, b, 0.0); store.left = 2U; add(&fn, 2U, store); }
    jump(&fn, 2U, 3U);
    CinderIRInst load = instruction(IR_LOCAL_LOAD, 3U, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, type); load.slot = 0; add(&fn, 3U, load); returned(&fn, 3U, 3U);
    cinder_vec_push((CinderVec *)&module->functions, &fn);
    return (Expected){chosen ? a : b, chosen || initialize_both ? INTERP_DEFINED : INTERP_UNINITIALIZED};
}

static Expected recursion(CinderIRModule *module, unsigned index) {
    CinderType *type = module->types->int_type;
    CinderIRFunction helper = function(module, "sum", type);
    CinderParam param = {"n", type, 0U}; cinder_vec_push((CinderVec *)&helper.type->params, &param);
    CinderDecl *decl = cinder_arena_alloc(&module->arena, sizeof(*decl), _Alignof(CinderDecl)); memset(decl, 0, sizeof(*decl)); decl->type = type; decl->name = "n";
    cinder_vec_push((CinderVec *)&helper.params, &decl);
    for (unsigned b = 0U; b < 3U; ++b) block(&helper);
    CinderIRInst argument = instruction(IR_ARG, 0U, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, type); argument.slot = 0; argument.operator_code = 0; add(&helper, 0U, argument);
    constant(&helper, 0U, 1U, type, 0, 0.0); add(&helper, 0U, instruction(IR_CMP_LE_S, 2U, 0U, 1U, type)); branch(&helper, 0U, 2U, 1U, 2U); returned(&helper, 1U, 1U);
    constant(&helper, 2U, 3U, type, 1, 0.0); add(&helper, 2U, instruction(IR_SUB, 4U, 0U, 3U, type));
    CinderIRInst call = instruction(IR_CALL, 5U, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, type); call.callee_type = helper.type; call.source_type = helper.type; call.callee = cinder_strndup("sum", 3U); CinderValueId input = 4U; bool fp = false;
    cinder_vec_push((CinderVec *)&call.args, &input); cinder_vec_push((CinderVec *)&call.arg_floats, &fp); add(&helper, 2U, call);
    add(&helper, 2U, instruction(IR_ADD, 6U, 0U, 5U, type)); returned(&helper, 2U, 6U);
    CinderIRFunction main_fn = function(module, "main", type); block(&main_fn);
    int64_t n = index % 53U == 0U ? 300 : (int64_t)(random_bits() % 12U);
    constant(&main_fn, 0U, 0U, type, n, 0.0);
    call = instruction(IR_CALL, 1U, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, type); call.callee_type = helper.type; call.source_type = helper.type; call.callee = cinder_strndup("sum", 3U); input = 0U;
    cinder_vec_push((CinderVec *)&call.args, &input); cinder_vec_push((CinderVec *)&call.arg_floats, &fp); add(&main_fn, 0U, call); returned(&main_fn, 0U, 1U);
    cinder_vec_push((CinderVec *)&module->functions, &helper); cinder_vec_push((CinderVec *)&module->functions, &main_fn);
    return (Expected){n * (n + 1) / 2, n == 300 ? INTERP_RESOURCE_LIMIT : INTERP_DEFINED};
}

static bool observe(CinderIRModule *module, Expected expected, CinderDiagnostics *diags) {
    CinderInterpResult actual = cinder_interpret(module, "main", NULL, 0U, 20000U, diags);
    return actual.classification == expected.classification && actual.valid == (expected.classification == INTERP_DEFINED) && (!actual.valid || actual.value == expected.value);
}
static void reset_diags(CinderDiagnostics *diags) { cinder_diags_destroy(diags); cinder_diags_init(diags); }
static char *serialized(CinderIRModule *module, FILE *stream, size_t *length, CinderDiagnostics *diags) {
    rewind(stream);
    if (cinder_write_ir(module, stream, diags) != 0 || fflush(stream) != 0) return NULL;
    long extent = ftell(stream); if (extent < 0) return NULL;
    *length = (size_t)extent; char *text = cinder_alloc(*length + 1U); rewind(stream);
    if (fread(text, 1U, *length, stream) != *length) { free(text); return NULL; }
    text[*length] = '\0'; return text;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    char *end = NULL; errno = 0;
    unsigned long requested = strtoul(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' || argv[1][0] == '-' || requested == 0UL || requested > 1000000UL) return 2;
    unsigned count = (unsigned)requested;
    FILE *archive = fopen(argv[2], "wb"), *first = tmpfile(), *second = tmpfile();
    if (archive == NULL || first == NULL || second == NULL) return 2;
    unsigned defined = 0U, classified = 0U;
    for (unsigned index = 0U; index < count; ++index) {
        rng = UINT64_C(0x81c394746eed17f3) ^ ((uint64_t)index + 1U) * UINT64_C(0x9e3779b97f4a7c15);
        CinderTypeContext types; cinder_types_init(&types); CinderIRModule module; cinder_ir_init(&module, &types);
        CinderDiagnostics diags; cinder_diags_init(&diags);
        unsigned family = index < 65536U ? 0U : 1U + index % 9U;
        Expected expected = family <= 5U ? arithmetic(&module, index, family) : family <= 7U ? floating(&module, index, family) : family == 8U ? memory_join(&module, index) : recursion(&module, index);
        bool passed = cinder_verify_ir(&module, &diags) == 0 && observe(&module, expected, &diags);
        reset_diags(&diags);
        if (family == 8U && passed) passed = cinder_insert_join_phis(&module.functions.data[0], &diags) >= 0 && cinder_verify_ir(&module, &diags) == 0 && observe(&module, expected, &diags);
        reset_diags(&diags);
        size_t length = 0U; char *text = passed ? serialized(&module, first, &length, &diags) : NULL;
        CinderTypeContext parsed_types; cinder_types_init(&parsed_types); CinderIRModule parsed; cinder_ir_init(&parsed, &parsed_types);
        if (text == NULL || cinder_parse_ir(&parsed, text, length, &diags) != 0 || !observe(&parsed, expected, &diags)) passed = false;
        reset_diags(&diags);
        size_t other_length = 0U; char *other = passed ? serialized(&parsed, second, &other_length, &diags) : NULL;
        if (other == NULL || length != other_length || memcmp(text, other, length) != 0) passed = false;
        if (passed) {
            CinderOptStats stats;
            passed = cinder_optimize(&parsed, 2, &stats, &diags) == 0 && cinder_verify_ir(&parsed, &diags) == 0 && observe(&parsed, expected, &diags);
        }
        if (!passed) {
            fprintf(stderr, "IR campaign discrepancy: case=%u family=%u expected=%" PRId64 " class=%u\n", index, family, expected.value, (unsigned)expected.classification);
            if (text != NULL) fwrite(text, 1U, length, stderr);
            CinderSourceManager sources; cinder_sources_init(&sources); cinder_diag_print(&diags, &sources, stderr); cinder_sources_destroy(&sources);
            free(other); free(text); cinder_diags_destroy(&diags); cinder_ir_destroy(&parsed); cinder_types_destroy(&parsed_types); cinder_ir_destroy(&module); cinder_types_destroy(&types);
            fclose(archive); fclose(first); fclose(second);
            return 1;
        }
        long offset = ftell(archive);
        if (offset < 0 || fwrite(text, 1U, length, archive) != length) return 2;
        printf("{\"case\":%u,\"family\":%u,\"expected\":%" PRId64 ",\"classification\":%u,\"archive_offset\":%ld,\"archive_bytes\":%zu,\"roundtrip\":true,\"optimized_match\":true}\n", index, family, expected.value, (unsigned)expected.classification, offset, length);
        if (expected.classification == INTERP_DEFINED) ++defined; else ++classified;
        free(other); free(text); cinder_diags_destroy(&diags); cinder_ir_destroy(&parsed); cinder_types_destroy(&parsed_types); cinder_ir_destroy(&module); cinder_types_destroy(&types);
    }
    if (fclose(archive) != 0 || fclose(first) != 0 || fclose(second) != 0) return 2;
    fprintf(stderr, "IR campaign: %u round-trip/execution/optimization cases; defined=%u classified=%u\n", count, defined, classified);
    return 0;
}
