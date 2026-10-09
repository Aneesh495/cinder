cmake_minimum_required(VERSION 3.20)
project(cinder LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CINDER_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CINDER_SOURCES
  source/main.c
  source/arena.c
  source/buffer.c
  source/source.c
  source/diag.c
  source/output.c
  source/token.c
  source/lex.c
  source/literal.c
  source/numeric.c
  source/pp.c
  source/pp_tokens.c
  source/pp_expand.c
  source/pp_eval.c
  source/type.c
  source/ast.c
  source/parser.c
  source/const_eval.c
  source/init_shape.c
  source/static_address.c
  source/sema.c
  source/control.c
  source/ir.c
  source/ir_verify.c
  source/ir_text.c
  source/analysis.c
  source/ssa.c
  source/ir_interp.c
  source/interp_memory.c
  source/interp_variadic.c
  source/opt.c
  source/opt_cfg.c
  source/opt_sparse.c
  source/opt_values.c
  source/opt_strength.c
  source/opt_copy.c
  source/opt_memory.c
  source/opt_loop.c
  source/mir.c
  source/regalloc.c
  source/stack_layout.c
  source/liveness.c
  source/alloc_check.c
  source/x86_64.c
  source/abi.c
  source/elf64.c
  source/assembly.c
  source/driver.c
  source/inspect.c
)

function(cinder_target_profile target)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_compile_definitions(${target} PRIVATE CINDER_VERSION="0.1.0")
  target_compile_definitions(${target} PRIVATE CINDER_RUNTIME_INCLUDE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/include")
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror)
  if(CINDER_SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

set(CINDER_CORE_SOURCES ${CINDER_SOURCES})
list(REMOVE_ITEM CINDER_CORE_SOURCES source/main.c)
add_library(cinder_core STATIC ${CINDER_CORE_SOURCES})
cinder_target_profile(cinder_core)
add_executable(cindercc source/main.c)
cinder_target_profile(cindercc)
target_link_libraries(cindercc PRIVATE cinder_core)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME cinder_smoke COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_smoke.sh $<TARGET_FILE:cindercc>)
  set_tests_properties(cinder_smoke PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

add_executable(cinder_ssa_probe EXCLUDE_FROM_ALL tests/ssa_probe.c)
cinder_target_profile(cinder_ssa_probe)
target_link_libraries(cinder_ssa_probe PRIVATE cinder_core)

add_executable(cinderir source/ir_main.c)
cinder_target_profile(cinderir)
target_link_libraries(cinderir PRIVATE cinder_core)

add_executable(cinder_ir_campaign EXCLUDE_FROM_ALL tests/ir_campaign.c)
cinder_target_profile(cinder_ir_campaign)
target_link_libraries(cinder_ir_campaign PRIVATE cinder_core)
