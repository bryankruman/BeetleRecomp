#!/usr/bin/env python3
"""Apply reviewed game-specific changes without replacing pinned submodules."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def git(directory, *args):
    return subprocess.run(
        ["git", "-c", "core.excludesfile=", "-C", str(directory), *args],
        capture_output=True, text=True,
    )


def main():
    pending = []
    # Keep the original RT64 patch immutable. A separate addon can apply to
    # an already reviewed/dirty dependency without resetting or replaying it.
    for name, dependency in (("N64ModernRuntime", "N64ModernRuntime"),
                             ("rt64", "rt64"), ("N64Recomp", "N64Recomp"),
                             ("rt64-windows-dxil", "rt64")):
        directory = ROOT / "lib" / dependency
        patch = ROOT / "scripts" / "dependency-patches" / (name + ".patch")
        if git(directory, "apply", "--reverse", "--check", str(patch)).returncode == 0:
            print(f"{name}: reviewed patch already applied")
            continue
        result = git(directory, "apply", "--check", str(patch))
        if result.returncode:
            sys.exit(f"{name}: incompatible checkout; no patches applied.\n{result.stderr}")
        pending.append((name, directory, patch))
    for name, directory, patch in pending:
        result = git(directory, "apply", str(patch))
        if result.returncode:
            sys.exit(f"{name}: patch failed.\n{result.stderr}")
        print(f"{name}: applied reviewed patch")


if __name__ == "__main__":
    main()
