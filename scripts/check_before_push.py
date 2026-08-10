"""Run what CI runs, here, before anything is pushed.

A failed workflow emails whoever watches the repository and interrupts them
with a mistake that never needed to leave the laptop. This script exists so
that the mistake is caught here instead.

That is only worth anything if the local check is *the same* check. So the
commands are not written out again below - they are read out of this
repository's own `.github/workflows`, which is the file the runner obeys. When
the workflow changes, this follows; when it does not, the two cannot drift.

    python scripts/check_before_push.py          # report
    python scripts/check_before_push.py --install-hook

`--install-hook` writes .git/hooks/pre-push, so `git push` refuses to send code
that CI would reject. Bypass a single push with `git push --no-verify` when the
failure is genuinely unrelated.

On Windows the honest answer is that CI cannot be reproduced: the workflow
builds on ubuntu-latest with GCC, and the `linux-default` preset refuses to
configure anywhere else. Rather than pretend, the script says so and offers the
one thing this machine can do - scripts/build_windows.ps1, which runs the same
four stages (dependencies, configure, build, ctest) through the MSVC presets.
That is a real check and it is reported as a different one, never as "CI would
pass".
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOWS = ROOT / ".github" / "workflows"

HOOK = """#!/bin/sh
# Installed by scripts/check_before_push.py.
# Runs the same checks CI runs, so a red pipeline is caught here instead of on
# a hosted runner. Bypass once with: git push --no-verify
exec "{python}" "{script}" --quiet
"""

# The four stages of the build. Anything else a workflow does - installing a
# compiler, warming a cache, adding an apt repository - is the runner's own
# housekeeping and has no meaning on a machine that already has those things.
STAGES = (
    "conan install",
    "cmake --preset",
    "cmake --build",
    "ctest --preset",
)

# A command may be spelled with a wrapper in front of it, as the Mandelbrot
# workflow wraps ctest in xvfb-run to give SFML a display. The wrapper is part
# of the command and is kept; it is only used to recognise the line.
WRAPPERS = ("xvfb-run",)


def workflow_files() -> list[Path]:
    if not WORKFLOWS.is_dir():
        return []
    return sorted(WORKFLOWS.glob("*.y*ml"))


def _is_stage(text: str) -> bool:
    for stage in STAGES:
        if text.startswith(stage):
            return True
        for wrapper in WRAPPERS:
            if text.startswith(wrapper) and stage in text:
                return True
    return False


def ci_commands() -> list[str]:
    """Every build, test and dependency command this repository's CI runs.

    Read line by line rather than through a YAML parser, because PyYAML is not
    something a C++ checkout can assume, and because the shell continuations
    inside a `run: |` block are what has to be reassembled anyway.
    """
    found: list[str] = []
    for workflow in workflow_files():
        lines = workflow.read_text(encoding="utf-8", errors="replace").splitlines()
        index = 0
        while index < len(lines):
            text = lines[index].strip()
            text = re.sub(r"^-?\s*run:\s*\|?-?\s*", "", text)
            if text.startswith("#") or not _is_stage(text):
                index += 1
                continue
            # Reassemble `foo \` / `  bar \` / `  baz` into one command.
            parts = [text]
            while parts[-1].endswith("\\") and index + 1 < len(lines):
                parts[-1] = parts[-1][:-1].strip()
                index += 1
                parts.append(lines[index].strip())
            command = " ".join(part for part in parts if part)
            if command not in found:
                found.append(command)
            index += 1
    return found


def ci_compiler() -> tuple[str, str] | None:
    """The CC/CXX the workflow sets, so the local run uses the same compiler.

    Configuring with whatever `cc` happens to be first on PATH would be a
    different check: these projects ask for language features that the runner's
    default GCC does not have.
    """
    cc = cxx = None
    for workflow in workflow_files():
        text = workflow.read_text(encoding="utf-8", errors="replace")
        if cc is None and (match := re.search(r"^\s*CC:\s*(\S+)\s*$", text, re.M)):
            cc = match.group(1)
        if cxx is None and (match := re.search(r"^\s*CXX:\s*(\S+)\s*$", text, re.M)):
            cxx = match.group(1)
    return (cc, cxx) if cc and cxx else None


def localise(command: str) -> str:
    """Adjust one CI command for this machine.

    ccache is a way of making the runner faster, not a thing being checked, so
    a machine without it drops the flag instead of failing the push on it.
    """
    if "ccache" in command and shutil.which("ccache") is None:
        command = command.replace("-D CMAKE_CXX_COMPILER_LAUNCHER=ccache", "").strip()
    for wrapper in WRAPPERS:
        if command.startswith(wrapper) and shutil.which(wrapper) is None:
            command = command[command.index("ctest") :]
    return command


def run(command: str, environment: dict[str, str], *, quiet: bool) -> bool:
    proc = subprocess.run(
        ["/bin/bash", "-c", command],
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,  # the return code is the result, not an error
    )
    if proc.returncode == 0:
        print(f"  OK   {command}")
        return True

    print(f"  FAIL {command}")
    tail = (proc.stdout + proc.stderr).strip().splitlines()
    for line in tail[-8:] if quiet else tail[-25:]:
        print(f"       {line}")
    return False


def check_on_linux(commands: list[str], *, quiet: bool) -> int:
    compiler = ci_compiler()
    environment = dict(os.environ)
    if compiler:
        cc, cxx = compiler
        if shutil.which(cxx) is None:
            print(f"NOT CHECKED: CI builds with {cxx}, which is not installed here.")
            print("             Nothing was verified. Install it, or let the runner")
            print("             be the first place this configuration is built.")
            return 0
        environment["CC"], environment["CXX"] = cc, cxx

    print("running the checks CI runs:")
    for command in commands:
        if not run(localise(command), environment, quiet=quiet):
            print("\nCI would fail on this. Fix it here rather than on a runner.")
            return 1
    print("\nall clear - safe to push")
    return 0


def check_on_windows(commands: list[str]) -> int:
    """Say plainly that CI was not reproduced, then run what can be run.

    The temptation is to report the MSVC build as a pass and let the reader
    assume CI is happy. It is not the same build - different compiler,
    different standard library, different preset - and a check that is easier
    than the one it mirrors is worse than no check, because it says "safe to
    push" and the runner then says otherwise.
    """
    print("CI builds on ubuntu-latest with GCC; the linux-default preset does not")
    print("configure on Windows, so these steps cannot be reproduced here:")
    for command in commands:
        print(f"  - {command}")

    script = ROOT / "scripts" / "build_windows.ps1"
    if not script.is_file():
        print("\nNOT CHECKED: nothing was verified on this machine.")
        return 0

    print("\nRunning the native MSVC build instead. Same four stages - dependencies,")
    print("configure, build, ctest - through the windows-msvc-release preset.")
    # The build writes straight to the console while this process' own output
    # sits in a buffer, so without a flush the explanation of what is about to
    # happen appears after the thing it explains.
    sys.stdout.flush()
    proc = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(script),
        ],
        cwd=ROOT,
        check=False,
    )
    if proc.returncode != 0:
        print("\nThe MSVC build failed. That is a real failure: fix it here.")
        return 1

    print("\nThe MSVC build and its tests passed. This is NOT a statement about CI:")
    print("the Linux job compiles the same sources with a different compiler and a")
    print("newer language standard, and it can still fail. Pushing is not blocked.")
    return 0


def install_hook() -> int:
    hooks = ROOT / ".git" / "hooks"
    if not hooks.is_dir():
        print("not a git repository (no .git/hooks)")
        return 1
    target = hooks / "pre-push"
    target.write_text(
        HOOK.format(
            python=sys.executable.replace("\\", "/"),
            script=str(Path(__file__).resolve()).replace("\\", "/"),
        ),
        encoding="utf-8",
        newline="\n",
    )
    target.chmod(0o755)
    print(f"installed {target}")
    print("`git push` will now run these checks first; --no-verify skips them.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--install-hook", action="store_true")
    parser.add_argument(
        "--quiet", action="store_true", help="shorter output, for hook use"
    )
    args = parser.parse_args()

    if args.install_hook:
        return install_hook()

    commands = ci_commands()
    if not commands:
        print("NOT CHECKED: CI here runs no build or test step this script can mirror.")
        print("             Nothing was verified. Pushing is not blocked, but nothing")
        print("             says the code is good either.")
        return 0

    if platform.system() == "Linux":
        return check_on_linux(commands, quiet=args.quiet)
    return check_on_windows(commands)


if __name__ == "__main__":
    raise SystemExit(main())
