BUILD_DIR ?= build
BUILD_TYPE ?= Debug
CMAKE ?= cmake

.PHONY: all bootstrap build test test-frontend test-globals test-multi test-preprocessor test-ir test-ir-text test-ir-campaign test-ssa test-control test-undefined test-memory test-literals test-linkage test-static-addresses test-initializers test-aggregates test-aggregate-abi test-variadic test-compound-literals test-static-assert test-generic test-alignment test-noreturn test-block-storage test-goto test-switch test-register test-offset test-runtime-headers test-flexible test-anonymous test-bitfields test-qualifiers test-optimizer test-passes test-pointer-words test-rewrites test-constraints test-numeric test-storage test-allocation test-parallel-copy test-float test-varargs test-generated test-differential test-apps test-native-linux test-abi test-abi-callbacks test-abi-aggregates test-abi-variadic test-object test-object-campaign test-debug test-output fuzz selfhost benchmark demo acceptance verify clean

all: build

bootstrap:
	@command -v cmake >/dev/null || (echo 'CMake 3.20+ is required' >&2; exit 1)
	@command -v cc >/dev/null || (echo 'A C17 host compiler is required for stage 1' >&2; exit 1)
	@$(CMAKE) -S . -B $(BUILD_DIR) -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: bootstrap
	@$(CMAKE) --build $(BUILD_DIR) --parallel

test: build
	@ctest --test-dir $(BUILD_DIR) --output-on-failure

test-frontend: build
	@tests/run_frontend.sh $(BUILD_DIR)/cindercc

test-globals: build
	@tests/run_globals.sh $(BUILD_DIR)/cindercc

test-multi: build
	@tests/run_multi.sh $(BUILD_DIR)/cindercc

test-preprocessor: build
	@tests/run_preprocessor.sh $(BUILD_DIR)/cindercc
	@python3 tests/test_preprocessor.py $(BUILD_DIR)/cindercc

test-ir: build
	@tests/run_ir.sh $(BUILD_DIR)/cindercc

test-ir-campaign: build
	@$(CMAKE) --build $(BUILD_DIR) --target cinder_ir_campaign --parallel
	@python3 tests/test_ir_campaign.py $(BUILD_DIR)/cinder_ir_campaign 100000

test-ir-text: build
	@python3 tests/test_ir_text.py $(BUILD_DIR)/cindercc

test-ssa: build
	@$(CMAKE) --build $(BUILD_DIR) --target cinder_ssa_probe --parallel
	@python3 tests/test_ssa.py $(BUILD_DIR)/cinder_ssa_probe 1000

test-control: build
	@python3 tests/test_control.py $(BUILD_DIR)/cindercc

test-undefined: build
	@python3 tests/test_undefined.py $(BUILD_DIR)/cindercc

test-constraints: build
	@python3 tests/test_constraints.py $(BUILD_DIR)/cindercc

test-numeric: build
	@python3 tests/test_numeric.py $(BUILD_DIR)/cindercc

test-storage:
	@tests/run_storage.sh

test-initializers: build
	@python3 tests/test_reference_policy.py
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc

test-aggregates: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc aggregates

test-aggregate-abi: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc aggregate_abi

test-variadic: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc variadic

test-compound-literals: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc compound_literals

test-static-assert: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc static_assertions

test-generic: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc generic

test-alignment: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc alignment
	@python3 tests/test_alignment_contracts.py $(BUILD_DIR)/cindercc

test-noreturn: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc noreturn
	@python3 tests/test_noreturn_contracts.py $(BUILD_DIR)/cindercc

test-block-storage: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc block_storage
	@python3 tests/test_block_storage_contracts.py $(BUILD_DIR)/cindercc

test-goto: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc goto
	@python3 tests/test_goto_contracts.py $(BUILD_DIR)/cindercc

test-switch: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc switch
	@python3 tests/test_switch_contracts.py $(BUILD_DIR)/cindercc

test-register: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc register
	@python3 tests/test_register_contracts.py $(BUILD_DIR)/cindercc

test-offset: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc offset
	@python3 tests/test_offset_contracts.py $(BUILD_DIR)/cindercc

test-runtime-headers: build
	@python3 tests/test_runtime_headers.py $(BUILD_DIR)/cindercc

test-flexible: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc flexible
	@python3 tests/test_flexible_contracts.py $(BUILD_DIR)/cindercc
	@python3 tests/test_runtime_headers.py $(BUILD_DIR)/cindercc flexible_allocated
	@python3 tests/test_block_storage_contracts.py $(BUILD_DIR)/cindercc flexible

test-anonymous: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc anonymous
	@python3 tests/test_anonymous_contracts.py $(BUILD_DIR)/cindercc
	@python3 tests/test_block_storage_contracts.py $(BUILD_DIR)/cindercc anonymous

test-bitfields: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc bitfields
	@python3 tests/test_bitfield_contracts.py $(BUILD_DIR)/cindercc
	@python3 tests/test_block_storage_contracts.py $(BUILD_DIR)/cindercc bitfields

