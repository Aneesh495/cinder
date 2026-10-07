#include "cinder.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static uint64_t state;
static uint32_t random_word(void) {
    state ^= state << 13U; state ^= state >> 7U; state ^= state << 17U;
    return (uint32_t)state;
}

static CinderIRInst instruction(CinderIROp op, CinderValueId dst, CinderValueId left, CinderValueId right, CinderType *type) {
    CinderIRInst inst; memset(&inst, 0, sizeof(inst));
    inst.op = op; inst.dst = dst; inst.left = left; inst.right = right; inst.slot = -1; inst.type = type;
    return inst;
}

static void edge(CinderIRFunction *function, CinderBlockId from, CinderBlockId to) {
    cinder_vec_push((CinderVec *)&function->blocks.data[from].successors, &to);
    cinder_vec_push((CinderVec *)&function->blocks.data[to].predecessors, &from);
}

static void destroy_graph(CinderIRFunction *function) {
    for (size_t b = 0U; b < function->blocks.len; ++b) {
        CinderIRBlock *block = &function->blocks.data[b];
        for (size_t i = 0U; i < block->instructions.len; ++i) {
            free(block->instructions.data[i].args.data); free(block->instructions.data[i].phi_blocks.data);
        }
        free(block->instructions.data); free(block->predecessors.data); free(block->successors.data);
    }
    free(function->blocks.data); free(function->local_types.data); free(function->local_alignments.data);
}

static void create_graph(CinderIRFunction *function, CinderType *integer, CinderType *floating, unsigned kind) {
    memset(function, 0, sizeof(*function)); function->name = "allocation_probe";
    size_t block_count = 4U + random_word() % 9U;
    for (size_t b = 0U; b < block_count; ++b) {
        CinderIRBlock block; memset(&block, 0, sizeof(block)); block.id = (CinderBlockId)b;
        block.terminator.value = CINDER_INVALID_VALUE; block.terminator.condition = CINDER_INVALID_VALUE;
        cinder_vec_push((CinderVec *)&function->blocks, &block);
    }
    size_t roots = 8U + random_word() % 32U;
    for (size_t r = 0U; r < roots; ++r) {
        bool fp = (r + kind) % 3U == 0U;
        CinderIRInst inst = instruction(fp ? IR_FCONST : IR_CONST, (CinderValueId)function->value_count++, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, fp ? floating : integer);
        inst.integer = (int64_t)(random_word() % 103U); inst.floating = (double)inst.integer;
        cinder_vec_push((CinderVec *)&function->blocks.data[0].instructions, &inst);
    }
    for (size_t b = 1U; b < block_count; ++b) {
        CinderIRBlock *block = &function->blocks.data[b];
        /* Every entry value is used in every block, creating pressure across
         * branches, calls, and loop backedges, rather than dead definitions. */
        for (size_t r = 0U; r < roots; ++r) {
            bool fp = (r + kind) % 3U == 0U;
            CinderIRInst inst = instruction(fp ? IR_FADD : IR_ADD, (CinderValueId)function->value_count++, (CinderValueId)r, (CinderValueId)r, fp ? floating : integer);
            cinder_vec_push((CinderVec *)&block->instructions, &inst);
            if ((random_word() & 7U) == 0U) {
                CinderIRInst call = instruction(IR_CALL, (CinderValueId)function->value_count++, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, integer);
                call.callee = "opaque";
                cinder_vec_push((CinderVec *)&call.args, &inst.dst);
                cinder_vec_push((CinderVec *)&block->instructions, &call);
            }
        }
    }
    for (size_t b = 0U; b < block_count; ++b) {
        CinderIRBlock *block = &function->blocks.data[b];
        if (b + 1U == block_count) {
            block->terminator.kind = TERM_RETURN;
            block->terminator.value = block->instructions.data[block->instructions.len - 1U].dst;
        } else if (b == 0U) {
            block->terminator.kind = TERM_JUMP; block->terminator.target = 1U; edge(function, 0U, 1U);
        } else {
            block->terminator.kind = TERM_BRANCH; size_t condition = 0U;
            while ((condition + kind) % 3U == 0U) ++condition;
            block->terminator.condition = (CinderValueId)condition;
            block->terminator.yes = (CinderBlockId)(b + 1U);
            block->terminator.no = (CinderBlockId)(1U + random_word() % b);
            edge(function, (CinderBlockId)b, block->terminator.yes);
            if (block->terminator.no != block->terminator.yes) edge(function, (CinderBlockId)b, block->terminator.no);
        }
    }
    CinderIRBlock *header = &function->blocks.data[1];
    for (unsigned cls = 0U; cls < 2U; ++cls) {
        size_t root = 0U;
        while (((root + kind) % 3U == 0U) != (cls == 1U)) ++root;
        CinderType *type = cls == 1U ? floating : integer;
        CinderIRInst phi = instruction(IR_PHI, (CinderValueId)function->value_count++, CINDER_INVALID_VALUE, CINDER_INVALID_VALUE, type);
        for (size_t p = 0U; p < header->predecessors.len; ++p) {
            CinderBlockId predecessor = header->predecessors.data[p];
            CinderValueId source = (CinderValueId)root;
            if (predecessor != 0U) {
                const CinderIRBlock *before = &function->blocks.data[predecessor];
                for (size_t i = 0U; i < before->instructions.len; ++i)
                    if (before->instructions.data[i].left == root) { source = before->instructions.data[i].dst; break; }
            }
            cinder_vec_push((CinderVec *)&phi.args, &source);
            cinder_vec_push((CinderVec *)&phi.phi_blocks, &predecessor);
        }
        CinderIRInst *expanded = cinder_alloc((header->instructions.len + 1U) * sizeof(*expanded));
        expanded[0] = phi; memcpy(expanded + 1U, header->instructions.data, header->instructions.len * sizeof(*expanded));
        free(header->instructions.data); header->instructions.data = expanded;
        ++header->instructions.len; header->instructions.cap = header->instructions.len;
        CinderIRInst use = instruction(cls == 1U ? IR_FADD : IR_ADD, (CinderValueId)function->value_count++, phi.dst, phi.dst, type);
        cinder_vec_push((CinderVec *)&header->instructions, &use);
    }

}

