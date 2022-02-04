#ifndef CINDER_H
#define CINDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CINDER_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define CINDER_MAX(a, b) ((a) > (b) ? (a) : (b))
#define CINDER_MIN(a, b) ((a) < (b) ? (a) : (b))

/* ---------- checked storage primitives ---------- */
typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} CinderVec;

void cinder_vec_init(CinderVec *v, size_t elem_size);
void cinder_vec_free(CinderVec *v);
void *cinder_vec_push_impl(CinderVec *v, const void *elem, size_t elem_size);
void *cinder_vec_at_impl(CinderVec *v, size_t index, size_t elem_size);
const void *cinder_vec_cat_impl(const CinderVec *v, size_t index, size_t elem_size);
#define cinder_vec_push(v, elem) cinder_vec_push_impl((CinderVec *)(v), (elem), sizeof(*(elem)))
#define cinder_vec_at(v, index) cinder_vec_at_impl((CinderVec *)(v), (index), sizeof((v)->data[0]))
#define cinder_vec_cat(v, index) cinder_vec_cat_impl((const CinderVec *)(v), (index), sizeof((v)->data[0]))

#define CINDER_VEC_TYPE(T) struct { T *data; size_t len; size_t cap; }

void *cinder_alloc(size_t size);
void *cinder_realloc(void *ptr, size_t size);
char *cinder_strndup(const char *text, size_t len);

/* ---------- arenas ---------- */
typedef struct CinderArenaBlock CinderArenaBlock;
typedef struct {
    CinderArenaBlock *first;
    CinderArenaBlock *last;
    size_t block_size;
} CinderArena;

void cinder_arena_init(CinderArena *arena, size_t block_size);
void cinder_arena_destroy(CinderArena *arena);
void *cinder_arena_alloc(CinderArena *arena, size_t size, size_t align);
char *cinder_arena_strndup(CinderArena *arena, const char *text, size_t len);

/* ---------- source and diagnostics ---------- */
typedef uint32_t CinderFileId;
typedef struct {
    CinderFileId file;
    size_t offset;
    size_t length;
    unsigned line;
    unsigned column;
} CinderLoc;

#define CINDER_NO_FILE ((CinderFileId)0)
typedef struct {
    CinderFileId id;
    char *path;
    char *bytes;
    size_t size;
} CinderSourceFile;

typedef struct {
    CinderArena arena;
    CINDER_VEC_TYPE(CinderSourceFile) files;
    char *preprocessed;
    size_t preprocessed_size;
} CinderSourceManager;

void cinder_sources_init(CinderSourceManager *sources);
void cinder_sources_destroy(CinderSourceManager *sources);
CinderFileId cinder_source_load(CinderSourceManager *sources, const char *path, FILE *err);
CinderSourceFile *cinder_source_get(CinderSourceManager *sources, CinderFileId id);
CinderLoc cinder_loc(CinderFileId file, size_t offset, size_t length);
void cinder_loc_linecol(CinderSourceManager *sources, CinderLoc *loc);
const char *cinder_source_name(CinderSourceManager *sources, CinderFileId id);

typedef enum {
    CINDER_NOTE,
    CINDER_WARNING,
    CINDER_ERROR,
    CINDER_FATAL,
} CinderSeverity;

typedef struct {
    CinderSeverity severity;
    CinderLoc loc;
    char *message;
} CinderDiagnostic;

typedef struct {
    CINDER_VEC_TYPE(CinderDiagnostic) items;
    unsigned errors;
    unsigned warnings;
    bool json;
} CinderDiagnostics;

void cinder_diags_init(CinderDiagnostics *diags);
void cinder_diags_destroy(CinderDiagnostics *diags);
void cinder_diag(CinderDiagnostics *diags, CinderSeverity severity, CinderLoc loc, const char *fmt, ...);
void cinder_diag_print(CinderDiagnostics *diags, CinderSourceManager *sources, FILE *out);