test-qualifiers: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc qualifiers
	@python3 tests/test_qualifier_contracts.py $(BUILD_DIR)/cindercc

test-optimizer: build
	@python3 tests/test_cfg_optimization.py $(BUILD_DIR)/cindercc
	@python3 tests/test_sparse_optimization.py $(BUILD_DIR)/cindercc
	@python3 tests/test_value_numbering.py $(BUILD_DIR)/cindercc
	@python3 tests/test_dead_code.py $(BUILD_DIR)/cindercc
	@python3 tests/test_strength_reduction.py $(BUILD_DIR)/cindercc
	@python3 tests/test_copy_cleanup.py $(BUILD_DIR)/cindercc
	@python3 tests/test_local_memory.py $(BUILD_DIR)/cindercc
	@python3 tests/test_loop_motion.py $(BUILD_DIR)/cindercc

test-passes: build
	@python3 -B tests/test_pass_pipeline.py $(BUILD_DIR)/cindercc
	@python3 -B tests/test_promotion_guards.py $(BUILD_DIR)/cindercc
	@python3 -B tests/test_pass_evidence.py $(BUILD_DIR)/cindercc

test-pointer-words: build
	@python3 tests/test_initializers.py $(BUILD_DIR)/cindercc pointer_words
	@python3 -B tests/test_pointer_word_contracts.py $(BUILD_DIR)/cindercc

test-rewrites: build
	@python3 -B tools/run_rewrite_checks.py $(BUILD_DIR)/cindercc

test-memory: build
	@python3 tests/test_memory.py $(BUILD_DIR)/cindercc

test-literals: build
	@python3 tests/test_literals.py $(BUILD_DIR)/cindercc

test-linkage: build
	@python3 tests/test_linkage.py $(BUILD_DIR)/cindercc

test-static-addresses: build
	@python3 tests/test_static_addresses.py $(BUILD_DIR)/cindercc

test-census:
	@python3 tests/test_census.py

test-evidence:
	@python3 -B tests/test_evidence.py

test-allocation: build
	@tests/run_allocation.sh

test-parallel-copy: build
	@tests/run_parallel_copy.sh $(BUILD_DIR)/cindercc

test-float: build
	@tests/run_float.sh $(BUILD_DIR)/cindercc

test-varargs: build
	@tests/run_varargs.sh $(BUILD_DIR)/cindercc

test-apps: build
	@tests/run_apps.sh $(BUILD_DIR)/cindercc

test-generated: build
	@python3 tools/run_defined_cases.py $(BUILD_DIR)/cindercc --count 1200 --output .agent-local/generated-summary.json

test-differential: build
	@python3 tools/run_native_programs.py --compiler cinder=$(BUILD_DIR)/cindercc --count 20000 --profile "$${CINDER_EXECUTION_PROFILE:-native}" --output ".agent-local/differential/$$(date -u +%Y%m%dT%H%M%SZ)-$$$$"

test-native-linux: build
	@tests/run_native_linux.sh $(BUILD_DIR)/cindercc

test-abi: build
	@tests/run_abi.sh $(BUILD_DIR)/cindercc
	@python3 tests/test_abi_boundary.py $(BUILD_DIR)/cindercc

test-abi-callbacks: build
	@python3 tests/test_abi_callbacks.py $(BUILD_DIR)/cindercc --count 512

test-abi-aggregates: build
	@python3 tests/test_abi_aggregates.py $(BUILD_DIR)/cindercc --count 512

test-abi-variadic: build
	@python3 tests/test_abi_aggregates.py $(BUILD_DIR)/cindercc --variadic --count 512

test-object: build
	@tests/run_object.sh $(BUILD_DIR)/cindercc

test-object-campaign: build
	@python3 tests/test_objects.py $(BUILD_DIR)/cindercc --count 1000

test-output: build
	@python3 tests/run_output.py $(BUILD_DIR)/cindercc

test-debug: build
	@tests/run_debug.sh $(BUILD_DIR)/cindercc

fuzz: build
	@echo 'Fuzz harnesses are built from tests/fuzz and require a sanitizer profile.'
	@$(CMAKE) -S . -B $(BUILD_DIR)-asan -DCINDER_SANITIZE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
	@$(CMAKE) --build $(BUILD_DIR)-asan --parallel

selfhost: build
	@tests/selfhost.sh $(BUILD_DIR)/cindercc

benchmark: build
	@tests/benchmark.sh $(BUILD_DIR)/cindercc

demo: build
	@tests/demo.sh $(BUILD_DIR)/cindercc

acceptance: build
	@tests/acceptance.sh $(BUILD_DIR)/cindercc

verify:
	@tests/verify_evidence.sh $(BUILD_DIR)/cindercc

clean:
	@rm -rf $(BUILD_DIR) $(BUILD_DIR)-asan out
