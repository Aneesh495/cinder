#include "cinder.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define IR_TEXT_LIMIT (64U * 1024U * 1024U)
#define IR_TABLE_LIMIT 1000000U
#define IR_TYPE_LIMIT 65536U

typedef struct { CinderType **data; size_t len; size_t cap; } TypeTable;

static const char *kind_name(CinderTypeKind kind) {
    static const char *names[] = {"error", "void", "bool", "char", "short", "int", "long", "llong", "float", "double", "pointer", "array", "function", "struct", "union", "enum"};
    return (size_t)kind < CINDER_ARRAY_LEN(names) ? names[kind] : "invalid";
}

static size_t type_index(const TypeTable *table, const CinderType *type) {
    for (size_t i = 0U; i < table->len; ++i) if (table->data[i] == type) return i;
    return SIZE_MAX;
}

static bool collect_type(TypeTable *table, CinderType *type, unsigned depth) {
    if (type == NULL || type_index(table, type) != SIZE_MAX) return true;
    if (depth >= 256U || table->len >= IR_TYPE_LIMIT) return false;
    cinder_vec_push((CinderVec *)table, &type);
    if (!collect_type(table, type->base, depth + 1U) || !collect_type(table, type->return_type, depth + 1U)) return false;
    for (size_t i = 0U; i < type->params.len; ++i) if (!collect_type(table, type->params.data[i].type, depth + 1U)) return false;
    for (size_t i = 0U; i < type->fields.len; ++i) if (!collect_type(table, type->fields.data[i].type, depth + 1U)) return false;
    return true;
}

static bool collect_module(TypeTable *table, const CinderIRModule *module) {
    for (size_t g = 0U; g < module->globals.len; ++g) {
        if (!collect_type(table, module->globals.data[g].type, 0U)) return false;
        for (size_t a = 0U; a < module->globals.data[g].addresses.len; ++a) {
            const CinderIRAddress *address = &module->globals.data[g].addresses.data[a];
            if (!collect_type(table, address->pointer_type, 0U) || !collect_type(table, address->target_type, 0U)) return false;
        }
    }
    for (size_t f = 0U; f < module->functions.len; ++f) {
        const CinderIRFunction *function = &module->functions.data[f];
        if (!collect_type(table, function->type, 0U)) return false;
        for (size_t s = 0U; s < function->local_types.len; ++s)
            if (!collect_type(table, function->local_types.data[s], 0U)) return false;
        for (size_t p = 0U; p < function->params.len; ++p)
            if (!collect_type(table, function->params.data[p]->type, 0U)) return false;
        for (size_t b = 0U; b < function->blocks.len; ++b)
            for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
                const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
                if (!collect_type(table, inst->type, 0U) || !collect_type(table, inst->source_type, 0U) || !collect_type(table, inst->callee_type, 0U)) return false;
            }
    }
    return true;
}

static void write_hex(FILE *out, const char *bytes, size_t count) {
    static const char digits[] = "0123456789abcdef";
    if (bytes == NULL) { fputc('-', out); return; }
    fputc('x', out);
    for (size_t i = 0U; i < count; ++i) { unsigned value = (unsigned char)bytes[i]; fputc(digits[value >> 4U], out); fputc(digits[value & 15U], out); }
}

static void write_string(FILE *out, const char *name) { write_hex(out, name, name == NULL ? 0U : strlen(name)); }
static void write_ref(FILE *out, size_t id) { if (id == SIZE_MAX) fputs("none", out); else fprintf(out, "%zu", id); }
static void write_value(FILE *out, CinderValueId id) { if (id == CINDER_INVALID_VALUE) fputs("none", out); else fprintf(out, "%u", id); }
static void write_type_ref(FILE *out, const TypeTable *types, const CinderType *type) { write_ref(out, type_index(types, type)); }
static void write_loc(FILE *out, CinderLoc loc) { fprintf(out, " loc %u %zu %zu %u %u", loc.file, loc.offset, loc.length, loc.line, loc.column); }
static uint64_t float_bits(double value) { uint64_t bits; memcpy(&bits, &value, sizeof(bits)); return bits; }

static void write_type(FILE *out, const TypeTable *table, size_t id) {
    const CinderType *type = table->data[id];
    fprintf(out, "type %zu %s %u %u %u %u %zu %zu ", id, kind_name(type->kind), type->qualifiers, type->complete ? 1U : 0U, type->is_unsigned ? 1U : 0U, type->plain_char ? 1U : 0U, type->size, type->align);
    write_type_ref(out, table, type->base); fprintf(out, " %zu ", type->array_len); write_type_ref(out, table, type->return_type);
    fprintf(out, " %u ", type->variadic ? 1U : 0U); write_string(out, type->tag); fprintf(out, " params %zu fields %zu identity %u\n", type->params.len, type->fields.len, type->identity);
    for (size_t p = 0U; p < type->params.len; ++p) {
        fputs("type-param ", out); write_string(out, type->params.data[p].name); fputc(' ', out); write_type_ref(out, table, type->params.data[p].type); fputc('\n', out);
    }
    for (size_t f = 0U; f < type->fields.len; ++f) {
        const CinderField *field = &type->fields.data[f];
        fputs("field ", out); write_string(out, field->name); fputc(' ', out); write_type_ref(out, table, field->type);
        fprintf(out, " %zu %u %u align %zu bits %u\n", field->offset, field->bit_offset, field->bit_width, field->alignment, field->is_bitfield ? 1U : 0U);
    }
}

