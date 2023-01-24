#include "regalloc_private.h"

#include <stdlib.h>
#include <string.h>

static const char *register_name(CinderRegister reg) {
    static const char *names[] = {"rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15","rbp","rsp","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","none"};
    return reg < CINDER_ARRAY_LEN(names) ? names[reg] : "none";
}

void cinder_alloc_init(CinderAllocation *allocation, CinderIRFunction *function) {
    cinder_selected_mir_init(&allocation->machine);
    allocation->ir = function; allocation->intervals.data = NULL; allocation->intervals.len = 0U; allocation->intervals.cap = 0U; allocation->frame_size = 0U; allocation->spills = 0U; allocation->spill_slots = 0U; allocation->saved_gpr_mask = 0U;
    allocation->local_offsets.data = NULL; allocation->local_offsets.len = 0U; allocation->local_offsets.cap = 0U; allocation->local_bytes = 0U;
    memset(&allocation->frame, 0, sizeof(allocation->frame));
}

void cinder_alloc_destroy(CinderAllocation *allocation) { cinder_selected_mir_destroy(&allocation->machine); free(allocation->intervals.data); allocation->intervals.data = NULL; allocation->intervals.len = 0U; allocation->intervals.cap = 0U; free(allocation->local_offsets.data); allocation->local_offsets.data = NULL; allocation->local_offsets.len = 0U; allocation->local_offsets.cap = 0U; }

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

int cinder_allocate(CinderAllocation *allocation, CinderDiagnostics *diags) {
    const CinderIRFunction *function = allocation->ir;
    if (function == NULL || function->type == NULL || function->type->kind != TYPE_FUNCTION) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation requires a function signature"); return 1;
    }
    if (cinder_select_mir(function, &allocation->machine, diags) != 0) return 1;
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
            if ((allocation->machine.blocks.data[b].instructions.data[i].clobbers & UINT64_C(0x00fc0000)) != 0U)
                cinder_vec_push((CinderVec *)&calls, &position);
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
        bool floating = allocation->machine.values[current->value].bank == MIR_BANK_SSE;
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
    return cinder_plan_frame(allocation, diags) || cinder_plan_edge_copies(allocation, diags);
}

static void dump_phi_copy_plans(const CinderAllocation *allocation, FILE *out);

