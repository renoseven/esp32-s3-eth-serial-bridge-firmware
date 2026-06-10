#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 RenoSeven

"""Attach an Espressif USB serial/JTAG device from Windows into WSL.

Uses usbipd-win on the Windows host to share and attach the device, then
waits for /dev/ttyACM* or /dev/ttyUSB* to appear inside WSL.

CLI: attach_wsl_usb.py [--vid VID] [--pid PID] [--auto-attach 0|1]

Library:
    from attach_wsl_usb import DEFAULT_AUTO_ATTACH, DEFAULT_PID, DEFAULT_VID, attach_wsl_usb
    attach_wsl_usb(DEFAULT_VID, DEFAULT_PID, DEFAULT_AUTO_ATTACH)  # returns 0 on success
"""

from __future__ import annotations

import argparse
import glob
import shutil
import subprocess
import sys
import time
from pathlib import Path

# --- Paths ---

SCRIPT_PATH = Path(__file__).resolve()
SCRIPT_NAME = SCRIPT_PATH.name
SCRIPTS_DIR = SCRIPT_PATH.parent
PS1_PATH = SCRIPTS_DIR / "attach_wsl_usb.ps1"


# --- Constants ---

DEFAULT_VID = "303a"
DEFAULT_PID = "1001"
DEFAULT_AUTO_ATTACH = "1"
SERIAL_GLOBS = ("/dev/ttyACM*", "/dev/ttyUSB*")
WAIT_TIMEOUT_SEC = 15
WAIT_INTERVAL_SEC = 0.5
ATTACH_ATTEMPTS = 3
ATTACH_RETRY_DELAY_SEC = 2


# --- USB IDs ---


def normalize_usb_id(value: str) -> str:
    value = value.strip().lower()
    if value.startswith("0x"):
        value = value[2:]
    return value


def format_hardware_id(vid: str, pid: str) -> str:
    return f"{normalize_usb_id(vid)}:{normalize_usb_id(pid)}"


# --- Argparse ---


def create_parser() -> argparse.ArgumentParser:
    cmd = f"./{SCRIPT_NAME}"
    parser = argparse.ArgumentParser(
        description="Attach an Espressif USB device from Windows to WSL (usbipd).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "examples:\n"
            f"  {cmd}\n"
            f"  {cmd} --vid 303a --pid 1001 --auto-attach 1\n"
            "\n"
            "notes:\n"
            "  Run from WSL with the board connected and at least one WSL terminal open.\n"
            "  First-time setup requires one Administrator PowerShell bind:\n"
            "    usbipd bind --busid <BUSID>"
        ),
    )
    parser.add_argument(
        "--vid",
        metavar="VID",
        default=DEFAULT_VID,
        help=f"USB vendor ID (default: {DEFAULT_VID})",
    )
    parser.add_argument(
        "--pid",
        metavar="PID",
        default=DEFAULT_PID,
        help=f"USB product ID (default: {DEFAULT_PID})",
    )
    parser.add_argument(
        "--auto-attach",
        choices=("0", "1"),
        metavar="0|1",
        default=DEFAULT_AUTO_ATTACH,
        help=f"keep device attached in background (default: {DEFAULT_AUTO_ATTACH})",
    )
    return parser


# --- Windows / usbipd ---


def wslpath_windows(path: Path) -> str:
    return subprocess.check_output(["wslpath", "-w", str(path)], text=True).strip()


# --- Serial port ---


def find_serial_ports() -> list[Path]:
    ports: list[Path] = []
    for pattern in SERIAL_GLOBS:
        ports.extend(Path(path) for path in glob.glob(pattern))
    return sorted(ports)


def wait_for_serial_port(timeout_sec: int = WAIT_TIMEOUT_SEC) -> bool:
    attempts = int(timeout_sec / WAIT_INTERVAL_SEC)
    for _ in range(attempts):
        if find_serial_ports():
            return True
        time.sleep(WAIT_INTERVAL_SEC)
    return False


# --- Attach flow ---


def check_wsl_environment() -> int | None:
    if shutil.which("powershell.exe") is None:
        print("Error: This system is not WSL environment.", file=sys.stderr)
        return 1

    if not PS1_PATH.is_file():
        print("Error: Missing the Windows USB attach helper.", file=sys.stderr)
        return 1

    return None


def attach_on_windows(
    vid: str, pid: str, auto_attach: str, *, quiet: bool = False
) -> int:
    cmd = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        wslpath_windows(PS1_PATH),
        "-HardwareId",
        format_hardware_id(vid, pid),
        "-AutoAttach",
        auto_attach,
    ]
    if quiet:
        cmd.append("-Quiet")
    return subprocess.run(cmd, check=False).returncode


def wait_for_serial_or_warn(*, announce: bool) -> int:
    if announce:
        print("Waiting for serial port ...", flush=True)
    if wait_for_serial_port():
        return 0

    print(
        f"Warning: Device attached, but no serial port appeared within {WAIT_TIMEOUT_SEC}s.",
        file=sys.stderr,
    )
    print("  Try: dmesg | tail", file=sys.stderr)
    return 1


def attach_wsl_usb(vid: str, pid: str, auto_attach: str) -> int:
    if err := check_wsl_environment():
        return err

    hardware_id = format_hardware_id(vid, pid)

    for attempt in range(ATTACH_ATTEMPTS):
        label = attempt + 1
        if attempt > 0:
            time.sleep(ATTACH_RETRY_DELAY_SEC)

        print(
            f"Attaching USB device {hardware_id} ({label}/{ATTACH_ATTEMPTS}) ...",
            flush=True,
        )

        status = attach_on_windows(
            vid,
            pid,
            auto_attach,
            quiet=(attempt < ATTACH_ATTEMPTS - 1),
        )
        if status != 0:
            continue

        if wait_for_serial_or_warn(announce=(attempt == 0)) == 0:
            return 0

    return 1


def main() -> int:
    args = create_parser().parse_args()
    return attach_wsl_usb(args.vid, args.pid, args.auto_attach)


if __name__ == "__main__":
    raise SystemExit(main())