/* ---------- preprocessing and tokens ---------- */
typedef enum {
    TOK_EOF = 256,
    TOK_IDENTIFIER,
    TOK_NUMBER,
    TOK_STRING,
    TOK_CHAR,
    TOK_ARROW,
    TOK_EQEQ,
    TOK_NEQ,
    TOK_LE,
    TOK_GE,
    TOK_ANDAND,
    TOK_OROR,
    TOK_SHL,
    TOK_SHR,
    TOK_PLUSPLUS,
    TOK_MINUSMINUS,
    TOK_PLUSEQ,
    TOK_MINUSEQ,
    TOK_STAREQ,
    TOK_SLASHEQ,
    TOK_PERCENTEQ,
    TOK_ANDEQ,
    TOK_OREQ,
    TOK_XOREQ,
    TOK_LSHIFT_EQ,
    TOK_RSHIFT_EQ,
    TOK_ELLIPSIS,
    TOK_KW_AUTO,
    TOK_KW_BREAK,
    TOK_KW_CASE,
    TOK_KW_CHAR,
    TOK_KW_CONST,
    TOK_KW_CONTINUE,
    TOK_KW_DEFAULT,
    TOK_KW_DO,
    TOK_KW_DOUBLE,
    TOK_KW_ELSE,
    TOK_KW_ENUM,
    TOK_KW_EXTERN,
    TOK_KW_FLOAT,
    TOK_KW_FOR,
    TOK_KW_GOTO,
    TOK_KW_IF,
    TOK_KW_INLINE,
    TOK_KW_INT,
    TOK_KW_LONG,
    TOK_KW_REGISTER,
    TOK_KW_RESTRICT,
    TOK_KW_RETURN,
    TOK_KW_SHORT,
    TOK_KW_SIGNED,
    TOK_KW_SIZEOF,
    TOK_KW_STATIC,
    TOK_KW_STRUCT,
    TOK_KW_SWITCH,
    TOK_KW_TYPEDEF,
    TOK_KW_UNION,
    TOK_KW_UNSIGNED,
    TOK_KW_VOID,
    TOK_KW_VOLATILE,
    TOK_KW_WHILE,
    TOK_KW__BOOL,
    TOK_KW_ALIGNAS,
    TOK_KW_ALIGNOF,
    TOK_KW_GENERIC,
    TOK_KW_NORETURN,
    TOK_KW_STATIC_ASSERT,
} CinderTokenKind;

typedef struct {
    CinderTokenKind kind;
    const char *text;
    size_t length;
    CinderLoc loc;
    int64_t integer;
    bool is_floating;
    double floating;
} CinderToken;

typedef struct {
    CINDER_VEC_TYPE(CinderToken) tokens;
    char *text;
    size_t text_len;
} CinderTokenStream;

void cinder_tokens_init(CinderTokenStream *tokens);
void cinder_tokens_destroy(CinderTokenStream *tokens);
int cinder_preprocess(CinderSourceManager *sources, const char *path, const char *const *include_dirs, size_t include_count, const char *const *defines, size_t define_count, CinderDiagnostics *diags);
int cinder_lex(CinderSourceManager *sources, CinderTokenStream *tokens, CinderDiagnostics *diags);
const char *cinder_token_name(CinderTokenKind kind);
void cinder_dump_tokens(const CinderTokenStream *tokens, CinderSourceManager *sources, FILE *out);

/* ---------- target types ---------- */
typedef enum {
    TYPE_ERROR,
    TYPE_VOID,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_SHORT,
    TYPE_INT,
    TYPE_LONG,
    TYPE_LLONG,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_POINTER,
    TYPE_ARRAY,
    TYPE_FUNCTION,
    TYPE_STRUCT,
    TYPE_UNION,
    TYPE_ENUM,
} CinderTypeKind;

