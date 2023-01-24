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
    return cinder_plan_frame(allocation, diagnostics) || cinder_plan_edge_copies(allocation, diagnostics) || cinder_verify_allocation(allocation, diagnostics);
}

static void single_predecessor(CinderIRModule *module, CinderTypeContext *types, unsigned seed) {
    CinderIRFunction function; memset(&function, 0, sizeof(function));
    function.name = cinder_strndup("main", 4U); function.types = types; function.global = true;
    bool same = (seed & 2U) != 0U; function.value_count = same ? 4U : 5U;
    function.local_count = same ? 1U : 2U;
    for (size_t s = 0U; s < function.local_count; ++s) cinder_vec_push((CinderVec *)&function.local_types, &types->int_type);
    CinderParamVec parameters = {NULL,0U,0U}; function.type = cinder_type_function(types, types->int_type, &parameters);
    for (unsigned b = 0U; b < (same ? 2U : 3U); ++b) {
        CinderIRBlock block; memset(&block, 0, sizeof(block)); block.id = b; block.name = cinder_strndup("single", 6U);
        block.terminator.value = block.terminator.condition = CINDER_INVALID_VALUE;
        cinder_vec_push((CinderVec *)&function.blocks, &block);
    }
    for (unsigned v = 0U; v < 3U; ++v) {
        CinderIRInst value = inst(IR_CONST, v, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, types->int_type);
        value.integer = v == 0U ? 7 : v == 1U ? 9 : (int64_t)((seed & 1U) == 0U); add(&function, 0U, value);
    }
    function.blocks.data[0].terminator.kind = TERM_BRANCH; function.blocks.data[0].terminator.condition = 2U;
    function.blocks.data[0].terminator.yes = 1U; function.blocks.data[0].terminator.no = same ? 1U : 2U;
    for (unsigned b = 1U; b < function.blocks.len; ++b) {
        edge(&function, 0U, b); CinderIRInst phi = inst(IR_PHI, b + 2U, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, types->int_type);
        phi.slot = (int)b - 1;
        CinderValueId source = b - 1U; CinderBlockId predecessor = 0U;
        cinder_vec_push((CinderVec *)&phi.args, &source); cinder_vec_push((CinderVec *)&phi.phi_blocks, &predecessor); add(&function, b, phi);
        function.blocks.data[b].terminator.kind = TERM_RETURN; function.blocks.data[b].terminator.value = phi.dst;
    }
    cinder_vec_push((CinderVec *)&module->functions, &function);
}

static int forced_stack(CinderAllocation *allocation, CinderDiagnostics *diagnostics) {
    allocation->spill_slots = allocation->ir->value_count; allocation->saved_gpr_mask = 0U;
    for (size_t i = 0U; i < allocation->intervals.len; ++i) {
        CinderInterval *interval = &allocation->intervals.data[i];
        interval->location = (CinderLocation){LOC_STACK, REG_NONE, -(int)(allocation->local_bytes + (interval->value + 1U) * 8U)};
    }
    allocation->frame_size = (allocation->local_bytes + (allocation->spill_slots + 14U) * 8U + 15U) & ~(size_t)15U;
    return cinder_plan_frame(allocation, diagnostics) || cinder_plan_edge_copies(allocation, diagnostics) || cinder_verify_allocation(allocation, diagnostics);
}

static unsigned corrupt_copies(CinderIRFunction *function, CinderAllocation *allocation) {
    bool cyclic = false;
    for (size_t b = 0U; b < allocation->machine.blocks.len; ++b) if (allocation->machine.blocks.data[b].copy_temporaries != 0U) cyclic = true;
    for (size_t b = 0U; b < allocation->machine.blocks.len; ++b) {
        CinderMIRBlock *block = &allocation->machine.blocks.data[b]; if (block->transfers.len == 0U) continue;
        if (cyclic && block->copy_temporaries == 0U) continue;
        size_t first = 0U;
        if (cyclic) while (block->transfers.data[first].destination != CINDER_INVALID_VALUE) ++first;
        CinderMIREdgeMove original = block->transfers.data[first]; size_t length = block->transfers.len, inputs = block->copy_inputs;
        CinderMIREdgeMove second = block->transfers.data[first + 1U < length ? first + 1U : first];
        CinderValueId alternative = (CinderValueId)function->value_count;
        for (size_t i = 0U; i < length; ++i) if (block->transfers.data[i].source != CINDER_INVALID_VALUE && block->transfers.data[i].source != original.source && block->transfers.data[i].bank == original.bank) alternative = block->transfers.data[i].source;
        unsigned temporaries = block->copy_temporaries, rejected = 0U;
        for (unsigned change = 0U; change < 9U; ++change) {
            if (change == 0U) block->transfers.data[first].source = (CinderValueId)function->value_count;
            else if (change == 1U) block->transfers.data[first].destination = (CinderValueId)function->value_count;
            else if (change == 2U) block->transfers.data[first].bank = MIR_BANK_NONE;
            else if (change == 3U) ++block->copy_inputs;
            else if (change == 4U) ++block->copy_temporaries;
            else if (change == 5U) block->transfers.len = 0U;
            else if (change == 6U) block->transfers.data[first].source = alternative;
            else if (change == 7U) --block->transfers.len;
            else if (first + 1U < length) { block->transfers.data[first] = second; block->transfers.data[first + 1U] = original; }
            else block->transfers.data[first].source = CINDER_INVALID_VALUE;
            CinderDiagnostics diagnostics; cinder_diags_init(&diagnostics);
            bool invalid = cinder_verify_edge_copies(allocation, &diagnostics) != 0 && diagnostics.errors != 0U;
            cinder_diags_destroy(&diagnostics); cinder_diags_init(&diagnostics);
            CinderMachineObject object; cinder_machine_init(&object);
            invalid = invalid && cinder_lower_x86(function, allocation, &object, false, NULL, &diagnostics) != 0 && object.text.len == 0U && object.defined_symbols.len == 0U;
            cinder_machine_destroy(&object); cinder_diags_destroy(&diagnostics); if (invalid) ++rejected; else fprintf(stderr, "copy mutation %u unexpectedly accepted at block %zu move %zu\n", change, b, first);
            block->transfers.data[first] = original; block->transfers.len = length; block->copy_inputs = inputs; block->copy_temporaries = temporaries;
            if (first + 1U < length) block->transfers.data[first + 1U] = second;
        }
        return rejected;
    }
    return 0U;
}

