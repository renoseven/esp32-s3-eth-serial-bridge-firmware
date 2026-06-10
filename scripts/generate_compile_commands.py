#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 RenoSeven

"""Generate compile_commands.json for clangd from arduino-cli build output.

Runs or reuses an arduino-cli compile, reads -MMD dependency files from the
build cache, filters project sources, and writes compile_commands.json at
the repository root.

CLI: generate_compile_commands.py [-m profile] [--no-compile]
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
from collections.abc import Iterator
from pathlib import Path
from typing import Any

# --- Paths ---

SCRIPT_PATH = Path(__file__).resolve()
SCRIPT_NAME = SCRIPT_PATH.name
SCRIPTS_DIR = SCRIPT_PATH.parent
REPO_DIR = SCRIPTS_DIR.parent


# --- Constants ---

DEFAULT_PROFILE = "release"
SOURCE_SUFFIXES = (".cpp", ".c", ".ino", ".o")
SKIP_FLAGS = frozenset(
    {
        "-MMD",
        "-c",
        "-Os",
        "-O0",
        "-O1",
        "-O2",
        "-O3",
        "-w",
        "-g",
        "-ggdb",
        "-gdwarf-4",
        "-Werror=return-type",
    }
)
SKIP_PREFIXES = (
    "-misa",
    "-mlongcalls",
    "-mdisable-hardware-atomics",
    "-fstrict-volatile-bitfields",
    "-fno-tree-switch-conversion",
    "-fno-jump-tables",
    "-ffunction-sections",
    "-fdata-sections",
    "-freorder-blocks",
    "-fstack-protector",
    "-fno-builtin-",
)
RAW_DB_CANDIDATES = ("compile_commands.json", "sketch/compile_commands.json")
BUILD_OPT_FILE = "build_opt.h"
CLANG_PREFIX = ("clang++", "-std=gnu++17", "-D__XTENSA__=1")


# --- arduino-cli ---


def arduino_build_path(sketch: Path, profile: str) -> Path | None:
    try:
        out = subprocess.check_output(
            [
                "arduino-cli",
                "compile",
                "--profile",
                profile,
                "--show-properties=expanded",
                str(sketch),
            ],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        print(f"Error: Failed to query build path: {exc}", file=sys.stderr)
        print(
            "  Check that arduino-cli is installed and the sketch profile is valid.",
            file=sys.stderr,
        )
        return None

    prefix = "build.path="
    for line in out.splitlines():
        if line.startswith(prefix):
            return Path(line[len(prefix) :])
    return None


def compile_sketch(sketch: Path, profile: str) -> None:
    subprocess.check_call(["arduino-cli", "compile", "--profile", profile, str(sketch)])


# --- compile_commands I/O ---


def load_compile_commands(path: Path) -> list[dict[str, Any]]:
    text = path.read_text(encoding="utf-8", errors="replace").strip()
    if text.startswith("#line"):
        text = text.split("\n", 1)[1].strip()
    if not text:
        return []
    return json.loads(text)


def load_build_database(build_path: Path) -> tuple[Path, list[dict[str, Any]]] | None:
    for rel in RAW_DB_CANDIDATES:
        candidate = build_path / rel
        if not candidate.is_file():
            continue
        try:
            entries = load_compile_commands(candidate)
        except json.JSONDecodeError:
            continue
        if entries:
            return candidate, entries
    return None


def ensure_build_database(
    workspace: Path, build_path: Path, profile: str, *, no_compile: bool
) -> tuple[Path, list[dict[str, Any]]] | None:
    database = load_build_database(build_path)
    if no_compile and database is not None:
        return database

    try:
        compile_sketch(workspace, profile)
    except subprocess.CalledProcessError:
        database = load_build_database(build_path)
        if database is None:
            print(
                "Error: Compile failed and no cached compile_commands.json was found.",
                file=sys.stderr,
            )
            return None
        print(
            "Warning: Compile failed; reusing cached compile_commands.json.",
            file=sys.stderr,
        )
        return database

    return load_build_database(build_path)


def write_compile_commands(workspace: Path, entries: list[dict[str, Any]]) -> Path:
    out_path = workspace / "compile_commands.json"
    out_path.write_text(json.dumps(entries, indent=1) + "\n", encoding="utf-8")
    return out_path


# --- build_opt.h ---


def parse_build_opt_flags(workspace: Path) -> list[str]:
    """Read compiler flags from build_opt.h (arduino-cli merges these at build time)."""
    path = workspace / BUILD_OPT_FILE
    if not path.is_file():
        return []

    flags: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("//"):
            continue
        for token in shlex.split(stripped):
            if not token.startswith("-"):
                continue
            if token.startswith("-I"):
                include_path = token[2:]
                if include_path and not include_path.startswith(("/", "-")):
                    token = f"-I{workspace / include_path}"
            flags.append(token)
    return flags


def merge_flags(base: list[str], *extras: list[str]) -> list[str]:
    seen = set(base)
    out = list(base)
    for extra in extras:
        for flag in extra:
            if flag not in seen:
                seen.add(flag)
                out.append(flag)
    return out


# --- GCC / Clang helpers ---


def is_cross_compiler(arg: str) -> bool:
    return "xtensa" in arg and arg.endswith(("g++", "gcc"))


def is_source_or_output(arg: str) -> bool:
    return arg.endswith(SOURCE_SUFFIXES)


def skip_flag(arg: str) -> bool:
    return arg in SKIP_FLAGS or any(arg.startswith(p) for p in SKIP_PREFIXES)


class GccArgParser:
    """Expand arduino-cli @response files and convert gcc flags for clangd."""

    def __init__(self) -> None:
        self._response_cache: dict[Path, tuple[str, ...]] = {}

    def read_response_file(self, path: Path) -> tuple[str, ...]:
        cached = self._response_cache.get(path)
        if cached is not None:
            return cached

        if not path.is_file():
            tokens: tuple[str, ...] = ()
        else:
            parsed: list[str] = []
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                line = line.strip()
                if line and not line.startswith("#"):
                    parsed.extend(shlex.split(line))
            tokens = tuple(parsed)

        self._response_cache[path] = tokens
        return tokens

    def expand_args(self, args: list[str]) -> list[str]:
        expanded: list[str] = []
        for arg in args:
            if arg.startswith("@"):
                expanded.extend(self.read_response_file(Path(arg[1:])))
            else:
                expanded.append(arg)
        return expanded

    def convert_iprefix_args(self, args: list[str]) -> list[str]:
        out: list[str] = []
        iprefix: str | None = None
        i = 0
        while i < len(args):
            arg = args[i]
            if arg == "-iprefix" and i + 1 < len(args):
                iprefix = args[i + 1].rstrip("/") + "/"
                i += 2
                continue
            if arg == "-iwithprefixbefore" and i + 1 < len(args) and iprefix:
                out.append("-I" + iprefix + args[i + 1])
                i += 2
                continue
            out.append(arg)
            i += 1
        return out

    def expanded_gcc_args(self, args: list[str]) -> list[str]:
        return self.convert_iprefix_args(self.expand_args(args))

    def iter_clang_flags(self, args: list[str]) -> Iterator[str]:
        skip_next = False
        for arg in self.expanded_gcc_args(args):
            if skip_next:
                skip_next = False
                continue
            if arg == "-o":
                skip_next = True
                continue
            if is_cross_compiler(arg) or is_source_or_output(arg):
                continue
            if skip_flag(arg):
                continue
            if arg.startswith("@") or arg in (
                "-iwithprefixbefore",
                "-iprefix",
                "-MMD",
                "-c",
            ):
                if arg == "-iprefix":
                    skip_next = True
                continue
            yield arg

    def extract_gcc_includes(self, compiler: str, args: list[str]) -> list[str]:
        probe = [compiler, "-E", "-Wp,-v", "-x", "c++"]
        for arg in self.expanded_gcc_args(args):
            if arg == "-o":
                break
            if (
                is_source_or_output(arg)
                or arg in ("-MMD", "-c")
                or is_cross_compiler(arg)
            ):
                continue
            probe.append(arg)
        probe.append("/dev/null")

        try:
            proc = subprocess.run(probe, capture_output=True, text=True, check=False)
        except OSError:
            return []

        includes: list[str] = []
        in_section = False
        for line in proc.stderr.splitlines():
            stripped = line.strip()
            if stripped == "#include <...> search starts here:":
                in_section = True
                continue
            if stripped == "End of search list.":
                break
            if in_section and line.startswith(" "):
                includes.append("-I" + stripped)
        return includes

    def build_clang_base(self, args: list[str]) -> list[str]:
        compiler = next((a for a in args if is_cross_compiler(a)), None)
        out = list(CLANG_PREFIX)
        seen = set(out)

        if compiler:
            for flag in self.extract_gcc_includes(compiler, args):
                if flag not in seen:
                    seen.add(flag)
                    out.append(flag)

        for arg in self.iter_clang_flags(args):
            if not arg.startswith(("-D", "-I")):
                continue
            if arg not in seen:
                seen.add(arg)
                out.append(arg)

        return out


# --- Sketch path mapping ---


def sketch_relative(path_str: str, sketch_dir: Path) -> Path | None:
    prefix = sketch_dir.as_posix() + "/"
    normalized = path_str.replace("\\", "/")
    if not normalized.startswith(prefix):
        return None
    return Path(normalized[len(prefix) :])


def map_ino_cpp(rel: Path, workspace: Path) -> str | None:
    if rel.suffix == ".cpp" and rel.name.endswith(".ino.cpp"):
        return str(workspace / rel.with_suffix(""))
    return None


def map_sketch_rel(rel: Path, workspace: Path) -> str | None:
    mapped = map_ino_cpp(rel, workspace)
    if mapped is not None:
        return mapped
    if rel.parts[:1] == ("src",):
        return str(workspace / rel)
    return None


def map_sketch_file(file_path: str, sketch_dir: Path, workspace: Path) -> str | None:
    rel = sketch_relative(file_path, sketch_dir)
    return map_sketch_rel(rel, workspace) if rel is not None else None


def rewrite_arg(arg: str, sketch_dir: Path, workspace: Path) -> str:
    rel = sketch_relative(arg, sketch_dir)
    if rel is None:
        return arg
    mapped = map_sketch_rel(rel, workspace)
    return mapped if mapped is not None else arg


def clang_source_args(source_path: str) -> list[str]:
    """Clang does not treat .ino as C++; force C++ mode for clangd."""
    if source_path.endswith(".ino"):
        return ["-x", "c++", source_path]
    return [source_path]


# --- Entry filtering ---


def filter_project_entries(
    entries: list[dict[str, Any]],
    sketch_dir: Path,
    workspace: Path,
    parser: GccArgParser,
) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    clang_base: list[str] | None = None
    build_opt_flags = parse_build_opt_flags(workspace)

    for entry in entries:
        mapped = map_sketch_file(entry.get("file", ""), sketch_dir, workspace)
        if mapped is None:
            continue

        raw_args = entry.get("arguments")
        if not isinstance(raw_args, list):
            continue

        rewritten = [rewrite_arg(a, sketch_dir, workspace) for a in raw_args]
        if clang_base is None:
            clang_base = merge_flags(
                parser.build_clang_base(rewritten), build_opt_flags
            )

        out.append(
            {
                "directory": str(workspace),
                "file": mapped,
                "arguments": [*clang_base, *clang_source_args(mapped)],
            }
        )
    return out


# --- Argparse ---


def create_parser() -> argparse.ArgumentParser:
    cmd = f"./{SCRIPT_NAME}"
    parser = argparse.ArgumentParser(
        description="Generate compile_commands.json for clangd.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            f"examples:\n  {cmd}\n  {cmd} -m debug\n  {cmd} --no-compile   # reuse existing build cache"
        ),
    )

    compile_group = parser.add_argument_group("compile")
    compile_group.add_argument(
        "-m",
        "--profile",
        default=DEFAULT_PROFILE,
        help=f"sketch profile in sketch.yaml (default: {DEFAULT_PROFILE})",
    )

    utility_group = parser.add_argument_group("utilities")
    utility_group.add_argument(
        "--no-compile",
        action="store_true",
        help="reuse build cache without compiling",
    )
    return parser


# --- Main ---


def main() -> int:
    args = create_parser().parse_args()
    workspace = REPO_DIR
    os.chdir(workspace)

    build_path = arduino_build_path(workspace, args.profile)
    if build_path is None:
        return 1

    database = ensure_build_database(
        workspace, build_path, args.profile, no_compile=args.no_compile
    )
    if database is None:
        print(
            f"Error: No usable compile_commands.json under {build_path}.",
            file=sys.stderr,
        )
        print(
            "  Compile the firmware first, or run without --no-compile.",
            file=sys.stderr,
        )
        return 1

    _, raw_entries = database
    entries = filter_project_entries(
        raw_entries, build_path / "sketch", workspace, GccArgParser()
    )
    if not entries:
        print(
            "Error: No project source files found in compile_commands.json.",
            file=sys.stderr,
        )
        return 1

    out_path = write_compile_commands(workspace, entries)
    print(f"Generated: {out_path} ({len(entries)} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