typedef struct CinderType CinderType;
typedef struct CinderField CinderField;
typedef struct CinderParam CinderParam;
struct CinderField {
    char *name;
    CinderType *type;
    size_t offset;
    unsigned bit_offset;
    unsigned bit_width;
};
struct CinderParam {
    char *name;
    CinderType *type;
};
typedef struct {
    CinderParam *data;
    size_t len;
    size_t cap;
} CinderParamVec;
struct CinderType {
    CinderTypeKind kind;
    unsigned qualifiers;
    bool complete;
    bool is_unsigned;
    size_t size;
    size_t align;
    CinderType *base;
    size_t array_len;
    CinderParamVec params;
    CinderType *return_type;
    CINDER_VEC_TYPE(CinderField) fields;
    char *tag;
};

typedef struct {
    CinderArena arena;
    CinderType *error_type;
    CinderType *void_type;
    CinderType *bool_type;
    CinderType *char_type;
    CinderType *short_type;
    CinderType *int_type;
    CinderType *uint_type;
    CinderType *long_type;
    CinderType *ulong_type;
    CinderType *llong_type;
    CinderType *ullong_type;
    CinderType *float_type;
    CinderType *double_type;
} CinderTypeContext;

void cinder_types_init(CinderTypeContext *types);
void cinder_types_destroy(CinderTypeContext *types);
CinderType *cinder_type_new(CinderTypeContext *types, CinderTypeKind kind);
CinderType *cinder_type_pointer(CinderTypeContext *types, CinderType *base);
CinderType *cinder_type_array(CinderTypeContext *types, CinderType *base, size_t length);
CinderType *cinder_type_function(CinderTypeContext *types, CinderType *ret, const CinderParamVec *params);
bool cinder_type_equal(const CinderType *a, const CinderType *b);
bool cinder_type_compatible(const CinderType *a, const CinderType *b);
const char *cinder_type_name(const CinderType *type);
int cinder_type_layout_aggregate(CinderType *type, CinderDiagnostics *diags, CinderLoc loc);
/* ---------- AST ---------- */
typedef struct CinderExpr CinderExpr;
typedef struct CinderStmt CinderStmt;
typedef struct CinderDecl CinderDecl;

typedef enum {
    EX_INT,
    EX_FLOAT,
    EX_CHAR,
    EX_STRING,
    EX_NAME,
    EX_BINARY,
    EX_UNARY,
    EX_ASSIGN,
    EX_CALL,
    EX_CONDITIONAL,
    EX_CAST,
    EX_SIZEOF,
} CinderExprKind;

typedef enum {
    ST_EMPTY,
    ST_EXPR,
    ST_BLOCK,
    ST_RETURN,
    ST_IF,
    ST_WHILE,
    ST_DO,
    ST_FOR,
    ST_BREAK,
    ST_CONTINUE,
    ST_DECL,
} CinderStmtKind;

typedef enum {
    DECL_VAR,
    DECL_FUNCTION,
    DECL_TYPEDEF,
} CinderDeclKind;

struct CinderExpr {
    CinderExprKind kind;
    CinderLoc loc;
    CinderType *type;
    bool is_lvalue;
    union {
        int64_t integer;
        double floating;
        char *string;
        char *name;
        struct { int op; CinderExpr *left; CinderExpr *right; } binary;
        struct { int op; CinderExpr *value; } unary;
        struct { CinderExpr *target; CinderExpr *value; int op; } assign;
        struct { CinderExpr *callee; CINDER_VEC_TYPE(CinderExpr *) args; } call;
        struct { CinderExpr *condition; CinderExpr *yes; CinderExpr *no; } conditional;
        struct { CinderType *cast_type; CinderExpr *value; } cast;
    } as;
};

struct CinderStmt {
    CinderStmtKind kind;
    CinderLoc loc;
    union {
        CinderExpr *expr;
        struct { CINDER_VEC_TYPE(CinderStmt *) items; } block;
        struct { CinderExpr *value; } ret;
        struct { CinderExpr *condition; CinderStmt *then_branch; CinderStmt *else_branch; } if_stmt;
        struct { CinderExpr *condition; CinderStmt *body; } loop;
        struct { CinderStmt *init; CinderExpr *condition; CinderExpr *step; CinderStmt *body; } for_stmt;
        CinderDecl *decl;
    } as;
};

