#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 RenoSeven

"""Compile and upload SerialBridge firmware with arduino-cli.

Injects FW_VERSION_ID from git into build_opt.h (restored on exit), embeds
web assets when inputs change, runs arduino-cli compile, and optionally
uploads the firmware to a serial port.

CLI: build.py [-m profile] [-u] [-p port] [--vid VID] [--pid PID] [--auto-attach 0|1]
      [--force-assets] [--clean] [--board-list]
"""

from __future__ import annotations

import argparse
import atexit
import glob
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from attach_wsl_usb import DEFAULT_AUTO_ATTACH, DEFAULT_PID, DEFAULT_VID, attach_wsl_usb
from embed_assets import embed_assets

# --- Paths ---

SCRIPT_PATH = Path(__file__).resolve()
SCRIPT_NAME = SCRIPT_PATH.name
SCRIPTS_DIR = SCRIPT_PATH.parent
REPO_DIR = SCRIPTS_DIR.parent


# --- Constants ---

DEFAULT_PROFILE = "release"
BUILD_OPT_LINES = ("-Iinclude", "-fno-exceptions")
BUILD_OPT_BASE = BUILD_OPT_LINES[0]
VERSION_OPT = "-DFW_VERSION_ID="
SERIAL_GLOBS = ("/dev/ttyACM*", "/dev/ttyUSB*")
SERIAL_PORT_RE = re.compile(r"^/dev/tty(ACM|USB)")


# --- Argparse ---


def create_parser() -> argparse.ArgumentParser:
    cmd = f"./{SCRIPT_NAME}"
    parser = argparse.ArgumentParser(
        description="Compile and optionally upload SerialBridge firmware.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "examples:\n"
            "  compile:\n"
            f"    {cmd}\n"
            f"    {cmd} -m debug\n"
            f"    {cmd} --force-assets\n"
            "\n"
            "  upload:\n"
            f"    {cmd} -u\n"
            f"    {cmd} -u -p /dev/ttyACM0\n"
            "\n"
            "  utilities:\n"
            f"    {cmd} --board-list\n"
            f"    {cmd} --clean"
        ),
    )

    compile_group = parser.add_argument_group("compile")
    compile_group.add_argument(
        "-m",
        "--profile",
        default=DEFAULT_PROFILE,
        help=f"sketch profile in sketch.yaml (default: {DEFAULT_PROFILE})",
    )
    compile_group.add_argument(
        "--force-assets",
        action="store_true",
        help="regenerate src/assets.cpp before compiling",
    )
    compile_group.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="show verbose arduino-cli output",
    )
    compile_group.add_argument(
        "--no-git-hash",
        action="store_true",
        help="do not inject FW_VERSION_ID into build_opt.h",
    )

    upload_group = parser.add_argument_group("upload")
    upload_group.add_argument(
        "-u",
        "--upload",
        action="store_true",
        help="upload firmware after compiling",
    )
    upload_group.add_argument(
        "-p",
        "--port",
        help="upload port (default: auto-detect, e.g. /dev/ttyACM0)",
    )
    upload_group.add_argument(
        "--vid",
        metavar="VID",
        default=DEFAULT_VID,
        help=f"USB vendor ID for WSL attach (default: {DEFAULT_VID})",
    )
    upload_group.add_argument(
        "--pid",
        metavar="PID",
        default=DEFAULT_PID,
        help=f"USB product ID for WSL attach (default: {DEFAULT_PID})",
    )
    upload_group.add_argument(
        "--auto-attach",
        choices=("0", "1"),
        metavar="0|1",
        default=DEFAULT_AUTO_ATTACH,
        help=f"keep USB attached in background on WSL (default: {DEFAULT_AUTO_ATTACH})",
    )

    utility_group = parser.add_argument_group("utilities")
    utility_group.add_argument(
        "--board-list",
        action="store_true",
        help="list connected boards and serial ports",
    )
    utility_group.add_argument(
        "--clean",
        action="store_true",
        help="remove arduino-cli build cache",
    )
    return parser


