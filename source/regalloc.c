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

int cinder_allocate(CinderAllocation *allocation, CinderDiagnostics *diags) {
    (void)diags;
    size_t count = allocation->ir->value_count;
    CinderInterval *intervals = cinder_alloc((count == 0U ? 1U : count) * sizeof(*intervals));
    bool *seen = cinder_alloc((count == 0U ? 1U : count) * sizeof(*seen));
    for (size_t i = 0U; i < count; ++i) { intervals[i].value = (CinderValueId)i; intervals[i].start = SIZE_MAX; intervals[i].end = 0U; intervals[i].location.kind = LOC_STACK; intervals[i].location.reg = REG_NONE; intervals[i].location.stack_offset = 0; seen[i] = false; }
    size_t position = 0U;
    for (size_t b = 0U; b < allocation->ir->blocks.len; ++b) {
        CinderIRBlock *block = &allocation->ir->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i, ++position) {
            CinderIRInst *inst = &block->instructions.data[i];
            CinderValueId uses[2] = {inst->left, inst->right};
            for (size_t u = 0U; u < 2U; ++u) if (uses[u] != CINDER_INVALID_VALUE && uses[u] < count) { if (!seen[uses[u]]) { intervals[uses[u]].start = position; seen[uses[u]] = true; } intervals[uses[u]].end = position; }
            for (size_t a = 0U; a < inst->args.len; ++a) if (inst->args.data[a] < count) { if (!seen[inst->args.data[a]]) { intervals[inst->args.data[a]].start = position; seen[inst->args.data[a]] = true; } intervals[inst->args.data[a]].end = position; }
            if (inst->dst != CINDER_INVALID_VALUE && inst->dst < count) { intervals[inst->dst].start = position; intervals[inst->dst].end = position; seen[inst->dst] = true; }
        }
        if (block->terminator.condition != CINDER_INVALID_VALUE && block->terminator.condition < count) intervals[block->terminator.condition].end = position;
        if (block->terminator.value != CINDER_INVALID_VALUE && block->terminator.value < count) intervals[block->terminator.value].end = position;
    }
    int registers[] = {REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15};
    for (size_t i = 0U; i < count; ++i) {
        if (!seen[i]) continue;
        CinderInterval interval = intervals[i];
        if (i < CINDER_ARRAY_LEN(registers)) { interval.location.kind = LOC_REGISTER; interval.location.reg = (CinderRegister)registers[i]; }
        else { interval.location.kind = LOC_STACK; interval.location.reg = REG_NONE; allocation->spills++; }
        interval.location.stack_offset = -(int)((i + allocation->ir->local_count + 1U) * 8U);
        cinder_vec_push((CinderVec *)&allocation->intervals, &interval);
    }
    size_t slots = allocation->ir->local_count + allocation->spills;
    allocation->frame_size = (slots * 8U + 15U) & ~((size_t)15U);
    free(seen); free(intervals);
    return 0;
}

void cinder_dump_regalloc(const CinderAllocation *allocation, FILE *out) {
    fprintf(out, "allocation function=%s frame=%zu spills=%u\n", allocation->ir->name, allocation->frame_size, allocation->spills);
    for (size_t i = 0U; i < allocation->intervals.len; ++i) {
        const CinderInterval *interval = &allocation->intervals.data[i];
        if (interval->location.kind == LOC_REGISTER) fprintf(out, "  %%v%u [%zu,%zu] -> %s\n", interval->value, interval->start, interval->end, register_name(interval->location.reg));
        else fprintf(out, "  %%v%u [%zu,%zu] -> stack %d(%%rbp)\n", interval->value, interval->start, interval->end, interval->location.stack_offset);
    }
}
