#include "cinder.h"

#include <stdlib.h>
#include <string.h>

static const char *register_name(CinderRegister reg) {
    static const char *names[] = {"rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15","rbp","rsp","none"};
    return reg < CINDER_ARRAY_LEN(names) ? names[reg] : "none";
}

void cinder_alloc_init(CinderAllocation *allocation, CinderIRFunction *function) {
    allocation->ir = function; allocation->intervals.data = NULL; allocation->intervals.len = 0U; allocation->intervals.cap = 0U; allocation->frame_size = 0U; allocation->spills = 0U;
}

void cinder_alloc_destroy(CinderAllocation *allocation) { free(allocation->intervals.data); allocation->intervals.data = NULL; allocation->intervals.len = 0U; allocation->intervals.cap = 0U; }

static bool value_is_float(const CinderIRFunction *function, CinderValueId value) {
    for (size_t b = 0U; b < function->blocks.len; ++b) for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) { const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i]; if (inst->dst == value) return inst->op == IR_FCONST || inst->op == IR_FARG || inst->op == IR_FADD || inst->op == IR_FSUB || inst->op == IR_FMUL || inst->op == IR_FDIV || inst->op == IR_FNEG || (inst->op == IR_CALL && inst->floating_result); }
    return false;
}

int cinder_allocate(CinderAllocation *allocation, CinderDiagnostics *diags) {
    (void)diags;
    size_t count = allocation->ir->value_count;
    CinderInterval *intervals = cinder_alloc((count == 0U ? 1U : count) * sizeof(*intervals));
    bool *seen = cinder_alloc((count == 0U ? 1U : count) * sizeof(*seen));
    for (size_t i = 0U; i < count; ++i) { intervals[i].value = (CinderValueId)i; intervals[i].start = SIZE_MAX; intervals[i].end = 0U; intervals[i].location.kind = LOC_STACK; intervals[i].location.reg = REG_NONE; intervals[i].location.stack_offset = 0; seen[i] = false; }
    size_t position = 0U;
    CINDER_VEC_TYPE(size_t) call_positions = {NULL, 0U, 0U};
    for (size_t b = 0U; b < allocation->ir->blocks.len; ++b) {
        CinderIRBlock *block = &allocation->ir->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i, ++position) {
            CinderIRInst *inst = &block->instructions.data[i];
            if (inst->op == IR_CALL) cinder_vec_push((CinderVec *)&call_positions, &position);
            CinderValueId uses[2] = {inst->left, inst->right};
            for (size_t u = 0U; u < 2U; ++u) if (uses[u] != CINDER_INVALID_VALUE && uses[u] < count) { if (!seen[uses[u]]) { intervals[uses[u]].start = position; seen[uses[u]] = true; } intervals[uses[u]].end = position; }
            for (size_t a = 0U; a < inst->args.len; ++a) if (inst->args.data[a] < count) { if (!seen[inst->args.data[a]]) { intervals[inst->args.data[a]].start = position; seen[inst->args.data[a]] = true; } intervals[inst->args.data[a]].end = position; }
            if (inst->dst != CINDER_INVALID_VALUE && inst->dst < count) { intervals[inst->dst].start = position; intervals[inst->dst].end = position; seen[inst->dst] = true; }
        }
        if (block->terminator.condition != CINDER_INVALID_VALUE && block->terminator.condition < count) intervals[block->terminator.condition].end = position;
        if (block->terminator.value != CINDER_INVALID_VALUE && block->terminator.value < count) intervals[block->terminator.value].end = position;
    }
    int registers[] = {REG_R8, REG_R9, REG_R10, REG_R11};
    size_t spill_index = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (!seen[i]) continue;
        CinderInterval interval = intervals[i];
        bool live_across_call = false;
        for (size_t c = 0U; c < call_positions.len; ++c) if (interval.start < call_positions.data[c] && interval.end > call_positions.data[c]) live_across_call = true;
        bool float_value = value_is_float(allocation->ir, interval.value);
        if (!float_value && !live_across_call && i < CINDER_ARRAY_LEN(registers)) { interval.location.kind = LOC_REGISTER; interval.location.reg = (CinderRegister)registers[i]; interval.location.stack_offset = 0; }
        else { interval.location.kind = LOC_STACK; interval.location.reg = REG_NONE; interval.location.stack_offset = -((int)(allocation->ir->local_count + spill_index + 1U) * 8); allocation->spills++; spill_index++; }
        cinder_vec_push((CinderVec *)&allocation->intervals, &interval);
    }
    size_t slots = allocation->ir->local_count + allocation->spills;
    allocation->frame_size = (slots * 8U + 15U) & ~((size_t)15U);
    free(call_positions.data);
    free(seen); free(intervals);
    return 0;
}

static void dump_phi_copy_plans(const CinderAllocation *allocation, FILE *out);

void cinder_dump_regalloc(const CinderAllocation *allocation, FILE *out) {
    fprintf(out, "allocation function=%s frame=%zu spills=%u\n", allocation->ir->name, allocation->frame_size, allocation->spills);
    for (size_t i = 0U; i < allocation->intervals.len; ++i) {
        const CinderInterval *interval = &allocation->intervals.data[i];
        if (interval->location.kind == LOC_REGISTER) fprintf(out, "  %%v%u [%zu,%zu] -> %s\n", interval->value, interval->start, interval->end, register_name(interval->location.reg));
        else fprintf(out, "  %%v%u [%zu,%zu] -> stack %d(%%rbp)\n", interval->value, interval->start, interval->end, interval->location.stack_offset);
    }
    dump_phi_copy_plans(allocation, out);
}

