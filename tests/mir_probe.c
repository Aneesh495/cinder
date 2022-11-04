#include "cinder.h"
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { CinderIROp op; int64_t left, right, expected; bool signed_value; } Specimen;
static const Specimen integers[] = {
    {IR_ADD,30,3,33,false}, {IR_SUB,30,3,27,false}, {IR_MUL,30,3,90,false},
    {IR_DIV_S,-32,3,-10,true}, {IR_MOD_S,-32,3,-2,true}, {IR_DIV_U,30,3,10,false}, {IR_MOD_U,30,3,0,false},
    {IR_BIT_AND,30,3,2,false}, {IR_BIT_OR,30,3,31,false}, {IR_BIT_XOR,30,3,29,false},
    {IR_SHL,30,3,240,false}, {IR_SHR_S,-32,3,-4,true}, {IR_SHR_U,30,3,3,false},
    {IR_CMP_EQ,30,3,0,false}, {IR_CMP_NE,30,3,1,false}, {IR_CMP_LT_S,-32,3,1,true}, {IR_CMP_LE_S,-32,3,1,true},
    {IR_CMP_GT_S,-32,3,0,true}, {IR_CMP_GE_S,-32,3,0,true},
    {IR_CMP_LT_U,30,3,0,false}, {IR_CMP_LE_U,30,3,0,false}, {IR_CMP_GT_U,30,3,1,false}, {IR_CMP_GE_U,30,3,1,false},
};

static CinderIRInst instruction(CinderIROp op, CinderType *type, CinderValueId dst, CinderValueId left, CinderValueId right) {
    CinderIRInst result; memset(&result, 0, sizeof(result)); result.op = op; result.type = type; result.source_type = type;
    result.dst = dst; result.left = left; result.right = right; result.slot = -1; return result;
}

