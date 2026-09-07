# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later

# The algebra unit tests. Each is a plain main() that returns nonzero on the
# first failed assertion, so ctest needs no test framework to read them.

# TokenAlgebra is written against clang's AST, so its test links clang.
add_executable(token-algebra-test ../src/TokenAlgebraTest.cpp)
target_include_directories(token-algebra-test PRIVATE ../src)
target_link_libraries(token-algebra-test PRIVATE calgebra_common)
add_test(NAME token-algebra COMMAND token-algebra-test)

# LifecycleAlgebra is pure C++ with no LLVM dependency at all.
add_executable(lifecycle-algebra-test ../src/LifecycleAlgebraTest.cpp)
target_include_directories(lifecycle-algebra-test PRIVATE ../src)
target_compile_options(lifecycle-algebra-test PRIVATE -Wall -Wextra -Werror)
add_test(NAME lifecycle-algebra COMMAND lifecycle-algebra-test)

# ExactCScalarSMT drives Z3 directly, without clang.
add_executable(exact-c-scalar-smt-test ../src/ExactCScalarSMTTest.cpp)
target_include_directories(exact-c-scalar-smt-test PRIVATE ../src)
target_link_libraries(exact-c-scalar-smt-test PRIVATE PkgConfig::Z3)
add_test(NAME exact-c-scalar-smt COMMAND exact-c-scalar-smt-test)

# The fixture suites need a C driver (clang, to load the plugins) and an
# interpreter for the runner. Both are optional: without them the unit tests
# above still build and run.
find_package(Python3 COMPONENTS Interpreter)
find_program(CALGEBRA_CLANG NAMES clang-18 clang)

if(NOT Python3_Interpreter_FOUND OR NOT CALGEBRA_CLANG)
  message(STATUS "calgebra-lints: fixture suites disabled (need python3 and clang)")
  return()
endif()

set(RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/run-fixtures.py)

# add_fixture_suite(<dir> PLUGIN <target> [CHECKERS ...] [TAGS ...] ...)
function(add_fixture_suite suite)
  cmake_parse_arguments(ARG "SMOKE" "PLUGIN;MARKER;EXCLUDE_MARKER;ACTION;DIRECTORY"
    "CHECKERS;DISABLE;TAGS;DEFINES" ${ARGN})
  if(NOT ARG_DIRECTORY)
    set(ARG_DIRECTORY ${suite})
  endif()
  set(command ${Python3_EXECUTABLE} ${RUNNER}
    --suite ${suite}
    --clang ${CALGEBRA_CLANG}
    --plugin $<TARGET_FILE:${ARG_PLUGIN}>
    --directory ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_DIRECTORY})
  if(ARG_CHECKERS)
    list(JOIN ARG_CHECKERS "," checkers)
    list(APPEND command --checkers ${checkers})
  endif()
  if(ARG_DISABLE)
    list(JOIN ARG_DISABLE "," disable)
    list(APPEND command --disable-checkers ${disable})
  endif()
  if(ARG_TAGS)
    list(JOIN ARG_TAGS "," tags)
    list(APPEND command --tags ${tags})
  endif()
  foreach(define IN LISTS ARG_DEFINES)
    list(APPEND command --define ${define})
  endforeach()
  if(ARG_ACTION)
    list(APPEND command --plugin-action ${ARG_ACTION})
  endif()
  if(ARG_MARKER)
    list(APPEND command --marker ${ARG_MARKER})
  endif()
  if(ARG_EXCLUDE_MARKER)
    list(APPEND command --exclude-marker ${ARG_EXCLUDE_MARKER})
  endif()
  if(ARG_SMOKE)
    list(APPEND command --smoke)
  endif()
  add_test(NAME fixtures-${suite} COMMAND ${command})
endfunction()

add_fixture_suite(cast-range PLUGIN calgebra-size-cast-checker
  CHECKERS ntlibc.SizeCast TAGS ntlibc.SizeCast MARKER cast-range-expect)
add_fixture_suite(array-index PLUGIN calgebra-size-cast-checker
  CHECKERS ntlibc.ArrayIndex TAGS ntlibc.ArrayIndex MARKER array-index-expect)
add_fixture_suite(tagged-result PLUGIN calgebra-size-cast-checker
  CHECKERS ntlibc.TaggedResult TAGS ntlibc.TaggedResult MARKER tagged-result-expect)
add_fixture_suite(integer-sentinel PLUGIN calgebra-size-cast-checker
  CHECKERS ntlibc.IntegerSentinel TAGS ntlibc.IntegerSentinel
  MARKER integer-sentinel-expect)