static bool rejected_mutation(CinderAllocation *allocation, CinderIRFunction *function, unsigned kind) {
    CinderDiagnostics diagnostics; cinder_diags_init(&diagnostics);
    CinderInterval *first = &allocation->intervals.data[0];
    CinderLocation old = first->location;
    unsigned old_mask = allocation->saved_gpr_mask;
    size_t old_frame = allocation->frame_size, old_bytes = allocation->local_bytes;
    int old_local = allocation->local_offsets.data[0];
    size_t old_alignments[2] = {function->local_alignments.data[0], function->local_alignments.data[1]};
    size_t old_alignment_count = function->local_alignments.len;
    if (kind == 0U) first->location = (CinderLocation){LOC_REGISTER, REG_R10, 0};
    if (kind == 1U) first->location = (CinderLocation){LOC_STACK, REG_NONE, -8};
    if (kind == 2U) first->location = (CinderLocation){LOC_STACK, REG_NONE, -(int)allocation->frame_size - 8};
    if (kind == 3U) {
        first->location = allocation->intervals.data[1].location;
        /* Both root definitions are live at the end of the entry block. */
    }
    if (kind == 4U) { first->location = (CinderLocation){LOC_REGISTER, REG_R12, 0}; allocation->saved_gpr_mask = 0U; }
    if (kind == 5U) allocation->local_offsets.data[0] = allocation->local_offsets.data[1];
    if (kind == 6U) allocation->local_bytes += 8U;
    if (kind == 7U) allocation->frame_size = allocation->local_bytes;
    if (kind == 8U) allocation->saved_gpr_mask |= 16U;
    if (kind == 9U) first->location = (CinderLocation){LOC_STACK, REG_NONE, -(int)(allocation->local_bytes + (allocation->spill_slots + 1U) * 8U)};
    if (kind == 10U) function->local_alignments.data[0] = 3U;
    if (kind == 11U) function->local_alignments.data[1] = 1U;
    if (kind == 12U) function->local_alignments.data[0] = 32U;
    if (kind == 13U) function->local_alignments.len = 1U;
    int result = cinder_verify_allocation(allocation, &diagnostics);
    bool rejected = result != 0 && diagnostics.errors != 0U;
    first->location = old; allocation->saved_gpr_mask = old_mask;
    allocation->local_offsets.data[0] = old_local; allocation->frame_size = old_frame; allocation->local_bytes = old_bytes;
    function->local_alignments.len = old_alignment_count;
    function->local_alignments.data[0] = old_alignments[0]; function->local_alignments.data[1] = old_alignments[1];
    cinder_diags_destroy(&diagnostics); return rejected;
}

