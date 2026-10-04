#include "cinder.h"

#include <stdlib.h>
#include <string.h>

/* This checker reconstructs sets by walking instructions backwards. It never
 * reads the allocator's interval bounds or its bitset liveness analysis. */
static void add_use(bool *set, CinderValueId value, size_t count) {
    if ((size_t)value < count) set[value] = true;
}

static void backwards_instruction(bool *set, const CinderIRInst *inst, size_t count) {
    if ((size_t)inst->dst < count) set[inst->dst] = false;
    if (inst->op == IR_PHI) return;
    add_use(set, inst->left, count); add_use(set, inst->right, count);
    for (size_t a = 0U; a < inst->args.len; ++a) add_use(set, inst->args.data[a], count);
}

static void check_point(const bool *live, const CinderLocation *const *locations, size_t count, CinderValueId *owners, size_t storage_count, CinderDiagnostics *diags) {
    for (size_t k = 0U; k < storage_count; ++k) owners[k] = CINDER_INVALID_VALUE;
    for (size_t a = 0U; a < count; ++a) {
        if (!live[a]) continue;
        const CinderLocation *location = locations[a];
        if (location == NULL) {
            cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "live value %zu has no allocation", a);
            continue;
        }
        size_t key = location->kind == LOC_REGISTER ? (size_t)location->reg : (size_t)REG_NONE + 1U + (size_t)(-(int64_t)location->stack_offset / 8);
        if (key >= storage_count) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "invalid allocation storage key"); continue; }
        if (owners[key] != CINDER_INVALID_VALUE)
            cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "live values %u and %zu share physical storage", owners[key], a);
        owners[key] = (CinderValueId)a;
    }
}