# The analyzer's own core.DivideZero/core.BitwiseShift assume the same
# operation is well defined before the plugin's PreStmt callback runs, which
# would let these checkers "prove" their obligation from an assumption rather
# than from real path state. Only the overlapping built-ins are disabled.
add_fixture_suite(arithmetic-ub PLUGIN calgebra-size-cast-checker
  CHECKERS ntlibc.Divisor ntlibc.ShiftCount ntlibc.SignedArithmetic
           ntlibc.ArithmeticContract
  DISABLE core.DivideZero core.BitwiseShift
  TAGS ntlibc.Divisor ntlibc.ShiftCount ntlibc.SignedArithmetic
       ntlibc.ArithmeticContract
  DEFINES NTLIBC_ARITHMETIC_ANALYSIS MARKER arithmetic-ub-expect)

# `ownership-expect: resource-leak` marks the opt-in ResourceLeak checker's
# own expectations, which the always-on ntlibc.Resource does not report.
add_fixture_suite(ownership PLUGIN calgebra-ownership-checker
  CHECKERS ntlibc.Ownership ntlibc.OwnedConstruct ntlibc.OwnershipContract
           ntlibc.AggregateElementToken ntlibc.CapabilityToken
           ntlibc.OwnershipType ntlibc.ValidPointer ntlibc.Resource
  TAGS ntlibc.Ownership ntlibc.OwnedConstruct ntlibc.CapabilityToken
       ntlibc.OwnershipType ntlibc.ValidPointer ntlibc.Resource
  DEFINES NTLIBC_OWNERSHIP_ANALYSIS
  MARKER ownership-expect EXCLUDE_MARKER "ownership-expect: resource-leak")

# The opt-in leak checker reads the same corpus as the ownership suite; only
# the `resource-leak` tagged lines are its own.
add_fixture_suite(resourceleak DIRECTORY ownership PLUGIN calgebra-ownership-checker
  CHECKERS ntlibc.Resource ntlibc.ResourceLeak TAGS ntlibc.ResourceLeak
  DEFINES NTLIBC_OWNERSHIP_ANALYSIS MARKER "ownership-expect: resource-leak")

add_fixture_suite(allocation-lifetime PLUGIN calgebra-ownership-checker
  CHECKERS ntlibc.AllocationLifetime TAGS ntlibc.AllocationLifetime
  MARKER allocation-lifetime-expect)
add_fixture_suite(memory-contract PLUGIN calgebra-ownership-checker
  CHECKERS ntlibc.MemoryContract TAGS ntlibc.MemoryContract
  MARKER memory-contract-expect)
add_fixture_suite(initialization PLUGIN calgebra-initialization-checker
  CHECKERS ntlibc.InitializedRead TAGS ntlibc.InitializedRead
  MARKER initialization-expect)
add_fixture_suite(fallible-result PLUGIN calgebra-fallible-result-checker
  CHECKERS ntlibc.FallibleResult TAGS ntlibc.FallibleResult
  MARKER fallible-result-expect)
add_fixture_suite(pointer-provenance PLUGIN calgebra-pointer-provenance-checker
  CHECKERS ntlibc.PointerProvenance TAGS ntlibc.PointerProvenance
  MARKER pointer-provenance-expect)
add_fixture_suite(lock-discipline PLUGIN calgebra-lock-discipline-checker
  CHECKERS ntlibc.LockDiscipline TAGS ntlibc.LockDiscipline
  MARKER lock-discipline-expect)
add_fixture_suite(abi-zeroinit PLUGIN calgebra-abi-zeroinit-checker
  CHECKERS ntlibc.AbiZeroInit TAGS ntlibc.AbiZeroInit MARKER abi-zeroinit-expect)
add_fixture_suite(errno-discipline PLUGIN calgebra-errno-discipline-checker
  CHECKERS ntlibc.ErrnoDiscipline TAGS ntlibc.ErrnoDiscipline
  MARKER errno-discipline-expect)
add_fixture_suite(reentrancy PLUGIN calgebra-reentrancy-checker
  CHECKERS ntlibc.Reentrancy TAGS ntlibc.Reentrancy MARKER reentrancy-expect)
add_fixture_suite(purity PLUGIN calgebra-purity-checker
  CHECKERS ntlibc.Purity TAGS ntlibc.Purity MARKER purity-expect)
add_fixture_suite(loopcond PLUGIN calgebra-loop-condition-checker
  CHECKERS ntlibc.LoopCondition TAGS ntlibc.LoopCondition MARKER loopcond-expect)

# A plain PluginASTAction under -fsyntax-only, not an analyzer checker, so
# clang never predefines __clang_analyzer__ here and ownership.h's
# annotations need the define spelled out.
add_fixture_suite(signal-safety PLUGIN calgebra-signal-safety-checker
  ACTION ntlibc-signal-safety TAGS ntlibc.SignalSafety
  DEFINES NTLIBC_OWNERSHIP_ANALYSIS MARKER signal-safety-expect)

# The totality plugin emits proof inputs -- a call graph with size-change
# facts -- rather than diagnostics. Deciding termination from that stream is
# a separate prover, so this gate only checks the extractor runs clean.
add_fixture_suite(totality PLUGIN calgebra-totality-checker
  ACTION ntlibc-totality SMOKE)
