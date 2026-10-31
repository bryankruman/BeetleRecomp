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
                             ("rt64-windows-dxil", "rt64"),
                             ("rt64-macos-baseline", "rt64"),
                             ("rt64-sdl-filter-restore", "rt64"),
                             ("plume-metal-encoder-retain", "rt64/src/contrib/plume"),
                             ("N64ModernRuntime-code-memory", "N64ModernRuntime"),
                             ("N64ModernRuntime-interrupt-mask", "N64ModernRuntime")):
        directory = ROOT / "lib" / dependency
        patch = ROOT / "scripts" / "dependency-patches" / (name + ".patch")
        if git(directory, "apply", "--reverse", "--check", str(patch)).returncode == 0:
            print(f"{name}: reviewed patch already applied")
            continue
        result = git(directory, "apply", "--check", str(patch))
        selected_patch = patch
        if result.returncode and name == "N64Recomp":
            # A checkout may already contain the previous reviewed originals
            # patch. Never reset it or reverse-write it. Admit migration only
            # when the complete known legacy patch reverses and the strict
            # legacy-to-current incremental patch applies without fuzz.
            legacy = patch.with_name("N64Recomp-legacy-originals.patch")
            migration = patch.with_name("N64Recomp-original-bindings-migration.patch")
            if git(directory, "apply", "--reverse", "--check", str(legacy)).returncode == 0:
                result = git(directory, "apply", "--check", str(migration))
                selected_patch = migration
        if result.returncode:
            sys.exit(f"{name}: incompatible checkout; no patches applied.\n{result.stderr}")
        pending.append((name, directory, selected_patch))
    for name, directory, patch in pending:
        result = git(directory, "apply", str(patch))
        if result.returncode:
            sys.exit(f"{name}: patch failed.\n{result.stderr}")
        print(f"{name}: applied reviewed patch")


if __name__ == "__main__":
    main()