static void write_inst(FILE *out, const TypeTable *table, const CinderIRInst *inst) {
    fprintf(out, "inst %s ", cinder_ir_op_name(inst->op)); write_type_ref(out, table, inst->type); fputc(' ', out); write_type_ref(out, table, inst->source_type); fputc(' ', out); write_type_ref(out, table, inst->callee_type);
    fputc(' ', out); write_value(out, inst->dst); fputc(' ', out); write_value(out, inst->left); fputc(' ', out); write_value(out, inst->right);
    fprintf(out, " %016" PRIx64 " %016" PRIx64 " %d %d ", (uint64_t)inst->integer, float_bits(inst->floating), inst->slot, inst->operator_code);
    write_string(out, inst->callee); fprintf(out, " %u noreturn %u args %zu", inst->floating_result ? 1U : 0U, inst->noreturn_call ? 1U : 0U, inst->args.len);
    for (size_t a = 0U; a < inst->args.len; ++a) { fputc(' ', out); write_value(out, inst->args.data[a]); }
    fprintf(out, " classes %zu", inst->arg_floats.len);
    for (size_t a = 0U; a < inst->arg_floats.len; ++a) fprintf(out, " %u", inst->arg_floats.data[a] ? 1U : 0U);
    fprintf(out, " incoming %zu", inst->phi_blocks.len);
    for (size_t p = 0U; p < inst->phi_blocks.len; ++p) fprintf(out, " %u", inst->phi_blocks.data[p]);
    write_loc(out, inst->loc); fputc('\n', out);
}

int cinder_write_ir(const CinderIRModule *module, FILE *out, CinderDiagnostics *diags) {
    if (cinder_verify_ir(module, diags) != 0) return 1;
    TypeTable table = {NULL, 0U, 0U};
    if (!collect_module(&table, module)) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "IR serialization type graph exceeds limits"); free(table.data); return 1; }
    fprintf(out, "cinder-ir 6 lp64-le sysv-x86-64\ntypes %zu\n", table.len);
    for (size_t t = 0U; t < table.len; ++t) write_type(out, &table, t);
    fprintf(out, "globals %zu\n", module->globals.len);
    for (size_t g = 0U; g < module->globals.len; ++g) {
        const CinderIRGlobal *global = &module->globals.data[g];
        fputs("global ", out); write_string(out, global->name); fputc(' ', out); write_type_ref(out, &table, global->type);
        fprintf(out, " %016" PRIx64 " %016" PRIx64 " %u %u %u %u %zu ", (uint64_t)global->integer, float_bits(global->floating), global->read_only ? 1U : 0U, global->global ? 1U : 0U, global->is_extern ? 1U : 0U, global->has_initializer ? 1U : 0U, global->byte_count);
        write_hex(out, global->bytes, global->byte_count);
        fprintf(out, " align %zu", global->alignment);
        fprintf(out, " addresses %zu", global->addresses.len);
        for (size_t a = 0U; a < global->addresses.len; ++a) {
            const CinderIRAddress *address = &global->addresses.data[a];
            fprintf(out, " address %zu ", address->offset); write_string(out, address->symbol);
            fprintf(out, " %016" PRIx64 " ", (uint64_t)address->addend);
            write_type_ref(out, &table, address->pointer_type); fputc(' ', out); write_type_ref(out, &table, address->target_type);
            fprintf(out, " %zu %zu %u", address->domain_begin, address->domain_end, address->function ? 1U : 0U);
            fprintf(out, " origin %zu", address->origin_path.len);
            for (size_t p = 0U; p < address->origin_path.len; ++p) fprintf(out, " %zu", address->origin_path.data[p]);
        }
        write_loc(out, global->loc); fputc('\n', out);
    }
    fprintf(out, "functions %zu\n", module->functions.len);
    for (size_t f = 0U; f < module->functions.len; ++f) {
        const CinderIRFunction *function = &module->functions.data[f];
        fputs("function ", out); write_string(out, function->name); fputc(' ', out); write_type_ref(out, &table, function->type);
        fprintf(out, " %u noreturn %u %zu %zu %zu params %zu blocks %zu\n", function->global ? 1U : 0U, function->is_noreturn ? 1U : 0U, function->value_count, function->local_count, function->float_param_count, function->params.len, function->blocks.len);
        for (size_t l = 0U; l < function->local_types.len; ++l) { fputs("local ", out); write_type_ref(out, &table, function->local_types.data[l]); fprintf(out, " align %zu\n", function->local_alignments.len == 0U ? 0U : function->local_alignments.data[l]); }
        for (size_t p = 0U; p < function->params.len; ++p) { fputs("param ", out); write_string(out, function->params.data[p]->name); fputc(' ', out); write_type_ref(out, &table, function->params.data[p]->type); fputc('\n', out); }
        for (size_t b = 0U; b < function->blocks.len; ++b) {
            const CinderIRBlock *block = &function->blocks.data[b];
            fprintf(out, "block %u ", block->id); write_string(out, block->name); fprintf(out, " instructions %zu predecessors %zu", block->instructions.len, block->predecessors.len);
            for (size_t p = 0U; p < block->predecessors.len; ++p) fprintf(out, " %u", block->predecessors.data[p]);
            fprintf(out, " successors %zu", block->successors.len);
            for (size_t s = 0U; s < block->successors.len; ++s) fprintf(out, " %u", block->successors.data[s]);
            fputc('\n', out);
            for (size_t i = 0U; i < block->instructions.len; ++i) write_inst(out, &table, &block->instructions.data[i]);
            const CinderTerminator *term = &block->terminator;
            static const char *terminators[] = {"unreachable", "return", "jump", "branch"};
            fprintf(out, "term %s ", terminators[term->kind]); write_value(out, term->value); fputc(' ', out); write_value(out, term->target); fputc(' ', out); write_value(out, term->yes); fputc(' ', out); write_value(out, term->no); fputc(' ', out); write_value(out, term->condition);
            write_loc(out, term->loc); fputs("\nend-block\n", out);
        }
        fputs("end-function\n", out);
    }
    fputs("end-module\n", out); free(table.data);
    if (ferror(out)) { cinder_diag(diags, CINDER_ERROR, (CinderLoc){0}, "cannot write serialized IR"); return 1; }
    return 0;
}

