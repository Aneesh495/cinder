#include "regalloc_private.h"

#include <stdlib.h>
#include <string.h>

static const char *register_name(CinderRegister reg) {
    static const char *names[] = {"rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15","rbp","rsp","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","none"};
    return reg < CINDER_ARRAY_LEN(names) ? names[reg] : "none";
}

void cinder_alloc_init(CinderAllocation *allocation, CinderIRFunction *function) {
    allocation->ir = function; allocation->intervals.data = NULL; allocation->intervals.len = 0U; allocation->intervals.cap = 0U; allocation->frame_size = 0U; allocation->spills = 0U; allocation->spill_slots = 0U; allocation->saved_gpr_mask = 0U;
    allocation->local_offsets.data = NULL; allocation->local_offsets.len = 0U; allocation->local_offsets.cap = 0U; allocation->local_bytes = 0U;
}

void cinder_alloc_destroy(CinderAllocation *allocation) { free(allocation->intervals.data); allocation->intervals.data = NULL; allocation->intervals.len = 0U; allocation->intervals.cap = 0U; free(allocation->local_offsets.data); allocation->local_offsets.data = NULL; allocation->local_offsets.len = 0U; allocation->local_offsets.cap = 0U; }

static void touch(CinderInterval *intervals, size_t count, CinderValueId value, size_t position) {
    if ((size_t)value >= count) return;
    if (position < intervals[value].start) intervals[value].start = position;
    if (position > intervals[value].end) intervals[value].end = position;
}

static int interval_order(const void *left, const void *right) {
    const CinderInterval *a = left; const CinderInterval *b = right;
    if (a->start != b->start) return a->start < b->start ? -1 : 1;
    return a->value < b->value ? -1 : a->value != b->value;
}

static bool floating_value(const CinderIRFunction *function, CinderValueId value) {
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i) {
            const CinderIRInst *inst = &function->blocks.data[b].instructions.data[i];
            if (inst->dst == value) return inst->type != NULL && (inst->type->kind == TYPE_FLOAT || inst->type->kind == TYPE_DOUBLE);
        }
    return false;
}