int cinder_verify_allocation(const CinderAllocation *allocation, CinderDiagnostics *diags) {
    if ((allocation->frame_size & 15U) != 0U) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation frame for '%s' is not 16-byte aligned", allocation->ir->name);
        return 1;
    }
    for (size_t i = 0U; i < allocation->intervals.len; ++i) {
        const CinderInterval *left = &allocation->intervals.data[i];
        if (left->start > left->end) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation interval for value %u is inverted", left->value); continue; }
        for (size_t j = i + 1U; j < allocation->intervals.len; ++j) {
            const CinderInterval *right = &allocation->intervals.data[j];
            bool overlap = left->start <= right->end && right->start <= left->end;
            if (overlap && left->location.kind == LOC_REGISTER && right->location.kind == LOC_REGISTER && left->location.reg == right->location.reg) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "overlapping values %u and %u share register %u", left->value, right->value, left->location.reg);
            if (overlap && left->location.kind == LOC_STACK && right->location.kind == LOC_STACK && left->location.stack_offset == right->location.stack_offset) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "overlapping values %u and %u share stack slot", left->value, right->value);
        }
    }
    for (size_t b = 0U; b < allocation->ir->blocks.len; ++b) {
        CinderIRBlock *block = &allocation->ir->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst *inst = &block->instructions.data[i];
            if (inst->op != IR_PHI || inst->args.len == 0U) continue;
            CinderValueId *destinations = cinder_alloc(inst->args.len * sizeof(*destinations));
            for (size_t p = 0U; p < inst->args.len; ++p) destinations[p] = inst->dst;
            CinderParallelCopyPlan plan; cinder_parallel_copy_init(&plan);
            (void)cinder_resolve_parallel_copies(inst->args.data, destinations, inst->args.len, &plan, diags);
            cinder_parallel_copy_destroy(&plan); free(destinations);
        }
    }
    return diags->errors == 0U ? 0 : 1;
}


void cinder_parallel_copy_init(CinderParallelCopyPlan *plan) { plan->moves.data = NULL; plan->moves.len = 0U; plan->moves.cap = 0U; plan->temporary_count = 0U; }

void cinder_parallel_copy_destroy(CinderParallelCopyPlan *plan) { free(plan->moves.data); plan->moves.data = NULL; plan->moves.len = 0U; plan->moves.cap = 0U; plan->temporary_count = 0U; }

int cinder_resolve_parallel_copies(const CinderValueId *sources, const CinderValueId *destinations, size_t count, CinderParallelCopyPlan *plan, CinderDiagnostics *diags) {
    CINDER_VEC_TYPE(CinderParallelCopy) pending = {NULL, 0U, 0U};
    for (size_t i = 0U; i < count; ++i) if (sources[i] != destinations[i]) { CinderParallelCopy move = {sources[i], destinations[i]}; cinder_vec_push((CinderVec *)&pending, &move); }
    while (pending.len > 0U) {
        size_t ready = SIZE_MAX;
        for (size_t i = 0U; i < pending.len; ++i) {
            bool destination_is_source = false;
            for (size_t j = 0U; j < pending.len; ++j) if (i != j && pending.data[i].destination == pending.data[j].source) destination_is_source = true;
            if (!destination_is_source) { ready = i; break; }
        }
        if (ready != SIZE_MAX) {
            cinder_vec_push((CinderVec *)&plan->moves, &pending.data[ready]);
            memmove(&pending.data[ready], &pending.data[ready + 1U], (pending.len - ready - 1U) * sizeof(pending.data[0]));
            pending.len--;
            continue;
        }
        CinderParallelCopy cycle = pending.data[0];
        CinderParallelCopy temporary = {cycle.source, CINDER_INVALID_VALUE};
        cinder_vec_push((CinderVec *)&plan->moves, &temporary);
        pending.data[0].source = CINDER_INVALID_VALUE;
        plan->temporary_count++;
    }
    free(pending.data);
    if (plan->temporary_count > count && count != 0U) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "parallel-copy resolver exceeded one temporary per cycle");
    return diags->errors == 0U ? 0 : 1;
}


static void dump_phi_copy_plans(const CinderAllocation *allocation, FILE *out) {
    for (size_t b = 0U; b < allocation->ir->blocks.len; ++b) {
        CinderIRBlock *block = &allocation->ir->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            CinderIRInst *inst = &block->instructions.data[i];
            if (inst->op != IR_PHI || inst->args.len == 0U) continue;
            CinderValueId *destinations = cinder_alloc(inst->args.len * sizeof(*destinations));
            for (size_t p = 0U; p < inst->args.len; ++p) destinations[p] = inst->dst;
            CinderParallelCopyPlan plan; cinder_parallel_copy_init(&plan);
            CinderDiagnostics diagnostics; cinder_diags_init(&diagnostics);
            (void)cinder_resolve_parallel_copies(inst->args.data, destinations, inst->args.len, &plan, &diagnostics);
            fprintf(out, "  parallel-copy phi=%%v%u moves=%zu temporaries=%u\n", inst->dst, plan.moves.len, plan.temporary_count);
            cinder_diags_destroy(&diagnostics); cinder_parallel_copy_destroy(&plan); free(destinations);
        }
    }
}