struct CinderDecl {
    CinderDeclKind kind;
    CinderLoc loc;
    char *name;
    CinderType *type;
    bool is_static;
    bool is_extern;
    bool is_definition;
    CinderExpr *initializer;
    CINDER_VEC_TYPE(CinderDecl *) params;
    CinderStmt *body;
};

typedef struct {
    CINDER_VEC_TYPE(CinderDecl *) declarations;
    CinderArena arena;
    CinderTypeContext *types;
    CinderTokenStream *tokens;
    CinderDiagnostics *diags;
    size_t cursor;
} CinderAst;

void cinder_ast_init(CinderAst *ast, CinderTypeContext *types, CinderTokenStream *tokens, CinderDiagnostics *diags);
void cinder_ast_destroy(CinderAst *ast);
int cinder_parse(CinderAst *ast);
void cinder_dump_ast(const CinderAst *ast, CinderSourceManager *sources, FILE *out);

/* ---------- semantic analysis ---------- */
typedef struct {
    char *name;
    CinderType *type;
    CinderDecl *decl;
    bool is_function;
} CinderSymbol;

typedef struct CinderScope CinderScope;
struct CinderScope {
    CINDER_VEC_TYPE(CinderSymbol) symbols;
    CinderScope *parent;
};

typedef struct {
    CinderAst *ast;
    CinderTypeContext *types;
    CinderDiagnostics *diags;
    CinderScope globals;
} CinderSema;

void cinder_sema_init(CinderSema *sema, CinderAst *ast, CinderTypeContext *types, CinderDiagnostics *diags);
void cinder_sema_destroy(CinderSema *sema);
int cinder_sema_run(CinderSema *sema);
CinderSymbol *cinder_scope_lookup(CinderScope *scope, const char *name);

/* ---------- typed SSA-like IR ---------- */
typedef uint32_t CinderValueId;
typedef uint32_t CinderBlockId;
#define CINDER_INVALID_VALUE UINT32_MAX
#define CINDER_INVALID_BLOCK UINT32_MAX

typedef enum {
    IR_NOP,
    IR_CONST,
    IR_FCONST,
    IR_GLOBAL_LOAD,
    IR_GLOBAL_STORE,
    IR_LOCAL_LOAD,
    IR_LOCAL_STORE,
    IR_ARG,
    IR_COPY,
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_FADD,
    IR_FSUB,
    IR_FMUL,
    IR_FDIV,
    IR_FNEG,
    IR_FCMP_EQ,
    IR_FCMP_NE,
    IR_FCMP_LT,
    IR_FCMP_LE,
    IR_FCMP_GT,
    IR_FCMP_GE,
    IR_DIV_S,
    IR_DIV_U,
    IR_MOD_S,
    IR_MOD_U,
    IR_NEG,
    IR_BIT_NOT,
    IR_BIT_AND,
    IR_BIT_OR,
    IR_BIT_XOR,
    IR_SHL,
    IR_SHR_S,
    IR_SHR_U,
    IR_CMP_EQ,
    IR_CMP_NE,
    IR_CMP_LT_S,
    IR_CMP_LE_S,
    IR_CMP_GT_S,
    IR_CMP_GE_S,
    IR_CMP_LT_U,
    IR_CMP_LE_U,
    IR_CMP_GT_U,
    IR_CMP_GE_U,
    IR_CALL,
    IR_PHI,
} CinderIROp;

typedef enum {
    TERM_UNREACHABLE,
    TERM_RETURN,
    TERM_JUMP,
    TERM_BRANCH,
} CinderTermKind;

typedef struct {
    CinderIROp op;
    CinderValueId dst;
    CinderValueId left;
    CinderValueId right;
    int64_t integer;
    double floating;
    int slot;
    int operator_code;
    char *callee;
    CINDER_VEC_TYPE(CinderValueId) args;
    CINDER_VEC_TYPE(CinderBlockId) phi_blocks;
    CinderLoc loc;
} CinderIRInst;