int main(int argc, char **argv) {
    if (argc != 3 && (argc != 4 || strcmp(argv[3], "single") != 0)) return 2;
    bool single = argc == 4;
    unsigned seed = (unsigned)strtoul(argv[1], NULL, 10);
    CinderTypeContext types; cinder_types_init(&types);
    CinderIRModule module; cinder_ir_init(&module, &types);
    if (single) single_predecessor(&module, &types, seed); else graph(&module, &types, seed);
    CinderDiagnostics diagnostics; cinder_diags_init(&diagnostics);
    unsigned rotate = (1U + seed % 31U) % 3U;
    int64_t a = (int64_t)(1U + (seed + rotate) % 3U);
    int64_t b = (int64_t)(1U + (seed + rotate + 1U) % 3U);
    int64_t c = (int64_t)(1U + (seed + rotate + 2U) % 3U);
    int64_t expected = a * 100 + b * 10 + c;
    if (single) expected = (seed & 2U) != 0U || (seed & 1U) == 0U ? 7 : 9;
    int result = 1;
    if (cinder_verify_ir(&module, &diagnostics) != 0) goto done;
    CinderInterpResult interpreted = cinder_interpret(&module, "main", NULL, 0U, 10000U, &diagnostics);
    if (!interpreted.valid || interpreted.value != expected) goto done;
    size_t blocks = single && (seed & 2U) != 0U ? 3U : 5U;
    if (cinder_mir_boundary(&module.functions.data[0], &diagnostics) != 0 || module.functions.data[0].blocks.len != blocks || cinder_verify_ir(&module, &diagnostics) != 0) goto done;
    interpreted = cinder_interpret(&module, "main", NULL, 0U, 10000U, &diagnostics);
    if (!interpreted.valid || interpreted.value != expected) goto done;
    CinderAllocation allocation; cinder_alloc_init(&allocation, &module.functions.data[0]);
    unsigned rejected = 0U;
    if (cinder_allocate(&allocation, &diagnostics) == 0 && (single ? forced_stack(&allocation, &diagnostics) : forced_cycle(&allocation, &diagnostics)) == 0 && (rejected = corrupt_copies(&module.functions.data[0], &allocation)) == 9U) {
        CinderMachineObject machine; cinder_machine_init(&machine);
        if (cinder_lower_x86(&module.functions.data[0], &allocation, &machine, false, NULL, &diagnostics) == 0 && cinder_write_elf64(&machine, argv[2], &diagnostics) == 0) result = 0;
        cinder_machine_destroy(&machine);
    }
    cinder_alloc_destroy(&allocation);
    if (result == 0) printf("{\"seed\":%u,\"expected\":%" PRId64 ",\"interpreter\":%" PRId64 ",\"split_edges\":%u,\"physical_cycle\":%u,\"rejected_copies\":%u,\"single_predecessor\":%s}\n", seed, expected, interpreted.value, single && (seed & 2U) == 0U ? 2U : 1U, single ? 0U : 3U, rejected, single ? "true" : "false");
done:
    if (result != 0) { CinderSourceManager sources; cinder_sources_init(&sources); cinder_diag_print(&diagnostics, &sources, stderr); cinder_sources_destroy(&sources); }
    cinder_diags_destroy(&diagnostics); cinder_ir_destroy(&module); cinder_types_destroy(&types);
    return result;
}