static int64_t graph(CinderIRModule *module, CinderTypeContext *types, unsigned number) {
    CinderIRFunction function; memset(&function, 0, sizeof(function));
    function.name = cinder_strndup("main", 4U); function.types = types; function.global = true; function.value_count = 4U;
    CinderParamVec parameters = {NULL,0U,0U}; function.type = cinder_type_function(types, types->int_type, &parameters);
    CinderIRBlock block; memset(&block, 0, sizeof(block)); block.name = cinder_strndup("entry", 5U);
    block.terminator.kind = TERM_RETURN; block.terminator.value = 3U;
    block.terminator.condition = CINDER_INVALID_VALUE; block.terminator.target = CINDER_INVALID_BLOCK;
    block.terminator.yes = block.terminator.no = CINDER_INVALID_BLOCK;
    CinderType *operand_type; CinderIROp operation; int64_t expected;
    CinderIRInst a, b;
    if (number < CINDER_ARRAY_LEN(integers)) {
        const Specimen *specimen = &integers[number]; operand_type = specimen->signed_value ? types->long_type : types->ulong_type;
        operation = specimen->op; expected = specimen->expected;
        a = instruction(IR_CONST, operand_type, 0U, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE); a.integer = specimen->left;
        b = instruction(IR_CONST, operand_type, 1U, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE); b.integer = specimen->right;
    } else {
        unsigned floating = number - (unsigned)CINDER_ARRAY_LEN(integers);
        operand_type = floating < 4U ? types->float_type : types->double_type;
        a = instruction(IR_FCONST, operand_type, 0U, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE); a.floating = floating >= 14U ? NAN : 1.5;
        b = instruction(IR_FCONST, operand_type, 1U, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE); b.floating = 0.5;
        if (floating < 8U) {
            static const CinderIROp operations[] = {IR_FADD,IR_FSUB,IR_FMUL,IR_FDIV};
            static const int64_t outcomes[] = {2,1,0,3}; operation = operations[floating % 4U]; expected = outcomes[floating % 4U];
        } else {
            static const CinderIROp operations[] = {IR_FCMP_EQ,IR_FCMP_NE,IR_FCMP_LT,IR_FCMP_LE,IR_FCMP_GT,IR_FCMP_GE};
            static const int64_t ordered[] = {0,1,0,0,1,1}, unordered[] = {0,1,0,0,0,0};
            unsigned relation = (floating - 8U) % 6U; operation = operations[relation]; expected = floating >= 14U ? unordered[relation] : ordered[relation];
        }
    }
    bool comparison = (operation >= IR_CMP_EQ && operation <= IR_CMP_GE_U) || (operation >= IR_FCMP_EQ && operation <= IR_FCMP_GE);
    CinderType *result_type = comparison ? types->int_type : operand_type;
    CinderIRInst result = instruction(operation, result_type, 2U, 0U, 1U); result.source_type = operand_type;
    CinderIRInst returned = instruction(IR_CONVERT, types->int_type, 3U, 2U, CINDER_INVALID_VALUE); returned.source_type = result_type;
    cinder_vec_push((CinderVec *)&block.instructions, &a); cinder_vec_push((CinderVec *)&block.instructions, &b);
    cinder_vec_push((CinderVec *)&block.instructions, &result); cinder_vec_push((CinderVec *)&block.instructions, &returned);
    cinder_vec_push((CinderVec *)&function.blocks, &block); cinder_vec_push((CinderVec *)&module->functions, &function); return expected;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    unsigned number = (unsigned)strtoul(argv[1], NULL, 10); if (number >= 43U) return 2;
    CinderTypeContext types; cinder_types_init(&types); CinderDiagnostics diags; cinder_diags_init(&diags);
    CinderIRModule module; cinder_ir_init(&module, &types); int64_t expected = graph(&module, &types, number);
    int failed = cinder_verify_ir(&module, &diags); CinderInterpResult observed = cinder_interpret(&module, "main", NULL, 0U, 1000U, &diags);
    if (!observed.valid || observed.value != expected) failed = 1;
    CinderAllocation allocation; cinder_alloc_init(&allocation, &module.functions.data[0]);
    if (cinder_allocate(&allocation, &diags) != 0 || cinder_verify_allocation(&allocation, &diags) != 0) failed = 1;
    unsigned rejected = 0U;
    if (!failed) for (unsigned mutation = 0U; mutation < 7U; ++mutation) {
        CinderMIRInst *selected = &allocation.machine.blocks.data[0].instructions.data[2]; CinderMIRInst original = *selected;
        CinderMIRValue value = allocation.machine.values[2]; CinderTerminator terminator = allocation.machine.blocks.data[0].terminator;
        if (mutation == 0U) selected->encoding_size = 99U;
        else if (mutation == 1U) selected->clobbers ^= UINT64_C(1);
        else if (mutation == 2U) selected->operands.left = 1U;
        else if (mutation == 3U) selected->operands.floating_result = !selected->operands.floating_result;
        else if (mutation == 4U) allocation.machine.values[2].bank = MIR_BANK_NONE;
        else if (mutation == 5U) allocation.machine.blocks.data[0].terminator.value = 2U;
        else selected->encoding[0] ^= 1U;
        CinderDiagnostics invalid; cinder_diags_init(&invalid);
        if (cinder_verify_selected_mir(&allocation.machine, &invalid) == 0 || invalid.errors == 0U) failed = 1; else ++rejected;
        cinder_diags_destroy(&invalid); *selected = original; allocation.machine.values[2] = value; allocation.machine.blocks.data[0].terminator = terminator;
    }
    CinderMachineObject object; cinder_machine_init(&object);
    if (!failed && (cinder_lower_x86(&module.functions.data[0], &allocation, &object, false, NULL, &diags) != 0 || cinder_write_elf64(&object, argv[2], &diags) != 0)) failed = 1;
    if (!failed) printf("{\"case\":%u,\"expected\":%" PRId64 ",\"interpreter\":%" PRId64 ",\"rejected\":%u}\n", number, expected, observed.value, rejected);
    cinder_machine_destroy(&object); cinder_alloc_destroy(&allocation); cinder_ir_destroy(&module); cinder_types_destroy(&types); cinder_diags_destroy(&diags);
    return failed;
}