int cinder_verify_allocation(const CinderAllocation *allocation, CinderDiagnostics *diags) {
    const CinderIRFunction *function = allocation->ir;
    size_t count = function->value_count;
    size_t blocks = function->blocks.len;
    if (allocation->local_offsets.len != function->local_count || function->local_types.len != function->local_count) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation has an invalid local storage table"); return 1;
    }
    size_t local_extent = 0U;
    for (size_t s = 0U; s < function->local_count; ++s) {
        const CinderType *type = function->local_types.data[s];
        if (type == NULL || type->align == 0U || type->align > 16U || type->size > 64U * 1024U * 1024U) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation has invalid local object storage"); return 1; }
        size_t size = type->size < 8U ? 8U : type->size, align = type->align < 8U ? 8U : type->align;
        size_t end = local_extent + size;
        if (end % align != 0U) end += align - end % align;
        if (allocation->local_offsets.data[s] >= 0 || -(int64_t)allocation->local_offsets.data[s] != (int64_t)end) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocated local objects overlap or have incorrect extent/alignment");
        local_extent = end;
    }
    if (allocation->local_bytes != local_extent) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocated local area has incorrect size");
    unsigned preserved = 0U;
    for (unsigned bit = 0U; bit < 4U; ++bit) if ((allocation->saved_gpr_mask & (1U << bit)) != 0U) ++preserved;
    if ((allocation->saved_gpr_mask & ~15U) != 0U || allocation->frame_size < local_extent + (allocation->spill_slots + preserved + 14U) * 8U) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation does not own all spill, preservation, and incoming argument storage");
    if ((allocation->frame_size & 15U) != 0U)
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation frame is not 16-byte aligned");
    if (count != 0U && blocks > (128U * 1024U * 1024U) / count) {
        cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation checker storage limit exceeded");
        return 1;
    }
    size_t width = count == 0U ? 1U : count;
    const CinderLocation **locations = cinder_alloc(width * sizeof(*locations));
    memset(locations, 0, width * sizeof(*locations));
    for (size_t i = 0U; i < allocation->intervals.len; ++i) {
        const CinderInterval *interval = &allocation->intervals.data[i];
        if ((size_t)interval->value >= count || locations[interval->value] != NULL) {
            cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "duplicate or invalid allocated value");
            continue;
        }
        const CinderLocation *location = &interval->location;
        locations[interval->value] = location;
        if (location->kind == LOC_REGISTER) {
            bool gpr = location->reg >= REG_R12 && location->reg <= REG_R15;
            bool sse = location->reg >= REG_XMM2 && location->reg <= REG_XMM7;
            if (!gpr && !sse) cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation uses a reserved register");
            if (gpr && (allocation->saved_gpr_mask & (1U << (unsigned)(location->reg - REG_R12))) == 0U)
                cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocated callee-saved register is not preserved");
        } else if (location->kind != LOC_STACK || location->stack_offset >= 0 || location->stack_offset % 8 != 0 || -(int64_t)location->stack_offset > (int64_t)(allocation->local_bytes + allocation->spill_slots * 8U) || -(int64_t)location->stack_offset <= (int64_t)allocation->local_bytes) {
            cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation stack slot is outside its owned frame area");
        }
    }
    if (diags->errors != 0U) { free(locations); return 1; }
    size_t storage_count = (size_t)REG_NONE + 2U + allocation->frame_size / 8U;
    CinderValueId *owners = cinder_alloc(storage_count * sizeof(*owners));
    bool *input = cinder_alloc(blocks * width * sizeof(*input));
    bool *output = cinder_alloc(blocks * width * sizeof(*output));
    bool *set = cinder_alloc(width * sizeof(*set));
    memset(input, 0, blocks * width * sizeof(*input));
    memset(output, 0, blocks * width * sizeof(*output));
    bool changed;
    do {
        changed = false;
        for (size_t bi = blocks; bi > 0U; --bi) {
            size_t b = bi - 1U;
            const CinderIRBlock *block = &function->blocks.data[b];
            memset(set, 0, width * sizeof(*set));
            for (size_t s = 0U; s < block->successors.len; ++s) {
                CinderBlockId target = block->successors.data[s];
                if (target >= blocks) { cinder_diag(diags, CINDER_FATAL, (CinderLoc){0}, "allocation checker found invalid CFG edge"); continue; }
                for (size_t v = 0U; v < count; ++v) set[v] = set[v] || input[(size_t)target * width + v];
                const CinderIRBlock *successor = &function->blocks.data[target];
                for (size_t i = 0U; i < successor->instructions.len; ++i) {
                    const CinderIRInst *phi = &successor->instructions.data[i];
                    if (phi->op != IR_PHI) continue;
                    for (size_t p = 0U; p < phi->phi_blocks.len && p < phi->args.len; ++p)
                        if (phi->phi_blocks.data[p] == b) add_use(set, phi->args.data[p], count);
                }
            }
            if (memcmp(output + b * width, set, width * sizeof(*set)) != 0) changed = true;
            memcpy(output + b * width, set, width * sizeof(*set));
            add_use(set, block->terminator.value, count); add_use(set, block->terminator.condition, count);
            for (size_t i = block->instructions.len; i > 0U; --i) backwards_instruction(set, &block->instructions.data[i - 1U], count);
            if (memcmp(input + b * width, set, width * sizeof(*set)) != 0) changed = true;
            memcpy(input + b * width, set, width * sizeof(*set));
        }
    } while (changed);
    for (size_t b = 0U; b < blocks && diags->errors == 0U; ++b) {
        const CinderIRBlock *block = &function->blocks.data[b];
        memcpy(set, output + b * width, width * sizeof(*set));
        add_use(set, block->terminator.value, count); add_use(set, block->terminator.condition, count);
        check_point(set, locations, count, owners, storage_count, diags);
        for (size_t i = block->instructions.len; i > 0U && diags->errors == 0U; --i) {
            const CinderIRInst *inst = &block->instructions.data[i - 1U];
            if ((size_t)inst->dst < count) {
                const CinderLocation *location = locations[inst->dst];
                bool floating = inst->type != NULL && (inst->type->kind == TYPE_FLOAT || inst->type->kind == TYPE_DOUBLE);
                if (location == NULL) cinder_diag(diags, CINDER_FATAL, inst->loc, "instruction result has no physical location");
                else if (location->kind == LOC_REGISTER && floating != (location->reg >= REG_XMM2 && location->reg <= REG_XMM7))
                    cinder_diag(diags, CINDER_FATAL, inst->loc, "instruction result uses the wrong register class");
                set[inst->dst] = true;
                check_point(set, locations, count, owners, storage_count, diags);
                set[inst->dst] = false;
            }
            if (inst->op == IR_CALL)
                for (size_t v = 0U; v < count; ++v)
                    if (set[v] && locations[v] != NULL && locations[v]->kind == LOC_REGISTER && locations[v]->reg >= REG_XMM2)
                        cinder_diag(diags, CINDER_FATAL, inst->loc, "call clobbers a live allocated SSE value");
            backwards_instruction(set, inst, count);
            check_point(set, locations, count, owners, storage_count, diags);
        }
    }
    free(owners); free(set); free(output); free(input); free(locations);
    return diags->errors == 0U ? 0 : 1;
}