int cinder_allocate(CinderAllocation *allocation, CinderDiagnostics *diags) {
    const CinderIRFunction *function = allocation->ir;
    if (cinder_layout_stack(allocation, diags) != 0) return 1;
    CinderLiveness live;
    if (cinder_liveness_build(function, &live, diags) != 0) return 1;
    size_t count = function->value_count;
    CinderInterval *intervals = cinder_alloc((count == 0U ? 1U : count) * sizeof(*intervals));
    CINDER_VEC_TYPE(size_t) calls = {NULL, 0U, 0U};
    for (size_t v = 0U; v < count; ++v) {
        intervals[v].value = (CinderValueId)v; intervals[v].start = SIZE_MAX; intervals[v].end = 0U;
        intervals[v].location = (CinderLocation){LOC_STACK, REG_NONE, 0};
    }
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        const CinderIRBlock *block = &function->blocks.data[b];
        for (size_t v = 0U; v < count; ++v) {
            if (cinder_live_has(live.in + b * live.words, (CinderValueId)v)) touch(intervals, count, (CinderValueId)v, live.begin[b]);
            if (cinder_live_has(live.out + b * live.words, (CinderValueId)v)) touch(intervals, count, (CinderValueId)v, live.end[b]);
        }
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            const CinderIRInst *inst = &block->instructions.data[i];
            size_t position = live.begin[b] + i;
            touch(intervals, count, inst->dst, position);
            if (inst->op == IR_CALL) cinder_vec_push((CinderVec *)&calls, &position);
            if (inst->op == IR_PHI) {
                for (size_t p = 0U; p < inst->args.len && p < inst->phi_blocks.len; ++p)
                    if (inst->phi_blocks.data[p] < live.blocks) touch(intervals, count, inst->args.data[p], live.end[inst->phi_blocks.data[p]]);
            } else {
                touch(intervals, count, inst->left, position); touch(intervals, count, inst->right, position);
                for (size_t a = 0U; a < inst->args.len; ++a) touch(intervals, count, inst->args.data[a], position);
            }
        }
        touch(intervals, count, block->terminator.value, live.end[b]);
        touch(intervals, count, block->terminator.condition, live.end[b]);
    }
    for (size_t v = 0U; v < count; ++v)
        if (intervals[v].start != SIZE_MAX) cinder_vec_push((CinderVec *)&allocation->intervals, &intervals[v]);
    qsort(allocation->intervals.data, allocation->intervals.len, sizeof(CinderInterval), interval_order);
    size_t active[10]; size_t active_count = 0U;
    for (size_t i = 0U; i < allocation->intervals.len; ++i) {
        CinderInterval *current = &allocation->intervals.data[i];
        for (size_t a = 0U; a < active_count;) {
            if (allocation->intervals.data[active[a]].end < current->start) active[a] = active[--active_count];
            else ++a;
        }
        bool floating = floating_value(function, current->value);
        bool crossing_call = false;
        for (size_t c = 0U; c < calls.len; ++c) if (current->start < calls.data[c] && current->end > calls.data[c]) crossing_call = true;
        if (floating && crossing_call) continue;
        CinderRegister first = floating ? REG_XMM2 : REG_R12;
        CinderRegister last = floating ? REG_XMM7 : REG_R15;
        CinderRegister available = REG_NONE;
        for (CinderRegister reg = first; reg <= last; reg = (CinderRegister)((unsigned)reg + 1U)) {
            bool occupied = false;
            for (size_t a = 0U; a < active_count; ++a) if (allocation->intervals.data[active[a]].location.reg == reg) occupied = true;
            if (!occupied) { available = reg; break; }
        }
        if (available == REG_NONE) {
            size_t victim = SIZE_MAX;
            for (size_t a = 0U; a < active_count; ++a) {
                const CinderInterval *candidate = &allocation->intervals.data[active[a]];
                if (candidate->location.reg >= first && candidate->location.reg <= last && candidate->end > current->end && (victim == SIZE_MAX || candidate->end > allocation->intervals.data[active[victim]].end)) victim = a;
            }
            if (victim != SIZE_MAX) {
                CinderInterval *spilled = &allocation->intervals.data[active[victim]];
                available = spilled->location.reg;
                spilled->location = (CinderLocation){LOC_STACK, REG_NONE, 0};
                active[victim] = active[--active_count];
            }
        }
        if (available != REG_NONE) {
            current->location = (CinderLocation){LOC_REGISTER, available, 0};
            active[active_count++] = i;
        }
    }
    size_t *slot_end = cinder_alloc((count == 0U ? 1U : count) * sizeof(*slot_end));
    allocation->spill_slots = 0U; allocation->spills = 0U; allocation->saved_gpr_mask = 0U;
    for (size_t i = 0U; i < allocation->intervals.len; ++i) {
        CinderInterval *interval = &allocation->intervals.data[i];
        if (interval->location.kind == LOC_REGISTER) {
            if (interval->location.reg >= REG_R12 && interval->location.reg <= REG_R15) allocation->saved_gpr_mask |= 1U << (unsigned)(interval->location.reg - REG_R12);
            continue;
        }
        size_t slot = 0U;
        while (slot < allocation->spill_slots && slot_end[slot] >= interval->start) ++slot;
        if (slot == allocation->spill_slots) ++allocation->spill_slots;
        slot_end[slot] = interval->end;
        interval->location.stack_offset = -(int)(allocation->local_bytes + (slot + 1U) * 8U);
        ++allocation->spills;
    }
    unsigned saved = 0U;
    for (unsigned bit = 0U; bit < 4U; ++bit) if ((allocation->saved_gpr_mask & (1U << bit)) != 0U) ++saved;
    size_t variadic = function->type->variadic ? 22U : 0U;
    allocation->frame_size = (allocation->local_bytes + (allocation->spill_slots + saved + 14U + variadic) * 8U + 15U) & ~(size_t)15U;
    free(slot_end); free(calls.data); free(intervals); cinder_liveness_destroy(&live);
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
    const CinderIRFunction *function = allocation->ir;
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        const CinderIRBlock *block = &function->blocks.data[b];
        for (size_t p = 0U; p < block->predecessors.len; ++p) {
            CINDER_VEC_TYPE(CinderValueId) sources = {NULL, 0U, 0U}, destinations = {NULL, 0U, 0U};
            for (size_t i = 0U; i < block->instructions.len; ++i) {
                const CinderIRInst *phi = &block->instructions.data[i];
                if (phi->op != IR_PHI) continue;
                for (size_t a = 0U; a < phi->phi_blocks.len; ++a)
                    if (phi->phi_blocks.data[a] == block->predecessors.data[p]) {
                        cinder_vec_push((CinderVec *)&sources, &phi->args.data[a]);
                        cinder_vec_push((CinderVec *)&destinations, &phi->dst);
                    }
            }
            if (sources.len != 0U) {
                CinderParallelCopyPlan plan; cinder_parallel_copy_init(&plan);
                CinderDiagnostics diagnostics; cinder_diags_init(&diagnostics);
                (void)cinder_resolve_parallel_copies(sources.data, destinations.data, sources.len, &plan, &diagnostics);
                fprintf(out, "  parallel-copy edge=%u->%zu moves=%zu temporaries=%u\n", block->predecessors.data[p], b, plan.moves.len, plan.temporary_count);
                cinder_diags_destroy(&diagnostics); cinder_parallel_copy_destroy(&plan);
            }
            free(destinations.data); free(sources.data);
        }
    }
}