void cinder_dump_regalloc(const CinderAllocation *allocation, FILE *out) {
    fprintf(out, "allocation function=%s frame=%zu spills=%u\n", allocation->ir->name, allocation->frame_size, allocation->spills);
    fprintf(out, "  frame-program size=%zu incoming-base=%zu variadic=%d prologue=", allocation->frame.size, allocation->frame.incoming_base, allocation->frame.variadic_area);
    for (unsigned n = 0U; n < allocation->frame.prologue_size; ++n) fprintf(out, "%02x", allocation->frame.prologue[n]);
    fprintf(out, " epilogue=%02x%02x\n", allocation->frame.epilogue[0], allocation->frame.epilogue[1]);
    for (unsigned n = 0U; n < allocation->frame.saved_count; ++n) {
        const CinderMIRPreserved *saved = &allocation->frame.saved[n];
        fprintf(out, "  preserve reg=%u offset=%d save=", saved->reg, saved->offset);
        for (unsigned k = 0U; k < sizeof(saved->save); ++k) fprintf(out, "%02x", saved->save[k]);
        fputs(" restore=", out); for (unsigned k = 0U; k < sizeof(saved->restore); ++k) fprintf(out, "%02x", saved->restore[k]);
        fputc('\n', out);
    }
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


static const CinderLocation *copy_location(const CinderAllocation *allocation, CinderValueId value) {
    for (size_t i = 0U; i < allocation->intervals.len; ++i)
        if (allocation->intervals.data[i].value == value) return &allocation->intervals.data[i].location;
    return NULL;
}

static size_t copy_storage(const CinderLocation *location) {
    if (location == NULL) return SIZE_MAX;
    if (location->kind == LOC_REGISTER) return location->reg < REG_NONE ? (size_t)location->reg : SIZE_MAX;
    if (location->kind != LOC_STACK || location->stack_offset >= 0 || location->stack_offset % 8 != 0) return SIZE_MAX;
    return (size_t)REG_NONE + 1U + (size_t)(-(int64_t)location->stack_offset / 8);
}

int cinder_plan_edge_copies(CinderAllocation *allocation, CinderDiagnostics *diags) {
    for (size_t b = 0U; b < allocation->machine.blocks.len; ++b) {
        CinderMIRBlock *block = &allocation->machine.blocks.data[b];
        block->transfers.len = 0U; block->copy_inputs = 0U; block->copy_temporaries = 0U;
        if (block->control.kind != MIR_CONTROL_JUMP) continue;
        CinderBlockId to = block->control.targets[0];
        if ((size_t)to >= allocation->machine.blocks.len) { cinder_diag(diags, CINDER_FATAL, block->terminator.loc, "copy edge has no target"); return 1; }
        const CinderMIRBlock *target = &allocation->machine.blocks.data[to]; size_t count = 0U;
        for (size_t i = 0U; i < target->instructions.len; ++i) if (target->instructions.data[i].operands.op == IR_PHI) ++count;
        if (count == 0U) continue;
        CinderValueId *sources = cinder_alloc(count * sizeof(*sources)), *destinations = cinder_alloc(count * sizeof(*destinations));
        CinderValueId *source_values = cinder_alloc(count * sizeof(*source_values)), *destination_values = cinder_alloc(count * sizeof(*destination_values));
        size_t index = 0U;
        for (size_t i = 0U; i < target->instructions.len; ++i) {
            const CinderIRInst *phi = &target->instructions.data[i].operands; if (phi->op != IR_PHI) continue;
            size_t incoming = SIZE_MAX;
            for (size_t p = 0U; p < phi->phi_blocks.len; ++p) if (phi->phi_blocks.data[p] == b) incoming = p;
            if (incoming == SIZE_MAX || incoming >= phi->args.len) { cinder_diag(diags, CINDER_FATAL, phi->loc, "copy edge has no phi input"); break; }
            size_t source = copy_storage(copy_location(allocation, phi->args.data[incoming]));
            size_t destination = copy_storage(copy_location(allocation, phi->dst));
            if (source >= CINDER_INVALID_VALUE || destination >= CINDER_INVALID_VALUE) { cinder_diag(diags, CINDER_FATAL, phi->loc, "copy edge has no physical storage"); break; }
            sources[index] = (CinderValueId)source; destinations[index] = (CinderValueId)destination;
            source_values[index] = phi->args.data[incoming]; destination_values[index] = phi->dst; ++index;
        }
        CinderParallelCopyPlan plan; cinder_parallel_copy_init(&plan);
        if (diags->errors == 0U) (void)cinder_resolve_parallel_copies(sources, destinations, count, &plan, diags);
        CinderMIRBank temporary = MIR_BANK_NONE;
        for (size_t m = 0U; m < plan.moves.len && diags->errors == 0U; ++m) {
            CinderParallelCopy move = plan.moves.data[m]; CinderMIREdgeMove selected = {CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, MIR_BANK_NONE};
            for (size_t k = 0U; k < count; ++k) {
                if (sources[k] == move.source) selected.source = source_values[k];
                if (destinations[k] == move.destination) selected.destination = destination_values[k];
            }
            selected.bank = selected.source != CINDER_INVALID_VALUE ? allocation->machine.values[selected.source].bank : temporary;
            if ((move.source != CINDER_INVALID_VALUE && selected.source == CINDER_INVALID_VALUE) ||
                (move.destination != CINDER_INVALID_VALUE && selected.destination == CINDER_INVALID_VALUE) || selected.bank == MIR_BANK_NONE) {
                cinder_diag(diags, CINDER_FATAL, block->terminator.loc, "copy schedule has an absent operand or bank"); break;
            }
            if (selected.destination == CINDER_INVALID_VALUE) temporary = selected.bank;
            cinder_vec_push((CinderVec *)&block->transfers, &selected);
        }
        block->copy_inputs = count; block->copy_temporaries = plan.temporary_count;
        cinder_parallel_copy_destroy(&plan); free(destination_values); free(source_values); free(destinations); free(sources);
        if (diags->errors != 0U) return 1;
    }
    return cinder_verify_edge_copies(allocation, diags);
}

/* This checker simulates storage tokens. It does not invoke the scheduler or
 * compare against another schedule produced by the same copy resolver. */
int cinder_verify_edge_copies(const CinderAllocation *allocation, CinderDiagnostics *diags) {
    const CinderIRFunction *function = allocation->ir;
    if (function == NULL || function->value_count > 1000000U || allocation->machine.value_count != function->value_count ||
        allocation->machine.blocks.len != function->blocks.len || allocation->frame_size > 128U * 1024U * 1024U ||
        (function->value_count != 0U && allocation->machine.values == NULL) ||
        (function->blocks.len != 0U && allocation->machine.blocks.data == NULL)) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "copy checker has inconsistent machine dimensions"); return 1;
    }
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        const CinderMIRBlock *block = &allocation->machine.blocks.data[b]; const CinderTerminator *term = &function->blocks.data[b].terminator;
        size_t count = 0U; const CinderIRBlock *target = NULL;
        if (term->kind == TERM_JUMP && (size_t)term->target < function->blocks.len) {
            target = &function->blocks.data[term->target];
            for (size_t i = 0U; i < target->instructions.len; ++i) if (target->instructions.data[i].op == IR_PHI) ++count;
        }
        if (block->copy_inputs != count || block->transfers.len > count * 2U || block->copy_temporaries > count ||
            (block->transfers.len != 0U && block->transfers.data == NULL)) {
            cinder_diag(diags, CINDER_FATAL, term->loc, "copy schedule has inconsistent extents"); return 1;
        }
        if (term->kind == TERM_BRANCH) {
            CinderBlockId successors[2] = {term->yes, term->no};
            for (unsigned s = 0U; s < 2U; ++s) if ((size_t)successors[s] < function->blocks.len) {
                const CinderIRBlock *successor = &function->blocks.data[successors[s]];
                for (size_t i = 0U; i < successor->instructions.len; ++i) if (successor->instructions.data[i].op == IR_PHI)
                    cinder_diag(diags, CINDER_FATAL, term->loc, "conditional phi edge was not split before allocation");
            }
        }
        if (count == 0U) continue;
        size_t cells = (size_t)REG_NONE + 2U + allocation->frame_size / 8U;
        CinderValueId *tokens = cinder_alloc(cells * sizeof(*tokens));
        CinderValueId *inputs = cinder_alloc(count * sizeof(*inputs)), *outputs = cinder_alloc(count * sizeof(*outputs));
        for (size_t k = 0U; k < cells; ++k) tokens[k] = CINDER_INVALID_VALUE;
        size_t index = 0U;
        for (size_t i = 0U; i < target->instructions.len; ++i) {
            const CinderIRInst *phi = &target->instructions.data[i]; if (phi->op != IR_PHI) continue;
            size_t incoming = SIZE_MAX;
            for (size_t p = 0U; p < phi->phi_blocks.len; ++p) if (phi->phi_blocks.data[p] == b) incoming = p;
            if (incoming == SIZE_MAX || incoming >= phi->args.len) { cinder_diag(diags, CINDER_FATAL, phi->loc, "copy checker has no phi source"); break; }
            inputs[index] = phi->args.data[incoming]; outputs[index] = phi->dst;
            size_t storage = copy_storage(copy_location(allocation, inputs[index]));
            if ((size_t)inputs[index] >= function->value_count || (size_t)outputs[index] >= function->value_count || storage >= cells ||
                (tokens[storage] != CINDER_INVALID_VALUE && tokens[storage] != inputs[index])) {
                cinder_diag(diags, CINDER_FATAL, phi->loc, "phi sources have absent or conflicting physical storage"); break;
            }
            tokens[storage] = inputs[index]; ++index;
        }
        CinderValueId temporary[3] = {CINDER_INVALID_VALUE,CINDER_INVALID_VALUE,CINDER_INVALID_VALUE}; unsigned saved = 0U;
        for (size_t m = 0U; m < block->transfers.len && diags->errors == 0U; ++m) {
            const CinderMIREdgeMove *move = &block->transfers.data[m];
            bool source = move->source == CINDER_INVALID_VALUE, destination = move->destination == CINDER_INVALID_VALUE;
            for (size_t i = 0U; i < count; ++i) { source = source || move->source == inputs[i]; destination = destination || move->destination == outputs[i]; }
            if (!source || !destination || (move->bank != MIR_BANK_GPR && move->bank != MIR_BANK_SSE) ||
                (move->source == CINDER_INVALID_VALUE && move->destination == CINDER_INVALID_VALUE) ||
                (move->source != CINDER_INVALID_VALUE && allocation->machine.values[move->source].bank != move->bank) ||
                (move->destination != CINDER_INVALID_VALUE && allocation->machine.values[move->destination].bank != move->bank)) {
                cinder_diag(diags, CINDER_FATAL, term->loc, "copy move has an invalid endpoint or register bank"); break;
            }
            size_t read = copy_storage(copy_location(allocation, move->source)), write = copy_storage(copy_location(allocation, move->destination));
            CinderValueId token = move->source == CINDER_INVALID_VALUE ? temporary[move->bank] : read < cells ? tokens[read] : CINDER_INVALID_VALUE;
            if (token == CINDER_INVALID_VALUE || (move->destination != CINDER_INVALID_VALUE && write >= cells)) {
                cinder_diag(diags, CINDER_FATAL, term->loc, "copy move reads an unavailable storage token"); break;
            }
            if (move->destination == CINDER_INVALID_VALUE) { temporary[move->bank] = token; ++saved; }
            else tokens[write] = token;
        }
        for (size_t i = 0U; i < index && diags->errors == 0U; ++i) {
            size_t storage = copy_storage(copy_location(allocation, outputs[i]));
            if (storage >= cells || tokens[storage] != inputs[i]) cinder_diag(diags, CINDER_FATAL, term->loc, "copy schedule does not preserve simultaneous phi values");
        }
        if (saved != block->copy_temporaries) cinder_diag(diags, CINDER_FATAL, term->loc, "copy temporary accounting is inconsistent");
        free(outputs); free(inputs); free(tokens);
        if (diags->errors != 0U) return 1;
    }
    return diags->errors == 0U ? 0 : 1;
}

static void dump_phi_copy_plans(const CinderAllocation *allocation, FILE *out) {
    for (size_t b = 0U; b < allocation->machine.blocks.len; ++b) {
        const CinderMIRBlock *block = &allocation->machine.blocks.data[b]; if (block->copy_inputs == 0U) continue;
        fprintf(out, "  parallel-copy edge=%zu->%u inputs=%zu moves=%zu temporaries=%u\n", b, block->control.targets[0], block->copy_inputs, block->transfers.len, block->copy_temporaries);
        for (size_t i = 0U; i < block->transfers.len; ++i) {
            const CinderMIREdgeMove *move = &block->transfers.data[i];
            fprintf(out, "    transfer source=%u destination=%u bank=%u\n", move->source, move->destination, (unsigned)move->bank);
        }
    }
}
