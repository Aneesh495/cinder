#include "cinder.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static CinderIRInst inst(CinderIROp op, CinderValueId dst, CinderValueId left, CinderValueId right, CinderType *type) {
    CinderIRInst result; memset(&result, 0, sizeof(result));
    result.op = op; result.dst = dst; result.left = left; result.right = right;
    result.type = type; result.source_type = type; result.slot = -1;
    return result;
}

static void add(CinderIRFunction *function, size_t block, CinderIRInst value) {
    cinder_vec_push((CinderVec *)&function->blocks.data[block].instructions, &value);
}

static void edge(CinderIRFunction *function, CinderBlockId from, CinderBlockId to) {
    cinder_vec_push((CinderVec *)&function->blocks.data[from].successors, &to);
    cinder_vec_push((CinderVec *)&function->blocks.data[to].predecessors, &from);
}

static void graph(CinderIRModule *module, CinderTypeContext *types, unsigned seed) {
    CinderType *scalar = seed % 3U == 0U ? types->int_type : seed % 3U == 1U ? types->float_type : types->double_type;
    bool fp = cinder_ir_floating(scalar);
    CinderIRFunction function; memset(&function, 0, sizeof(function));
    function.name = cinder_strndup("main", 4U); function.types = types; function.global = true;
    CinderParamVec parameters = {NULL, 0U, 0U};
    function.type = cinder_type_function(types, types->int_type, &parameters);
    function.local_count = 4U; function.value_count = fp ? 19U : 18U;
    for (size_t s = 0U; s < 4U; ++s) {
        CinderType *type = s == 3U ? types->int_type : scalar;
        cinder_vec_push((CinderVec *)&function.local_types, &type);
    }
    for (size_t b = 0U; b < 4U; ++b) {
        CinderIRBlock block; memset(&block, 0, sizeof(block));
        block.id = (CinderBlockId)b; block.name = cinder_strndup("probe", 5U);
        block.terminator.value = CINDER_INVALID_VALUE; block.terminator.condition = CINDER_INVALID_VALUE;
        cinder_vec_push((CinderVec *)&function.blocks, &block);
    }
    for (unsigned v = 0U; v < 6U; ++v) {
        CinderType *type = v < 3U ? scalar : types->int_type;
        CinderIRInst value = inst(v < 3U && fp ? IR_FCONST : IR_CONST, v, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, type);
        value.integer = v < 3U ? (int64_t)(1U + (seed + v) % 3U) : v == 3U ? 0 : v == 4U ? 1 : (int64_t)(1U + seed % 31U);
        value.floating = (double)value.integer; add(&function, 0U, value);
    }
    function.blocks.data[0].terminator.kind = TERM_JUMP; function.blocks.data[0].terminator.target = 1U;
    edge(&function, 0U, 1U); edge(&function, 1U, 2U); edge(&function, 1U, 3U); edge(&function, 2U, 1U); edge(&function, 2U, 3U);
    CinderValueId back[] = {7U, 8U, 6U, 11U};
    for (unsigned s = 0U; s < 4U; ++s) {
        CinderIRInst phi = inst(IR_PHI, s + 6U, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, s == 3U ? types->int_type : scalar);
        phi.slot = (int)s; CinderBlockId entry = 0U, body = 2U;
        cinder_vec_push((CinderVec *)&phi.args, &s); cinder_vec_push((CinderVec *)&phi.args, &back[s]);
        cinder_vec_push((CinderVec *)&phi.phi_blocks, &entry); cinder_vec_push((CinderVec *)&phi.phi_blocks, &body);
        add(&function, 1U, phi);
    }
    add(&function, 1U, inst(IR_CMP_LT_S, 10U, 9U, 5U, types->int_type));
    function.blocks.data[1].terminator.kind = TERM_BRANCH; function.blocks.data[1].terminator.condition = 10U;
    function.blocks.data[1].terminator.yes = 2U; function.blocks.data[1].terminator.no = 3U;
    add(&function, 2U, inst(IR_ADD, 11U, 9U, 4U, types->int_type));
    function.blocks.data[2].terminator.kind = TERM_BRANCH; function.blocks.data[2].terminator.condition = 10U;
    function.blocks.data[2].terminator.yes = 1U; function.blocks.data[2].terminator.no = 3U;
    for (unsigned v = 12U; v < 14U; ++v) {
        CinderIRInst value = inst(fp ? IR_FCONST : IR_CONST, v, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, scalar);
        value.integer = v == 12U ? 100 : 10; value.floating = (double)value.integer; add(&function, 3U, value);
    }
    add(&function, 3U, inst(fp ? IR_FMUL : IR_MUL, 14U, 6U, 12U, scalar));
    add(&function, 3U, inst(fp ? IR_FMUL : IR_MUL, 15U, 7U, 13U, scalar));
    add(&function, 3U, inst(fp ? IR_FADD : IR_ADD, 16U, 14U, 15U, scalar));
    add(&function, 3U, inst(fp ? IR_FADD : IR_ADD, 17U, 16U, 8U, scalar));
    if (fp) { CinderIRInst convert = inst(IR_CONVERT, 18U, 17U, CINDER_INVALID_VALUE, types->int_type); convert.source_type = scalar; add(&function, 3U, convert); }
    function.blocks.data[3].terminator.kind = TERM_RETURN; function.blocks.data[3].terminator.value = fp ? 18U : 17U;
    cinder_vec_push((CinderVec *)&module->functions, &function);
}

