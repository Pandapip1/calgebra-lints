<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# calgebra-lints

An annotation-driven static analysis toolkit for C, built as a set of Clang
Static Analyzer plugins.

A C project opts in by including `ownership.h` and writing contracts directly
in its own declarations -- which token a pointer carries, which call consumes
it, which value must never be a sentinel, which function is pure. The plugins
here then prove those contracts hold on every path, with Z3 discharging the
arithmetic side conditions. Nothing is inferred from naming conventions and
nothing is hardcoded per project: every rule is read back off the annotations
the source itself declares.

The checkers were originally developed inside
[ntlibc](https://github.com/Pandapip1/spicule), a from-scratch C library, and
this repository carries their full commit history from that tree.

## What it checks

| Plugin | Checkers |
| --- | --- |
| `calgebra-ownership-checker.so` | linear ownership of pointers and handles, capability tokens, owned-construct lifecycles, allocation lifetimes, memory extent/aliasing contracts, resource acquire/release, resource leaks |
| `calgebra-size-cast-checker.so` | narrowing integer casts, array index bounds, tagged results, integer sentinels, division by zero, shift counts, signed overflow, declared arithmetic ranges |
| `calgebra-totality-checker.so` | extracts call-graph and size-change facts for a termination proof |
| `calgebra-pointer-provenance-checker.so` | pointer provenance across casts and arithmetic |
| `calgebra-errno-discipline-checker.so` | `errno` set/read/clear discipline around fallible calls |
| `calgebra-lock-discipline-checker.so` | lock acquire/release pairing and held-lock preconditions |
| `calgebra-purity-checker.so` | declared-pure functions that observably are not, and functions that could be declared pure |
| `calgebra-signal-safety-checker.so` | async-signal-safety of code reachable from a handler |
| `calgebra-reentrancy-checker.so` | reentrancy hazards in shared state |
| `calgebra-initialization-checker.so` | reads of storage not proven initialized |
| `calgebra-fallible-result-checker.so` | discarded results of `fallible` functions |
| `calgebra-abi-zeroinit-checker.so` | ABI structs passed out without full zero initialization |
| `calgebra-loop-condition-checker.so` | loop headers that compound a bound with a data-dependent flag |
| `calgebra-declscan.so`, `calgebra-lint-declscan.so` | AST walks reporting what a file declares or defines |

## Building

Requirements: CMake 3.20+, a C++17 compiler, LLVM and Clang 18 development
packages (headers, CMake config, and `libclang-cpp`), and Z3 with a
discoverable `z3.pc`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

If LLVM and Clang are not on the default CMake search path, point at them:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(llvm-config-18 --prefix)"
```

`ctest` runs three algebra unit tests and one fixture suite per checker. A
fixture suite passes only when the set of lines the checker reports is exactly
the set of lines the fixtures marked with a `<suite>-expect` comment, so a
checker that has gone silent fails as loudly as one that has become noisy.

## Running a checker against a C project

The plugins are ordinary analyzer plugins, loaded per translation unit:

```sh
clang-18 --analyze \
  -Xclang -load -Xclang build/calgebra-ownership-checker.so \
  -Xclang -analyzer-checker=ntlibc.Ownership,ntlibc.ValidPointer,ntlibc.Resource \
  -Xclang -analyzer-output=text \
  -I path/to/calgebra/include \
  source.c -o /dev/null
```

Two of the plugins (`calgebra-totality-checker.so`,
`calgebra-signal-safety-checker.so`) are plain `PluginASTAction`s rather than
analyzer checkers, and are loaded under `-fsyntax-only` instead:

```sh
clang-18 -fsyntax-only \
  -Xclang -load -Xclang build/calgebra-signal-safety-checker.so \
  -Xclang -add-plugin -Xclang ntlibc-signal-safety \
  -DNTLIBC_OWNERSHIP_ANALYSIS source.c
```

`-DNTLIBC_OWNERSHIP_ANALYSIS` is needed only in that second form. Under
`--analyze`, Clang predefines `__clang_analyzer__` and `ownership.h` enables
its attributes on its own; a plain `-fsyntax-only` run gets neither, so the
annotations would otherwise expand to nothing.

The checker category strings are still spelled `ntlibc.*`. Renaming them is
tracked separately, so that an in-flight rename in the original tree does not
collide with this extraction.

## The annotation vocabulary

`include/ownership.h` is the toolkit's public contract, and the only header a
consuming project needs. Every macro in it expands to
`__attribute__((annotate("...")))` under analysis and to nothing otherwise, so
annotated code still compiles with any C compiler -- including ones that have
never heard of `annotate`.

The core of it:

- `tokdef NAME qualifiers...` declares a token: a named fact a value can
  carry. Qualifiers say how it behaves -- `l_strict` (linear: never
  duplicated), `l_permissive`, `l_unlimited`, `implicit_drop`,
  `dynamic_storage`, `extent_at_least`, `zero_vacuous`, `lock_held`, and
  others.
- `withtok(token)` on a parameter or return says the value carries that
  token. `elements_withtok(token, extent)` does the same for a counted range,
  deriving the byte extent from the carrier's element type.
- `consume(token)`, `consume_any(token)`, `grant(token)`, `drop(token)` say
  what a call does to the token its argument carries -- the linear-ownership
  verbs. `consume_if_nonnull_return(token)` covers the conditional case.
- `construct(handle)`, `destroy(handle)`, `handle(handle)` track an owned
  construct's lifecycle instead of a transferable token.
- `grants_thread_token(token)` / `requires_thread_token(token)` attach a fact
  to the analysis as a whole, for calls with no value to hang it on.
- `sentinel_exclude(value)`, `integer_sentinel(value)`,
  `long_sentinel(value)` name a reserved value that must be ruled out before
  the value is used in arithmetic, a cast, or an index.
- `fallible`, `async_signal_safe`, `io_operation`, `fields_established` are
  bare function-level facts the corresponding checkers read back instead of
  keeping hardcoded name lists.

`include/memory_tokens.h` builds on that with the ready-made storage
refinements most projects want: `readable_span`, `writable_span`,
`readable_elements`, `writable_elements`, `disjoint_span`.

Both headers carry the full reasoning for each entry inline; read them
directly rather than treating this list as the specification. `test/` has a
worked safe/unsafe pair for every checker.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