# --- Git / build_opt ---


def write_build_opt(path: Path, sha: str | None = None) -> None:
    """Inject FW_VERSION_ID as the last line of build_opt.h, or strip any existing one."""
    if path.is_file():
        lines = path.read_text(encoding="utf-8").splitlines()
    else:
        lines = list(BUILD_OPT_LINES)

    # Remove any existing version-id line(s), wherever they are.
    lines = [ln for ln in lines if not ln.strip().startswith(VERSION_OPT)]

    if not lines:
        lines = list(BUILD_OPT_LINES)

    if sha:
        lines.append(f'{VERSION_OPT}\\"{sha}\\"')

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def git_short_head(base: Path) -> str | None:
    try:
        return subprocess.check_output(
            ["git", "-C", str(base), "rev-parse", "--short", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except subprocess.CalledProcessError:
        return None


def is_git_repo(base: Path) -> bool:
    try:
        subprocess.run(
            ["git", "-C", str(base), "rev-parse", "--is-inside-work-tree"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return True
    except subprocess.CalledProcessError:
        return False


def setup_git_hash(build_opt_path: Path, *, inject: bool) -> str | None:
    """Inject FW_VERSION_ID into build_opt.h; restore on exit. Returns short SHA if injected."""
    state = {"wrote": False}

    def restore() -> None:
        if state["wrote"]:
            write_build_opt(build_opt_path)

    atexit.register(restore)

    if not inject or not is_git_repo(build_opt_path.parent):
        return None

    sha = git_short_head(build_opt_path.parent)
    if not sha:
        return None

    write_build_opt(build_opt_path, sha)
    state["wrote"] = True
    return sha


def read_define_str(path: Path, name: str) -> str | None:
    """Read a string-literal #define value (e.g. FW_VERSION_NAME) from a header."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"#define\s+" + re.escape(name) + r'\s+"([^"]*)"', text)
    return match.group(1) if match else None


def firmware_version(base: Path, sha: str | None) -> str | None:
    """Compose the full version string ("name (id)") matching firmware.h's FW_VERSION."""
    header = base / "include" / "firmware.h"
    name = read_define_str(header, "FW_VERSION_NAME")
    version_id = sha or read_define_str(header, "FW_VERSION_ID")
    if name and version_id:
        return f"{name} ({version_id})"
    return name or version_id


# --- Output ---


def display_profile(profile: str) -> str:
    return "-".join(part.capitalize() for part in profile.split("-"))


def print_build_context(
    *,
    profile: str,
    version: str | None,
) -> None:
    if version:
        print(f"Firmware Version: {version}", flush=True)
    else:
        print("Firmware Version: (not set)", flush=True)
    print(f"Profile: {display_profile(profile)}", flush=True)


# --- Serial / upload ---


def detect_serial_ports() -> list[str]:
    ports: list[str] = []
    for pattern in SERIAL_GLOBS:
        ports.extend(sorted(glob.glob(pattern)))
    if ports:
        return ports

    try:
        out = subprocess.check_output(
            ["arduino-cli", "board", "list"], text=True, stderr=subprocess.DEVNULL
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []

    return [
        parts[0]
        for line in out.splitlines()[1:]
        if (parts := line.split()) and SERIAL_PORT_RE.match(parts[0])
    ]


def detect_upload_port(vid: str, pid: str, auto_attach: str) -> tuple[str | None, int]:
    """Auto-detect upload port; on WSL, attach USB when none is found."""
    candidates = detect_serial_ports()

    if not candidates and os.environ.get("WSL_DISTRO_NAME"):
        print("No serial port found.")
        if (code := attach_wsl_usb(vid, pid, auto_attach)) != 0:
            return None, code
        candidates = detect_serial_ports()

    if not candidates:
        print("Error: No serial port found.", file=sys.stderr)
        return None, 1

    if len(candidates) > 1:
        print(
            f"Error: Found multiple serial ports: {' '.join(candidates)}",
            file=sys.stderr,
        )
        return None, 1

    return candidates[0], 0


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
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None

    prefix = "build.path="
    for line in out.splitlines():
        if line.startswith(prefix):
            return Path(line[len(prefix) :])
    return None


def show_board_list() -> int:
    return subprocess.run(["arduino-cli", "board", "list"], check=False).returncode


def clean_build_cache(base: Path, profile: str) -> int:
    build_path = arduino_build_path(base, profile)
    if build_path and build_path.is_dir():
        print(f"Removed build cache: {build_path}", flush=True)
        shutil.rmtree(build_path)
    else:
        print("No build cache to remove.", flush=True)
    return 0


def run_arduino_compile(
    base: Path,
    *,
    profile: str,
    verbose: bool,
) -> int:
    cmd = ["arduino-cli", "compile", "--profile", profile]
    if verbose:
        cmd.append("-v")
    cmd.append(str(base))

    if verbose:
        print(f"  Command: {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd, check=False).returncode


def run_arduino_upload(
    base: Path,
    *,
    profile: str,
    port: str,
    verbose: bool,
) -> int:
    cmd = ["arduino-cli", "upload", "--profile", profile, "-p", port]
    if verbose:
        cmd.append("-v")
    cmd.append(str(base))

    if verbose:
        print(f"  Command: {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd, check=False).returncode


# --- Build pipeline ---


def compile_firmware(
    base: Path,
    *,
    profile: str,
    upload: bool,
    port: str | None,
    vid: str,
    pid: str,
    auto_attach: str,
    verbose: bool,
    force_assets: bool,
    ignore_git_hash: bool,
) -> int:
    sha = setup_git_hash(base / "build_opt.h", inject=ignore_git_hash)
    version = firmware_version(base, sha)

    # 1. Print build context
    print_build_context(
        profile=profile,
        version=version,
    )
    print(flush=True)

    # 2. Embed assets
    print("Embedding assets...", flush=True)
    if (code := embed_assets(force=force_assets)) != 0:
        return code
    print(flush=True)

    # 3. Compile firmware
    print("Building firmware...", flush=True)
    if (code := run_arduino_compile(base, profile=profile, verbose=verbose)) != 0:
        return code
    print(flush=True)
    if not upload:
        print("Done.", flush=True)
        return 0

    # 4. Detect upload port
    print("Detecting upload port...", flush=True)
    upload_port = port
    if upload_port:
        print(f"Using {upload_port}.", flush=True)
    else:
        upload_port, code = detect_upload_port(vid, pid, auto_attach)
        if code != 0:
            return code
        print(f"Detected {upload_port}.", flush=True)
    print(flush=True)

    # 5. Upload firmware
    print("Uploading firmware...", flush=True)
    if (
        code := run_arduino_upload(
            base,
            profile=profile,
            port=upload_port,
            verbose=verbose,
        )
    ) != 0:
        return code
    print(flush=True)

    print("Done.", flush=True)
    return 0


# --- Main ---


def main() -> int:
    args = create_parser().parse_args()

    if shutil.which("arduino-cli") is None:
        print("Error: Could not find arduino-cli in PATH.", file=sys.stderr)
        print("  Install: https://arduino.github.io/arduino-cli/", file=sys.stderr)
        return 1

    base = REPO_DIR
    os.chdir(base)

    if args.board_list:
        return show_board_list()

    if args.clean:
        return clean_build_cache(base, args.profile)

    return compile_firmware(
        base,
        profile=args.profile,
        upload=args.upload,
        port=args.port,
        vid=args.vid,
        pid=args.pid,
        auto_attach=args.auto_attach,
        verbose=args.verbose,
        force_assets=args.force_assets,
        ignore_git_hash=not args.no_git_hash,
    )


if __name__ == "__main__":
    raise SystemExit(main())