static int forced_cycle(CinderAllocation *allocation, CinderDiagnostics *diagnostics) {
    bool fp = cinder_ir_floating(allocation->ir->local_types.data[0]);
    allocation->spill_slots = (unsigned)allocation->ir->value_count; allocation->saved_gpr_mask = fp ? 0U : 7U;
    for (size_t i = 0U; i < allocation->intervals.len; ++i) {
        CinderInterval *interval = &allocation->intervals.data[i];
        if (interval->value >= 6U && interval->value <= 8U) {
            interval->location = (CinderLocation){LOC_REGISTER, (CinderRegister)((fp ? REG_XMM2 : REG_R12) + interval->value - 6U), 0};
        } else interval->location = (CinderLocation){LOC_STACK, REG_NONE, -(int)((allocation->local_bytes / 8U + interval->value + 1U) * 8U)};
    }
    allocation->frame_size = ((allocation->local_bytes / 8U + allocation->spill_slots + (fp ? 0U : 3U) + 14U) * 8U + 15U) & ~(size_t)15U;
    return cinder_verify_allocation(allocation, diagnostics);
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    unsigned seed = (unsigned)strtoul(argv[1], NULL, 10);
    CinderTypeContext types; cinder_types_init(&types);
    CinderIRModule module; cinder_ir_init(&module, &types); graph(&module, &types, seed);
    CinderDiagnostics diagnostics; cinder_diags_init(&diagnostics);
    unsigned rotate = (1U + seed % 31U) % 3U;
    int64_t a = (int64_t)(1U + (seed + rotate) % 3U);
    int64_t b = (int64_t)(1U + (seed + rotate + 1U) % 3U);
    int64_t c = (int64_t)(1U + (seed + rotate + 2U) % 3U);
    int64_t expected = a * 100 + b * 10 + c;
    int result = 1;
    if (cinder_verify_ir(&module, &diagnostics) != 0) goto done;
    CinderInterpResult interpreted = cinder_interpret(&module, "main", NULL, 0U, 10000U, &diagnostics);
    if (!interpreted.valid || interpreted.value != expected) goto done;
    if (cinder_mir_boundary(&module.functions.data[0], &diagnostics) != 0 || module.functions.data[0].blocks.len != 5U || cinder_verify_ir(&module, &diagnostics) != 0) goto done;
    interpreted = cinder_interpret(&module, "main", NULL, 0U, 10000U, &diagnostics);
    if (!interpreted.valid || interpreted.value != expected) goto done;
    CinderAllocation allocation; cinder_alloc_init(&allocation, &module.functions.data[0]);
    if (cinder_allocate(&allocation, &diagnostics) == 0 && forced_cycle(&allocation, &diagnostics) == 0) {
        CinderMachineObject machine; cinder_machine_init(&machine);
        if (cinder_lower_x86(&module.functions.data[0], &allocation, &machine, false, NULL, &diagnostics) == 0 && cinder_write_elf64(&machine, argv[2], &diagnostics) == 0) result = 0;
        cinder_machine_destroy(&machine);
    }
    cinder_alloc_destroy(&allocation);
    if (result == 0) printf("{\"seed\":%u,\"expected\":%" PRId64 ",\"interpreter\":%" PRId64 ",\"critical_edges\":1,\"physical_cycle\":3}\n", seed, expected, interpreted.value);
done:
    if (result != 0) { CinderSourceManager sources; cinder_sources_init(&sources); cinder_diag_print(&diagnostics, &sources, stderr); cinder_sources_destroy(&sources); }
    cinder_diags_destroy(&diagnostics); cinder_ir_destroy(&module); cinder_types_destroy(&types);
    return result;
}
