#!/usr/bin/env python3
"""Stage dependency and font notices with their paths, avoiding filename collisions."""
import argparse
import fnmatch
import os
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = ('LICENSE*', 'LICENCE*', 'COPYING*', 'COPYRIGHT*', 'NOTICE*', 'OFL*')
REQUIRED = (
    'lib/N64ModernRuntime/COPYING', 'lib/N64Recomp/LICENSE', 'lib/rt64/LICENSE',
    'lib/bar-decomp/LICENSE', 'lib/RecompFrontend/recompui/lib/RmlUi/LICENSE.txt',
    'lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/LICENSE.txt',
    'lib/RecompFrontend/recompui/lib/lunasvg/LICENSE',
    'lib/RecompFrontend/recompui/lib/freetype-windows-binaries/LICENSE.TXT',
    'lib/RecompFrontend/lib/GamepadMotionHelpers/LICENSE', 'assets/ui/promptfont/LICENSE.txt',
)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--sdl-license', type=Path)
    args = parser.parse_args()
    destination = args.destination.resolve()
    if destination == ROOT or not destination.is_relative_to(ROOT):
        parser.error('destination must be a subdirectory inside this repository')
    sdl_license = args.sdl_license
    if sdl_license is None and args.build_dir:
        sdl_license = args.build_dir / '_deps/sdl2-src/COPYING.txt'
    if sdl_license is None or not sdl_license.is_file():
        parser.error('supply the SDL license via --build-dir (Windows) or --sdl-license (Linux)')
    paths = set()
    for relative in REQUIRED:
        path = ROOT / relative
        if not path.is_file():
            parser.error('required license missing: ' + relative)
        paths.add(path)
    for tree in (ROOT / 'lib', ROOT / 'assets'):
        for directory, names, files in os.walk(tree):
            names[:] = [n for n in names if n not in ('.git', '.venv', '__pycache__', 'node_modules')
                        and not n.startswith('build') and not (Path(directory) / n).is_symlink()]
            for name in files:
                if any(fnmatch.fnmatchcase(name.upper(), pattern) for pattern in PATTERNS):
                    path = Path(directory) / name
                    if path.is_file() and not path.is_symlink():
                        paths.add(path)
    for path in sorted(paths):
        target = destination / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    shutil.copyfile(sdl_license, destination / 'SDL2-COPYING.txt')
    print(f'Staged {len(paths) + 1} dependency/font notices in {destination.relative_to(ROOT)}')

if __name__ == '__main__':
    main()
