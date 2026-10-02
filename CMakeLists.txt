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
  source/token.c
  source/lex.c
  source/pp.c
  source/type.c
  source/ast.c
  source/parser.c
  source/sema.c
  source/ir.c
  source/ir_verify.c
  source/ir_interp.c
  source/opt.c
  source/mir.c
  source/regalloc.c
  source/x86_64.c
  source/elf64.c
  source/driver.c
  source/inspect.c
)

add_executable(cindercc ${CINDER_SOURCES})
target_include_directories(cindercc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(cindercc PRIVATE CINDER_VERSION=\"0.1.0\")
target_compile_options(cindercc PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror=implicit-function-declaration)
if(CINDER_SANITIZE)
  target_compile_options(cindercc PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(cindercc PRIVATE -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
  add_test(NAME cinder_smoke COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_smoke.sh $<TARGET_FILE:cindercc>)
  set_tests_properties(cinder_smoke PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
