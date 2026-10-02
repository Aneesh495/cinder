BUILD_DIR ?= build
BUILD_TYPE ?= Debug
CMAKE ?= cmake

.PHONY: all bootstrap build test test-frontend test-globals test-multi test-preprocessor test-ir test-parallel-copy test-abi test-object test-debug fuzz selfhost benchmark demo acceptance verify clean

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

test-ir: build
	@tests/run_ir.sh $(BUILD_DIR)/cindercc

test-parallel-copy: build
	@tests/run_parallel_copy.sh $(BUILD_DIR)/cindercc

test-abi: build
	@tests/run_abi.sh $(BUILD_DIR)/cindercc

test-object: build
	@tests/run_object.sh $(BUILD_DIR)/cindercc

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

verify: build
	@python3 tools/source_census.py --root . --output .agent-local/source-census.json
	@tests/verify_evidence.sh $(BUILD_DIR)/cindercc

clean:
	@rm -rf $(BUILD_DIR) $(BUILD_DIR)-asan out