typedef struct {
    CinderTermKind kind;
    CinderValueId value;
    CinderBlockId target;
    CinderBlockId yes;
    CinderBlockId no;
    CinderValueId condition;
    CinderLoc loc;
} CinderTerminator;

typedef struct {
    CinderBlockId id;
    char *name;
    CINDER_VEC_TYPE(CinderIRInst) instructions;
    CinderTerminator terminator;
    CINDER_VEC_TYPE(CinderBlockId) predecessors;
    CINDER_VEC_TYPE(CinderBlockId) successors;
} CinderIRBlock;

typedef struct {
    char *name;
    CinderType *type;
    CINDER_VEC_TYPE(CinderDecl *) params;
    CINDER_VEC_TYPE(CinderIRBlock) blocks;
    size_t value_count;
    size_t local_count;
    CinderAst *ast;
} CinderIRFunction;

typedef struct {
    char *name;
    CinderType *type;
    int64_t integer;
    char *bytes;
    size_t byte_count;
    bool read_only;
    bool is_extern;
    bool has_initializer;
    CinderLoc loc;
} CinderIRGlobal;

typedef struct {
    CINDER_VEC_TYPE(CinderIRFunction) functions;
    CINDER_VEC_TYPE(CinderIRGlobal) globals;
    CinderArena arena;
    CinderTypeContext *types;
} CinderIRModule;

void cinder_ir_init(CinderIRModule *module, CinderTypeContext *types);
void cinder_ir_destroy(CinderIRModule *module);
int cinder_lower_ir(CinderIRModule *module, CinderAst *ast, CinderDiagnostics *diags);
int cinder_verify_ir(const CinderIRModule *module, CinderDiagnostics *diags);
void cinder_dump_ir(const CinderIRModule *module, FILE *out);

typedef struct {
    size_t block_count;
    CinderBlockId *rpo;
    size_t rpo_count;
    CinderBlockId *idom;
    bool *reachable;
    bool *loop_header;
    size_t *rpo_index;
} CinderCFGAnalysis;

void cinder_cfg_init(CinderCFGAnalysis *analysis);
void cinder_cfg_destroy(CinderCFGAnalysis *analysis);
int cinder_analyze_cfg(const CinderIRFunction *function, CinderCFGAnalysis *analysis, CinderDiagnostics *diags);
void cinder_dump_cfg(const CinderIRFunction *function, const CinderCFGAnalysis *analysis, FILE *out);
int cinder_insert_join_phis(CinderIRFunction *function, CinderDiagnostics *diags);
unsigned cinder_forward_local_memory(CinderIRFunction *function);
unsigned cinder_remove_dead_ir(CinderIRFunction *function);
/* ---------- interpreter and optimization ---------- */
typedef struct {
    bool valid;
    bool floating_result;
    int64_t value;
    double floating;
} CinderInterpResult;
CinderInterpResult cinder_interpret(const CinderIRModule *module, const char *function_name, const int64_t *args, size_t arg_count, unsigned step_limit, CinderDiagnostics *diags);

typedef struct {
    unsigned functions_changed;
    unsigned instructions_changed;
    unsigned constants_folded;
    unsigned blocks_removed;
    unsigned memory_forwarded;
    unsigned dead_instructions_removed;
} CinderOptStats;
int cinder_optimize(CinderIRModule *module, int level, CinderOptStats *stats, CinderDiagnostics *diags);

/* ---------- machine representation and allocation ---------- */
typedef enum { LOC_STACK, LOC_REGISTER } CinderLocationKind;
typedef enum { REG_RAX, REG_RCX, REG_RDX, REG_RSI, REG_RDI, REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15, REG_RBP, REG_RSP, REG_NONE } CinderRegister;
typedef struct {
    CinderLocationKind kind;
    CinderRegister reg;
    int stack_offset;
} CinderLocation;

typedef struct {
    CinderValueId value;
    size_t start;
    size_t end;
    CinderLocation location;
} CinderInterval;