int main(int argc, char **argv) {
    size_t count = argc > 1 ? (size_t)strtoul(argv[1], NULL, 10) : 10000U;
    if (count == 0U) return 2;
    CinderType integer; memset(&integer, 0, sizeof(integer)); integer.kind = TYPE_LONG; integer.size = 8U; integer.align = 8U; integer.complete = true;
    CinderType floating; memset(&floating, 0, sizeof(floating)); floating.kind = TYPE_DOUBLE; floating.size = 8U; floating.align = 8U; floating.complete = true;
    CinderType array = integer; array.kind = TYPE_ARRAY; array.size = 37U; array.align = 1U;
    CinderType aggregate = integer; aggregate.kind = TYPE_STRUCT; aggregate.size = 48U; aggregate.align = 16U;
    CinderType signature; memset(&signature, 0, sizeof(signature)); signature.kind = TYPE_FUNCTION; signature.return_type = &integer; signature.align = 1U; signature.complete = true;
    unsigned mutations = 0U;
    for (size_t test = 0U; test < count; ++test) {
        state = UINT64_C(0x62762dcf87102) + test * UINT64_C(0x9e3779b97f4a7c15);
        CinderIRFunction function; create_graph(&function, &integer, &floating, (unsigned)(test % 7U));
        function.local_count = 2U;
        CinderType *first_type = test % 2U == 0U ? &integer : &array, *second_type = test % 3U == 0U ? &aggregate : &floating;
        cinder_vec_push((CinderVec *)&function.local_types, &first_type); cinder_vec_push((CinderVec *)&function.local_types, &second_type);
        size_t first_alignment = test % 2U == 0U ? 16U : 0U, second_alignment = test % 3U == 0U ? 0U : 16U;
        cinder_vec_push((CinderVec *)&function.local_alignments, &first_alignment); cinder_vec_push((CinderVec *)&function.local_alignments, &second_alignment);
        CinderDiagnostics diagnostics; cinder_diags_init(&diagnostics);
        CinderAllocation allocation; cinder_alloc_init(&allocation, &function);
        if (test == 0U) {
            CinderDiagnostics invalid; cinder_diags_init(&invalid);
            if (cinder_allocate(&allocation, &invalid) == 0 || cinder_verify_allocation(&allocation, &invalid) == 0 || invalid.errors != 2U) return 1;
            cinder_diags_destroy(&invalid);
        }
        signature.variadic = test % 2U != 0U; function.type = &signature;
        if (cinder_allocate(&allocation, &diagnostics) != 0 || cinder_verify_allocation(&allocation, &diagnostics) != 0) {
            fprintf(stderr, "allocation failed seed=%zu errors=%u\n", test, diagnostics.errors); return 1;
        }
        if (test < 1000U) {
            if (!rejected_mutation(&allocation, &function, (unsigned)(test % 14U))) { fprintf(stderr, "checker accepted mutation %zu\n", test); return 1; }
            ++mutations;
        }
        printf("{\"seed\":%zu,\"blocks\":%zu,\"values\":%zu,\"spills\":%u,\"slots\":%zu,\"frame\":%zu,\"variadic\":%s,\"checker\":true}\n", test, function.blocks.len, function.value_count, allocation.spills, allocation.spill_slots, allocation.frame_size, signature.variadic ? "true" : "false");
        cinder_alloc_destroy(&allocation); cinder_diags_destroy(&diagnostics); destroy_graph(&function);
    }
    fprintf(stderr, "allocation: %zu graphs and %u rejected mutations passed\n", count, mutations);
    return 0;
}
