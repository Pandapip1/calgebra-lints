# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(calgebra-lints LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LLVMConfigVersion.cmake only ever accepts its own exact version, so the
# floor is checked here rather than asked for in find_package.
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

if(LLVM_VERSION_MAJOR LESS 18)
  message(FATAL_ERROR
    "calgebra-lints needs LLVM/Clang 18 or newer; found ${LLVM_PACKAGE_VERSION}")
endif()

message(STATUS "calgebra-lints: LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
message(STATUS "calgebra-lints: Clang from ${Clang_DIR}")

# Z3 backs the arithmetic/ownership/termination proof obligations. It is
# discovered through z3.pc, the same way tools/lint.sh discovered it.
find_package(PkgConfig REQUIRED)
pkg_check_modules(Z3 REQUIRED IMPORTED_TARGET z3)

# LLVM and Clang are normally built without RTTI; plugins that link their
# libraries must match or the vtables will not resolve.
if(NOT LLVM_ENABLE_RTTI)
  add_compile_options(-fno-rtti)
endif()

# z3++.h is exception-based, so exceptions stay on for every target here.
# The ad-hoc clang++ invocations this build replaces had to re-enable them
# explicitly after llvm-config --cxxflags; CMake never turns them off.

# The plugins dlopen() into clang, so they must be position independent and
# may not resolve clang's own symbols at link time -- clang itself supplies
# them at load time. Linking libclang-cpp keeps the link honest anyway,
# which is what the hand-rolled build did too.
add_library(calgebra_common INTERFACE)
target_include_directories(calgebra_common SYSTEM INTERFACE
  ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(calgebra_common INTERFACE ${LLVM_DEFINITIONS})
target_link_libraries(calgebra_common INTERFACE clang-cpp LLVM)

# add_checker_plugin(<output-name> SOURCES ... [DEFINES ...] [Z3])
function(add_checker_plugin name)
  cmake_parse_arguments(ARG "Z3" "" "SOURCES;DEFINES" ${ARGN})
  add_library(${name} MODULE ${ARG_SOURCES})
  set_target_properties(${name} PROPERTIES
    PREFIX "" SUFFIX ".so" POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(${name} PRIVATE calgebra_common)
  if(ARG_DEFINES)
    target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
  endif()
  if(ARG_Z3)
    target_link_libraries(${name} PRIVATE PkgConfig::Z3)
  endif()
  install(TARGETS ${name} LIBRARY DESTINATION lib/calgebra)
endfunction()

# The integer/size/array/sentinel family all live in one translation unit
# and register as ntlibc.SizeCast, ntlibc.ArrayIndex, ntlibc.TaggedResult,
# ntlibc.IntegerSentinel, ntlibc.Divisor, ntlibc.ShiftCount,
# ntlibc.SignedArithmetic and ntlibc.ArithmeticContract.
add_checker_plugin(calgebra-size-cast-checker
  SOURCES src/SizeCastChecker.cpp
  DEFINES NTLIBC_ARITHMETIC_Z3
  Z3)

add_checker_plugin(calgebra-totality-checker
  SOURCES src/TotalityChecker.cpp
  Z3)

# One bundle, three translation units: the ownership, allocation-lifetime
# and memory-contract checkers share path state through OwnershipChecker's
# program-state traits, so they cannot be loaded as separate plugins. This
# single .so is what tools/lint.sh built twice (ownership, resourceleak)
# with identical flags.
add_checker_plugin(calgebra-ownership-checker
  SOURCES
    src/OwnershipChecker.cpp
    src/AllocationLifetimeChecker.cpp
    src/MemoryContractChecker.cpp
  DEFINES OWNERSHIP_CHECKER_BUNDLE NTLIBC_OWNERSHIP_Z3 NTLIBC_MEMORY_CONTRACT_Z3
  Z3)

add_checker_plugin(calgebra-initialization-checker
  SOURCES src/InitializationChecker.cpp)
add_checker_plugin(calgebra-fallible-result-checker
  SOURCES src/FallibleResultChecker.cpp)
add_checker_plugin(calgebra-pointer-provenance-checker
  SOURCES src/PointerProvenanceChecker.cpp)
add_checker_plugin(calgebra-lock-discipline-checker
  SOURCES src/LockDisciplineChecker.cpp)
add_checker_plugin(calgebra-abi-zeroinit-checker
  SOURCES src/AbiZeroInitChecker.cpp)
add_checker_plugin(calgebra-signal-safety-checker
  SOURCES src/SignalSafetyChecker.cpp)
add_checker_plugin(calgebra-errno-discipline-checker
  SOURCES src/ErrnoDisciplineChecker.cpp)
add_checker_plugin(calgebra-reentrancy-checker
  SOURCES src/ReentrancyChecker.cpp)
add_checker_plugin(calgebra-purity-checker
  SOURCES src/PurityChecker.cpp)
add_checker_plugin(calgebra-loop-condition-checker
  SOURCES src/LoopConditionChecker.cpp)

# Declaration scanners: plain -fsyntax-only AST walks that answer "what does
# this file declare/define", not analyzer checkers.
add_checker_plugin(calgebra-declscan SOURCES src/DeclScanner.cpp)
add_checker_plugin(calgebra-lint-declscan SOURCES src/LintDeclScanner.cpp)

install(FILES include/ownership.h include/memory_tokens.h
  DESTINATION include/calgebra)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
