#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run one checker plugin over one fixture directory and check its verdict.

Every fixture in this tree marks the lines where a diagnostic is expected
with a `<suite>-expect` comment.  A suite passes when the set of lines the
checker actually reports equals the set of lines the fixtures marked --
so a checker that has gone silent fails just as loudly as one that has
become noisy.

In smoke mode there are no line markers to compare: the fixture run only
has to load the plugin and finish without a crash.  That is the right gate
for a plugin whose output is a stream of proof facts rather than
diagnostics (the totality extractor), where the verdict is computed by a
separate prover.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


DIAGNOSTIC = re.compile(r"^(?P<path>.*?):(?P<line>\d+):\d+: warning: .*"
                        r"\[(?P<tag>[A-Za-z0-9_.]+)\]$")
CRASH = ("PLEASE submit a bug report", "clang frontend command failed",
         "Assertion", "Stack dump")


def analyze_command(args: argparse.Namespace, fixture: pathlib.Path) -> list[str]:
    command = [args.clang, "--analyze",
               "-Xclang", "-load", "-Xclang", args.plugin,
               "-Xclang", "-analyzer-checker=" + args.checkers,
               "-Xclang", "-analyzer-output=text"]
    if args.disable_checkers:
        command += ["-Xclang", "-analyzer-disable-checker=" + args.disable_checkers]
    return command + [f"-D{name}" for name in args.define] + [str(fixture), "-o", "/dev/null"]


def plugin_command(args: argparse.Namespace, fixture: pathlib.Path) -> list[str]:
    command = [args.clang, "-std=c99", "-fsyntax-only",
               "-Xclang", "-load", "-Xclang", args.plugin,
               "-Xclang", "-add-plugin", "-Xclang", args.plugin_action]
    return command + [f"-D{name}" for name in args.define] + [str(fixture)]


def run(args: argparse.Namespace) -> str:
    fixtures = sorted(args.directory.glob("*.c"))
    if not fixtures:
        raise SystemExit(f"{args.suite}: no fixtures in {args.directory}")
    output = ""
    for fixture in fixtures:
        build = plugin_command if args.plugin_action else analyze_command
        finished = subprocess.run(build(args, fixture), capture_output=True,
                                  text=True, cwd=args.directory.parent.parent)
        output += finished.stdout + finished.stderr
    for signature in CRASH:
        if signature in output:
            raise SystemExit(f"{args.suite}: the checker crashed on a fixture\n{output}")
    return output


def reported(output: str, tags: set[str]) -> set[tuple[str, int]]:
    result = set()
    for line in output.splitlines():
        match = DIAGNOSTIC.match(line)
        if match and match.group("tag") in tags:
            result.add((pathlib.Path(match.group("path")).name, int(match.group("line"))))
    return result


def marked(directory: pathlib.Path, marker: str, exclude: str | None) -> set[tuple[str, int]]:
    result = set()
    for fixture in sorted(directory.glob("*.c")):
        text = fixture.read_text(encoding="utf-8").splitlines()
        for number, line in enumerate(text, 1):
            if marker in line and not (exclude and exclude in line):
                result.add((fixture.name, number))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--plugin", required=True)
    parser.add_argument("--directory", required=True, type=pathlib.Path)
    parser.add_argument("--checkers", default="")
    parser.add_argument("--disable-checkers", default="")
    parser.add_argument("--plugin-action", default="")
    parser.add_argument("--tags", default="")
    parser.add_argument("--marker", default="")
    parser.add_argument("--exclude-marker", default="")
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--smoke", action="store_true")
    args = parser.parse_args()

    output = run(args)
    if args.smoke:
        print(f"{args.suite}: plugin loaded and analyzed every fixture")
        return 0

    expected = marked(args.directory, args.marker, args.exclude_marker or None)
    actual = reported(output, set(args.tags.split(",")))
    if expected != actual:
        print(f"{args.suite}: fixture mismatch", file=sys.stderr)
        print(f"  expected: {sorted(expected)}", file=sys.stderr)
        print(f"  actual:   {sorted(actual)}", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1
    print(f"{args.suite}: {len(expected)} expected diagnostic(s), all reported")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