typedef struct {
    CinderIRFunction *ir;
    CINDER_VEC_TYPE(CinderInterval) intervals;
    size_t frame_size;
    unsigned spills;
} CinderAllocation;

void cinder_alloc_init(CinderAllocation *allocation, CinderIRFunction *function);
void cinder_alloc_destroy(CinderAllocation *allocation);
int cinder_allocate(CinderAllocation *allocation, CinderDiagnostics *diags);
int cinder_verify_allocation(const CinderAllocation *allocation, CinderDiagnostics *diags);
typedef struct {
    CinderValueId source;
    CinderValueId destination;
} CinderParallelCopy;
typedef struct {
    CINDER_VEC_TYPE(CinderParallelCopy) moves;
    unsigned temporary_count;
} CinderParallelCopyPlan;
void cinder_parallel_copy_init(CinderParallelCopyPlan *plan);
void cinder_parallel_copy_destroy(CinderParallelCopyPlan *plan);
int cinder_resolve_parallel_copies(const CinderValueId *sources, const CinderValueId *destinations, size_t count, CinderParallelCopyPlan *plan, CinderDiagnostics *diags);
void cinder_dump_regalloc(const CinderAllocation *allocation, FILE *out);

/* ---------- x86-64 machine output ---------- */
typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} CinderBytes;

typedef struct {
    size_t offset;
    char *symbol;
    int type;
    int64_t addend;
} CinderFixup;

typedef struct {
    char *name;
    unsigned section_kind;
    size_t offset;
    size_t size;
    bool global;
} CinderDataSymbol;

typedef struct {
    CinderBytes text;
    CinderBytes data;
    CinderBytes rodata;
    size_t bss_size;
    CINDER_VEC_TYPE(CinderFixup) fixups;
    CINDER_VEC_TYPE(char *) defined_symbols;
    CINDER_VEC_TYPE(size_t) symbol_offsets;
    CINDER_VEC_TYPE(CinderDataSymbol) data_symbols;
    unsigned literal_counter;
    size_t frame_size;
} CinderMachineObject;

void cinder_bytes_reserve(CinderBytes *bytes, size_t extra);
void cinder_bytes_put8(CinderBytes *bytes, uint8_t value);
void cinder_bytes_put32(CinderBytes *bytes, uint32_t value);
void cinder_bytes_put64(CinderBytes *bytes, uint64_t value);
void cinder_bytes_patch32(CinderBytes *bytes, size_t offset, uint32_t value);
void cinder_bytes_append(CinderBytes *bytes, const unsigned char *data, size_t length);

void cinder_machine_init(CinderMachineObject *object);
void cinder_machine_destroy(CinderMachineObject *object);
int cinder_lower_globals(const CinderIRModule *module, CinderMachineObject *object, CinderDiagnostics *diags);
int cinder_lower_x86(const CinderIRFunction *function, CinderAllocation *allocation, CinderMachineObject *object, bool assembly, FILE *asm_out, CinderDiagnostics *diags);
int cinder_write_elf64(const CinderMachineObject *object, const char *path, CinderDiagnostics *diags);

/* ---------- driver and inspection ---------- */
typedef struct {
    const char *input;
    const char *const *inputs;
    size_t input_count;
    const char *output;
    const char *const *include_dirs;
    size_t include_count;
    const char *const *defines;
    size_t define_count;
    bool preprocess_only;
    bool emit_assembly;
    bool emit_object;
    bool syntax_only;
    bool dump_tokens;
    bool dump_ast;
    bool dump_ir;
    bool dump_mir;
    bool dump_regalloc;
    bool verify_each;
    bool interpret;
    bool debug;
    const char *explorer;
    int optimization;
} CinderOptions;

int cinder_driver_run(const CinderOptions *options);
void cinder_print_help(FILE *out);
void cinder_print_version(FILE *out);
int cinder_write_explorer(const char *directory, const CinderTokenStream *tokens, const CinderAst *ast, const CinderIRModule *module, const CinderMachineObject *machine, CinderDiagnostics *diags);

#endif