typedef struct {
    const char *text;
    size_t length;
    size_t cursor;
    unsigned line;
    unsigned tokens;
    bool failed;
    CinderIRModule *module;
    TypeTable types;
    CinderDiagnostics *diags;
} Reader;

typedef struct { const char *data; size_t length; } Word;

static void parse_error(Reader *reader, const char *reason) {
    if (!reader->failed) cinder_diag(reader->diags, CINDER_ERROR, (CinderLoc){CINDER_NO_FILE, reader->cursor, 0U, reader->line, 1U}, "IR text: %s", reason);
    reader->failed = true;
}

static bool space(char value) { return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v'; }

static Word word(Reader *reader) {
    if (reader->failed) return (Word){"", 0U};
    while (reader->cursor < reader->length) {
        char ch = reader->text[reader->cursor];
        if (space(ch)) { if (ch == '\n') ++reader->line; ++reader->cursor; }
        else if (ch == '#') { while (reader->cursor < reader->length && reader->text[reader->cursor] != '\n') ++reader->cursor; }
        else break;
    }
    size_t begin = reader->cursor;
    while (reader->cursor < reader->length && !space(reader->text[reader->cursor])) ++reader->cursor;
    size_t count = reader->cursor - begin;
    if (count == 0U) parse_error(reader, "unexpected end of input");
    if (count > 2U * IR_TABLE_LIMIT || ++reader->tokens > 8U * IR_TABLE_LIMIT) parse_error(reader, "token resource limit exceeded");
    return (Word){reader->text + begin, count};
}

static bool equal(Word value, const char *text) { return strlen(text) == value.length && memcmp(value.data, text, value.length) == 0; }
static void expect(Reader *reader, const char *text) { if (!equal(word(reader), text)) parse_error(reader, text); }

static int digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static uint64_t number_word(Reader *reader, Word value, unsigned base, uint64_t maximum) {
    uint64_t result = 0U;
    if (value.length == 0U) { parse_error(reader, "missing numeric field"); return 0U; }
    for (size_t i = 0U; i < value.length; ++i) {
        int part = digit(value.data[i]);
        if (part < 0 || (unsigned)part >= base || (uint64_t)part > maximum || result > (maximum - (unsigned)part) / base) { parse_error(reader, "invalid or overflowing numeric field"); return 0U; }
        result = result * base + (unsigned)part;
    }
    return result;
}

static uint64_t number(Reader *reader, uint64_t maximum) { return number_word(reader, word(reader), 10U, maximum); }
static bool boolean(Reader *reader) { return number(reader, 1U) != 0U; }
static uint64_t bits(Reader *reader) { Word value = word(reader); if (value.length != 16U) parse_error(reader, "bit pattern must have sixteen hex digits"); return number_word(reader, value, 16U, UINT64_MAX); }
static int64_t signed_bits(uint64_t value) { return value <= (uint64_t)INT64_MAX ? (int64_t)value : -1 - (int64_t)~value; }
static double float_from_bits(uint64_t value) { double result; memcpy(&result, &value, sizeof(result)); return result; }

static int signed_number(Reader *reader) {
    Word value = word(reader); bool negative = value.length != 0U && value.data[0] == '-';
    if (negative) { ++value.data; --value.length; }
    uint64_t magnitude = number_word(reader, value, 10U, negative ? (uint64_t)INT_MAX + 1U : (uint64_t)INT_MAX);
    if (!negative) return (int)magnitude;
    return magnitude == (uint64_t)INT_MAX + 1U ? INT_MIN : -(int)magnitude;
}

static CinderValueId value_ref(Reader *reader) {
    Word value = word(reader);
    return equal(value, "none") ? CINDER_INVALID_VALUE : (CinderValueId)number_word(reader, value, 10U, CINDER_INVALID_VALUE - 1U);
}

static CinderType *type_ref(Reader *reader) {
    Word value = word(reader);
    if (equal(value, "none")) return NULL;
    size_t index = (size_t)number_word(reader, value, 10U, reader->types.len == 0U ? 0U : reader->types.len - 1U);
    if (reader->failed || index >= reader->types.len) { parse_error(reader, "unknown type reference"); return NULL; }
    return reader->types.data[index];
}

static char *hex_bytes(Reader *reader, size_t *length) {
    Word value = word(reader); *length = 0U;
    if (equal(value, "-")) return NULL;
    if (value.length == 0U || value.data[0] != 'x' || (value.length & 1U) == 0U) { parse_error(reader, "invalid hexadecimal string"); return NULL; }
    size_t count = (value.length - 1U) / 2U;
    char *result = cinder_arena_alloc(&reader->module->types->arena, count + 1U, 1U);
    for (size_t i = 0U; i < count; ++i) {
        int high = digit(value.data[1U + i * 2U]), low = digit(value.data[2U + i * 2U]);
        if (high < 0 || low < 0) { parse_error(reader, "invalid hexadecimal string digit"); return NULL; }
        result[i] = (char)((unsigned)high * 16U + (unsigned)low);
    }
    result[count] = '\0'; *length = count; return result;
}

static char *string(Reader *reader, bool owned, bool required) {
    size_t count; char *result = hex_bytes(reader, &count);
    if (result != NULL && memchr(result, '\0', count) != NULL) parse_error(reader, "NUL is invalid in a name");
    if (required && (result == NULL || count == 0U)) parse_error(reader, "missing name");
    return owned && result != NULL ? cinder_strndup(result, count) : result;
}

static CinderLoc location(Reader *reader) {
    expect(reader, "loc"); CinderLoc loc;
    loc.file = (CinderFileId)number(reader, UINT32_MAX); loc.offset = (size_t)number(reader, SIZE_MAX);
    loc.length = (size_t)number(reader, SIZE_MAX); loc.line = (unsigned)number(reader, UINT_MAX); loc.column = (unsigned)number(reader, UINT_MAX);
    if (loc.offset > SIZE_MAX - loc.length) parse_error(reader, "source range overflows");
    return loc;
}

static CinderTypeKind parse_kind(Reader *reader) {
    Word value = word(reader);
    for (unsigned k = 0U; k <= TYPE_ENUM; ++k) if (equal(value, kind_name((CinderTypeKind)k))) return (CinderTypeKind)k;
    parse_error(reader, "unknown type kind"); return TYPE_ERROR;
}

static CinderIROp parse_op(Reader *reader) {
    Word value = word(reader);
    for (unsigned op = 0U; op <= IR_UNDEF; ++op) if (equal(value, cinder_ir_op_name((CinderIROp)op))) return (CinderIROp)op;
    parse_error(reader, "unknown opcode"); return IR_NOP;
}

static void read_types(Reader *reader) {
    expect(reader, "types"); size_t count = (size_t)number(reader, IR_TYPE_LIMIT);
    for (size_t i = 0U; i < count && !reader->failed; ++i) {
        CinderType *type = cinder_type_new(reader->module->types, TYPE_ERROR); cinder_vec_push((CinderVec *)&reader->types, &type);
    }
    for (size_t i = 0U; i < count && !reader->failed; ++i) {
        expect(reader, "type"); if (number(reader, count) != i) parse_error(reader, "type IDs must be consecutive");
        CinderType *type = reader->types.data[i]; type->kind = parse_kind(reader);
        type->qualifiers = (unsigned)number(reader, 7U); type->complete = boolean(reader); type->is_unsigned = boolean(reader); type->plain_char = boolean(reader);
        type->size = (size_t)number(reader, IR_TEXT_LIMIT); type->align = (size_t)number(reader, 4096U);
        type->base = type_ref(reader); type->array_len = (size_t)number(reader, IR_TABLE_LIMIT); type->return_type = type_ref(reader);
        type->variadic = boolean(reader); type->tag = string(reader, false, false);
        expect(reader, "params"); size_t parameters = (size_t)number(reader, 4096U); expect(reader, "fields"); size_t fields = (size_t)number(reader, 65536U);
        expect(reader, "identity"); type->identity = (uint32_t)number(reader, UINT32_MAX);
        if (type->identity == 0U) parse_error(reader, "type identity must be nonzero");
        for (size_t p = 0U; p < parameters && !reader->failed; ++p) {
            expect(reader, "type-param"); CinderParam param; param.name = string(reader, false, false); param.type = type_ref(reader); param.declaration_flags = 0U;
            if (param.type == NULL) parse_error(reader, "parameter has no type");
            cinder_vec_push((CinderVec *)&type->params, &param);
        }
        for (size_t f = 0U; f < fields && !reader->failed; ++f) {
            expect(reader, "field"); CinderField field; field.name = string(reader, false, false); field.type = type_ref(reader);
            field.offset = (size_t)number(reader, IR_TEXT_LIMIT); field.bit_offset = (unsigned)number(reader, 63U); field.bit_width = (unsigned)number(reader, 64U);
            expect(reader, "align"); field.alignment = (size_t)number(reader, 16U); expect(reader, "bits"); field.is_bitfield = boolean(reader);
            if (field.type == NULL) parse_error(reader, "field has no type");
            cinder_vec_push((CinderVec *)&type->fields, &field);
        }
    }
}

static bool finite_object(Reader *reader, const CinderType *type, bool *active, unsigned depth) {
    if (type->kind != TYPE_ARRAY && type->kind != TYPE_STRUCT && type->kind != TYPE_UNION) return true;
    size_t index = type_index(&reader->types, type);
    if (index == SIZE_MAX || depth >= 256U || active[index]) { parse_error(reader, "aggregate has a cyclic or excessively nested by-value member"); return false; }
    active[index] = true;
    bool valid = true;
    if (type->kind == TYPE_ARRAY) valid = type->base != NULL && finite_object(reader, type->base, active, depth + 1U);
    else for (size_t f = 0U; f < type->fields.len && valid; ++f) valid = finite_object(reader, type->fields.data[f].type, active, depth + 1U);
    active[index] = false;
    return valid;
}

static bool integer_type(const CinderType *type) {
    return (type->kind >= TYPE_BOOL && type->kind <= TYPE_LLONG) || type->kind == TYPE_ENUM;
}

static bool validate_type(Reader *reader, const CinderType *type, bool *active, unsigned depth) {
    size_t index = type_index(&reader->types, type);
    if (index == SIZE_MAX || depth >= 256U || active[index]) { parse_error(reader, "invalid cyclic or excessively nested type graph"); return false; }
    if (type->kind == TYPE_ERROR) { parse_error(reader, "error types cannot appear in accepted IR"); return false; }
    if (type->align == 0U || (type->align & (type->align - 1U)) != 0U) { parse_error(reader, "type alignment is not a power of two"); return false; }
    size_t size = type->kind == TYPE_BOOL || type->kind == TYPE_CHAR ? 1U : type->kind == TYPE_SHORT ? 2U : type->kind == TYPE_INT || type->kind == TYPE_ENUM || type->kind == TYPE_FLOAT ? 4U : 8U;
    if (((type->kind >= TYPE_BOOL && type->kind <= TYPE_POINTER) || type->kind == TYPE_ENUM) && (type->size != size || type->align != size || !type->complete)) { parse_error(reader, "scalar layout disagrees with LP64 target"); return false; }
    if (type->kind == TYPE_VOID && (type->size != 0U || type->align != 1U)) { parse_error(reader, "invalid void layout"); return false; }
    if (type->plain_char && type->kind != TYPE_CHAR) { parse_error(reader, "plain-char attribute on another type"); return false; }
    if (type->is_unsigned && !integer_type(type)) { parse_error(reader, "unsigned attribute on a noninteger type"); return false; }
    if ((type->kind != TYPE_FUNCTION && type->params.len != 0U) || ((type->kind != TYPE_STRUCT && type->kind != TYPE_UNION) && type->fields.len != 0U)) { parse_error(reader, "unexpected type members"); return false; }
    /* Aggregate identity terminates recursive structural paths. Field layout
     * is separately checked against the declared object extent below. */
    if (type->kind == TYPE_STRUCT || type->kind == TYPE_UNION) {
        if (type->base != NULL || type->return_type != NULL || type->variadic || (type->complete && (type->size == 0U || type->size % type->align != 0U)) || (!type->complete && (type->size != 0U || type->fields.len != 0U))) { parse_error(reader, "invalid aggregate layout or attributes"); return false; }
        size_t extent = 0U, aggregate_align = 1U, bit_cursor = 0U, named = 0U;
        for (size_t f = 0U; f < type->fields.len; ++f) {
            const CinderField *field = &type->fields.data[f];
            if (field->is_bitfield) {
                if ((field->type->kind != TYPE_BOOL && field->type->kind != TYPE_INT) || field->alignment != 0U || field->bit_width > (field->type->kind == TYPE_BOOL ? 1U : 32U) || (field->name != NULL && field->bit_width == 0U)) { parse_error(reader, "invalid target bitfield type, width, or alignment"); return false; }
                if (!validate_type(reader, field->type, active, depth + 1U)) return false;
                size_t unit = field->type->size * 8U;
                if (field->name != NULL) { ++named; if (field->type->align > aggregate_align) aggregate_align = field->type->align; }
                size_t expected_offset = 0U, expected_bit = 0U;
                if (type->kind == TYPE_UNION) {
                    size_t bytes = field->name == NULL ? (field->bit_width + 7U) / 8U : field->type->size;
                    if (bytes > extent) extent = bytes;
                } else {
                    if (field->bit_width == 0U || bit_cursor % unit + field->bit_width > unit) bit_cursor = (bit_cursor + unit - 1U) / unit * unit;
                    expected_offset = bit_cursor / unit * field->type->size; expected_bit = bit_cursor % unit;
                    bit_cursor += field->bit_width; extent = (bit_cursor + 7U) / 8U;
                }
                if (field->offset != expected_offset || field->bit_offset != expected_bit || field->bit_offset + field->bit_width > unit || field->offset > type->size || (field->bit_width != 0U && (field->bit_offset + field->bit_width + 7U) / 8U > type->size - field->offset)) { parse_error(reader, "bitfield storage disagrees with target layout"); return false; }
                continue;
            }
            if (field->bit_offset != 0U || field->bit_width != 0U || !cinder_object_alignment_valid(field->type, field->alignment)) { parse_error(reader, "invalid declared ordinary member alignment or bit metadata"); return false; }
            size_t alignment = field->alignment == 0U ? field->type->align : field->alignment;
            if (alignment > aggregate_align) aggregate_align = alignment;
            size_t expected = type->kind == TYPE_UNION ? 0U : (extent + alignment - 1U) & ~(alignment - 1U);
            if (field->offset != expected) { parse_error(reader, "member offset disagrees with target alignment layout"); return false; }
            bool flexible = field->type->kind == TYPE_ARRAY && !field->type->complete && field->type->base != NULL && field->type->base->complete;
            if (flexible && (type->kind != TYPE_STRUCT || f + 1U != type->fields.len || field->name == NULL || named == 0U || field->type->size != 0U || field->type->array_len != 0U)) { parse_error(reader, "invalid flexible array member placement or extent"); return false; }
            if ((!field->type->complete && !flexible) || field->type->kind == TYPE_VOID || field->type->kind == TYPE_FUNCTION || field->offset > type->size || field->type->size > type->size - field->offset || (type->kind == TYPE_STRUCT && cinder_type_contains_flexible(field->type))) { parse_error(reader, "field is outside aggregate extent or has an invalid object type"); return false; }
            if (field->name != NULL) ++named;
            else if (cinder_type_anonymous_member(field)) named += cinder_type_named_members(field->type);
            size_t end = expected + field->type->size;
            if (end > extent) extent = end;
            bit_cursor = extent * 8U;
            for (size_t previous = 0U; previous < f; ++previous)
                if (field->name != NULL && type->fields.data[previous].name != NULL && strcmp(field->name, type->fields.data[previous].name) == 0) { parse_error(reader, "duplicate aggregate member"); return false; }
        }
        if (type->complete && (type->fields.len == 0U || type->align != aggregate_align || type->size != ((extent + aggregate_align - 1U) & ~(aggregate_align - 1U)))) { parse_error(reader, "aggregate extent or alignment disagrees with target layout"); return false; }
        if (type->complete && !cinder_type_members_unique(type)) { parse_error(reader, "duplicate promoted member or invalid anonymous aggregate member"); return false; }
        return finite_object(reader, type, active, depth);
    }
    active[index] = true;
    if (type->kind == TYPE_POINTER || type->kind == TYPE_ARRAY) {
        if (type->base == NULL || !validate_type(reader, type->base, active, depth + 1U)) return false;
        if (type->kind == TYPE_ARRAY && (type->base->kind == TYPE_VOID || type->base->kind == TYPE_FUNCTION || type->base->size == 0U || type->array_len > SIZE_MAX / type->base->size || type->size != type->array_len * type->base->size || type->align != type->base->align)) { parse_error(reader, "invalid array layout"); return false; }
        if (type->kind == TYPE_ARRAY && cinder_type_contains_flexible(type->base)) { parse_error(reader, "array element contains a flexible array member"); return false; }
    } else if (type->base != NULL) { parse_error(reader, "unexpected base type"); return false; }
    if (type->kind == TYPE_FUNCTION) {
        if (type->size != 0U || type->align != 1U || !type->complete) { parse_error(reader, "invalid function layout"); return false; }
        if (type->return_type == NULL || type->return_type->kind == TYPE_ARRAY || type->return_type->kind == TYPE_FUNCTION || !validate_type(reader, type->return_type, active, depth + 1U)) { parse_error(reader, "invalid function return type"); return false; }
        for (size_t p = 0U; p < type->params.len; ++p)
            if (type->params.data[p].type->kind == TYPE_VOID || !validate_type(reader, type->params.data[p].type, active, depth + 1U)) { parse_error(reader, "invalid function parameter type"); return false; }
    } else if (type->return_type != NULL || type->variadic) { parse_error(reader, "unexpected function attributes"); return false; }
    active[index] = false; return true;
}

static void read_globals(Reader *reader) {
    expect(reader, "globals"); size_t count = (size_t)number(reader, IR_TABLE_LIMIT);
    for (size_t g = 0U; g < count && !reader->failed; ++g) {
        expect(reader, "global"); CinderIRGlobal global; memset(&global, 0, sizeof(global));
        global.name = string(reader, true, true); global.type = type_ref(reader);
        global.integer = signed_bits(bits(reader)); global.floating = float_from_bits(bits(reader));
        global.read_only = boolean(reader); global.global = boolean(reader); global.is_extern = boolean(reader); global.has_initializer = boolean(reader);
        global.byte_count = (size_t)number(reader, IR_TEXT_LIMIT); size_t actual; global.bytes = hex_bytes(reader, &actual);
        if (actual != global.byte_count || global.type == NULL) parse_error(reader, "global storage does not match its declaration");
        expect(reader, "align"); global.alignment = (size_t)number(reader, 16U);
        expect(reader, "addresses"); size_t addresses = (size_t)number(reader, IR_TABLE_LIMIT);
        for (size_t a = 0U; a < addresses && !reader->failed; ++a) {
            expect(reader, "address"); CinderIRAddress address; memset(&address, 0, sizeof(address));
            address.offset = (size_t)number(reader, IR_TEXT_LIMIT); address.symbol = string(reader, true, true);
            address.addend = signed_bits(bits(reader)); address.pointer_type = type_ref(reader); address.target_type = type_ref(reader);
            address.domain_begin = (size_t)number(reader, IR_TEXT_LIMIT); address.domain_end = (size_t)number(reader, IR_TEXT_LIMIT);
            address.function = boolean(reader); expect(reader, "origin"); size_t length = (size_t)number(reader, 256U);
            for (size_t p = 0U; p < length && !reader->failed; ++p) { size_t field = (size_t)number(reader, 65535U); cinder_vec_push((CinderVec *)&address.origin_path, &field); }
            cinder_vec_push((CinderVec *)&global.addresses, &address);
        }
        global.loc = location(reader); cinder_vec_push((CinderVec *)&reader->module->globals, &global);
    }
}

static CinderIRInst read_inst(Reader *reader) {
    expect(reader, "inst"); CinderIRInst inst; memset(&inst, 0, sizeof(inst));
    inst.op = parse_op(reader); inst.type = type_ref(reader); inst.source_type = type_ref(reader); inst.callee_type = type_ref(reader);
    inst.dst = value_ref(reader); inst.left = value_ref(reader); inst.right = value_ref(reader);
    inst.integer = signed_bits(bits(reader)); inst.floating = float_from_bits(bits(reader)); inst.slot = signed_number(reader); inst.operator_code = signed_number(reader);
    inst.callee = string(reader, true, false); inst.floating_result = boolean(reader);
    expect(reader, "noreturn"); inst.noreturn_call = boolean(reader);
    expect(reader, "args"); size_t args = (size_t)number(reader, 4096U);
    for (size_t a = 0U; a < args && !reader->failed; ++a) { CinderValueId value = value_ref(reader); cinder_vec_push((CinderVec *)&inst.args, &value); }
    expect(reader, "classes"); size_t classes = (size_t)number(reader, 4096U);
    for (size_t a = 0U; a < classes && !reader->failed; ++a) { bool fp = boolean(reader); cinder_vec_push((CinderVec *)&inst.arg_floats, &fp); }
    expect(reader, "incoming"); size_t incoming = (size_t)number(reader, 4096U);
    for (size_t a = 0U; a < incoming && !reader->failed; ++a) { CinderBlockId block = value_ref(reader); cinder_vec_push((CinderVec *)&inst.phi_blocks, &block); }
    inst.loc = location(reader); return inst;
}

static CinderTerminator read_term(Reader *reader) {
    expect(reader, "term"); Word name = word(reader); CinderTerminator term; memset(&term, 0, sizeof(term));
    if (equal(name, "unreachable")) term.kind = TERM_UNREACHABLE;
    else if (equal(name, "return")) term.kind = TERM_RETURN;
    else if (equal(name, "jump")) term.kind = TERM_JUMP;
    else if (equal(name, "branch")) term.kind = TERM_BRANCH;
    else parse_error(reader, "unknown terminator");
    term.value = value_ref(reader); term.target = value_ref(reader); term.yes = value_ref(reader); term.no = value_ref(reader); term.condition = value_ref(reader);
    term.loc = location(reader); return term;
}

static void read_functions(Reader *reader) {
    expect(reader, "functions"); size_t count = (size_t)number(reader, 65536U);
    for (size_t f = 0U; f < count && !reader->failed; ++f) {
        expect(reader, "function"); CinderIRFunction function; memset(&function, 0, sizeof(function));
        function.name = string(reader, true, true); function.type = type_ref(reader); function.types = reader->module->types;
        function.global = boolean(reader); expect(reader, "noreturn"); function.is_noreturn = boolean(reader); function.value_count = (size_t)number(reader, IR_TABLE_LIMIT); function.local_count = (size_t)number(reader, 65536U); function.float_param_count = (size_t)number(reader, 4096U);
        expect(reader, "params"); size_t params = (size_t)number(reader, 4096U); expect(reader, "blocks"); size_t blocks = (size_t)number(reader, 65536U);
        if (function.type == NULL || function.type->kind != TYPE_FUNCTION || blocks == 0U || params != function.type->params.len) parse_error(reader, "invalid function definition");
        cinder_vec_push((CinderVec *)&reader->module->functions, &function);
        CinderIRFunction *published = &reader->module->functions.data[reader->module->functions.len - 1U];
        for (size_t l = 0U; l < function.local_count && !reader->failed; ++l) {
            expect(reader, "local"); CinderType *type = type_ref(reader);
            if (type == NULL) parse_error(reader, "local has no type");
            cinder_vec_push((CinderVec *)&published->local_types, &type);
            expect(reader, "align"); size_t alignment = (size_t)number(reader, 16U);
            cinder_vec_push((CinderVec *)&published->local_alignments, &alignment);
        }
        for (size_t p = 0U; p < params && !reader->failed; ++p) {
            expect(reader, "param"); CinderDecl *param = cinder_arena_alloc(&reader->module->arena, sizeof(*param), _Alignof(CinderDecl));
            memset(param, 0, sizeof(*param)); param->kind = DECL_VAR; param->name = string(reader, false, false); param->type = type_ref(reader);
            if (param->type == NULL) parse_error(reader, "parameter has no type");
            cinder_vec_push((CinderVec *)&published->params, &param);
        }
        for (size_t b = 0U; b < blocks && !reader->failed; ++b) {
            expect(reader, "block"); CinderIRBlock block; memset(&block, 0, sizeof(block));
            block.id = value_ref(reader); block.name = string(reader, true, true);
            expect(reader, "instructions"); size_t instructions = (size_t)number(reader, IR_TABLE_LIMIT);
            expect(reader, "predecessors"); size_t predecessors = (size_t)number(reader, 4096U);
            cinder_vec_push((CinderVec *)&published->blocks, &block);
            CinderIRBlock *current = &published->blocks.data[published->blocks.len - 1U];
            for (size_t p = 0U; p < predecessors && !reader->failed; ++p) { CinderBlockId id = value_ref(reader); cinder_vec_push((CinderVec *)&current->predecessors, &id); }
            expect(reader, "successors"); size_t successors = (size_t)number(reader, 4096U);
            for (size_t s = 0U; s < successors && !reader->failed; ++s) { CinderBlockId id = value_ref(reader); cinder_vec_push((CinderVec *)&current->successors, &id); }
            for (size_t i = 0U; i < instructions && !reader->failed; ++i) { CinderIRInst inst = read_inst(reader); cinder_vec_push((CinderVec *)&current->instructions, &inst); }
            current->terminator = read_term(reader); expect(reader, "end-block");
        }
        expect(reader, "end-function");
    }
}

int cinder_parse_ir(CinderIRModule *module, const char *text, size_t length, CinderDiagnostics *diags) {
    Reader reader; memset(&reader, 0, sizeof(reader)); reader.text = text; reader.length = length; reader.line = 1U; reader.module = module; reader.diags = diags;
    if (length > IR_TEXT_LIMIT || memchr(text, '\0', length) != NULL || module->functions.len != 0U || module->globals.len != 0U) { parse_error(&reader, "invalid input extent or nonempty destination module"); return 1; }
    expect(&reader, "cinder-ir"); if (number(&reader, 6U) != 6U) parse_error(&reader, "unsupported IR schema version");
    expect(&reader, "lp64-le"); expect(&reader, "sysv-x86-64"); read_types(&reader);
    bool *active = cinder_alloc((reader.types.len == 0U ? 1U : reader.types.len) * sizeof(*active)); memset(active, 0, reader.types.len * sizeof(*active));
    for (size_t t = 0U; t < reader.types.len && !reader.failed; ++t) (void)validate_type(&reader, reader.types.data[t], active, 0U);
    free(active); read_globals(&reader); read_functions(&reader); expect(&reader, "end-module");
    while (reader.cursor < reader.length && space(reader.text[reader.cursor])) ++reader.cursor;
    if (reader.cursor != reader.length) parse_error(&reader, "trailing input after module");
    free(reader.types.data);
    if (reader.failed) return 1;
    return cinder_verify_ir(module, diags);
}
