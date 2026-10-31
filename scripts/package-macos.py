#!/usr/bin/env python3
"""Package an existing native ARM64 app for personal testing; never build or publish."""
import argparse
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_SUFFIXES = {'.z64', '.n64', '.v64', '.rom', '.bin', '.elf', '.eep', '.sra', '.fla', '.pak', '.mpk', '.log'}
FORBIDDEN_NAMES = {'hud.json', 'graphics.json', 'controls.json', 'input.json', 'general.json', 'sound.json', 'portable.txt'}
REQUIRED_FONTS = ('LatoLatin-Regular.ttf', 'LatoLatin-Bold.ttf', 'LatoLatin-Italic.ttf', 'NotoEmoji-Regular.ttf')


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def inspect_assets(assets):
    for name in ('recomp.rcss', *REQUIRED_FONTS):
        if not (assets / name).is_file():
            raise ValueError(f'Missing frontend resource: {name}')
    for path in assets.rglob('*'):
        if path.is_symlink():
            raise ValueError(f'Asset symlinks are not admitted: {path}')
        if path.suffix.lower() in FORBIDDEN_SUFFIXES or path.name.lower() in FORBIDDEN_NAMES:
            raise ValueError(f'Private/user data in asset tree: {path}')


def parse_dependencies(output):
    # otool -L's first row names the input file; subsequent rows are load paths.
    result = []
    for line in output.splitlines()[1:]:
        line = line.strip()
        if not line:
            continue
        marker = ' (compatibility version '
        if marker not in line:
            raise ValueError(f'Unrecognized otool dependency row: {line}')
        result.append(line.split(marker, 1)[0])
    return result


def parse_rpaths(output):
    lines = output.splitlines()
    result = []
    for i, line in enumerate(lines):
        if line.strip() == 'cmd LC_RPATH':
            for row in lines[i+1:i+4]:
                match = re.fullmatch(r'\s*path (.+) \(offset \d+\)', row)
                if match:
                    result.append(match.group(1))
                    break
            else:
                raise ValueError('Malformed LC_RPATH load command')
    return result


def parse_version(value):
    if not re.fullmatch(r'[0-9]+(?:\.[0-9]+){1,2}', value):
        raise ValueError(f'Invalid macOS version: {value}')
    parts = tuple(int(part) for part in value.split('.'))
    return parts + (0,) * (3 - len(parts))


def validate_baseline(value):
    version = parse_version(value)
    if version < (13, 0, 0):
        raise ValueError('The Metal renderer baseline is macOS 13.0 or newer')
    return version


def parse_minimum_macos(output):
    # Read only the ARM64 slice. SDK version is not the deployment minimum.
    commands = re.split(r'(?m)^\s*cmd ', output)[1:]
    minimums = []
    for command in commands:
        rows = command.splitlines()
        name = rows[0].strip()
        if name not in ('LC_BUILD_VERSION', 'LC_VERSION_MIN_MACOSX'):
            continue
        if name == 'LC_BUILD_VERSION':
            platforms = re.findall(r'(?m)^\s*platform (\S+)\s*$', command)
            # otool versions print either the numeric constant or its name.
            if platforms not in (['1'], ['macos']):
                raise ValueError('Expected a macOS Mach-O slice, not iOS/Catalyst')
        field = 'minos' if name == 'LC_BUILD_VERSION' else 'version'
        versions = re.findall(r'(?m)^\s*' + field + r' (\S+)\s*$', command)
        if len(versions) != 1:
            raise ValueError('Malformed minimum macOS load command')
        minimums.append(parse_version(versions[0]))
    if not minimums:
        raise ValueError('Mach-O lacks a recognized minimum macOS version')
    return max(minimums)


def require_compatible_minimum(image, minimum, baseline):
    if minimum > baseline:
        version = '.'.join(map(str, minimum))
        target = '.'.join(map(str, baseline))
        raise ValueError(f'{image} requires macOS {version}, newer than selected {target}; '
                         'rebuild this dependency for the selected deployment target')


def resolve_dependency(dependency, image, app, rpaths):
    # Only Apple's system libraries may remain outside the package.
    if dependency.startswith(('/System/Library/', '/usr/lib/')):
        return None
    executable_dir = app / 'Contents/MacOS'

    def expand(value):
        if value.startswith('@loader_path/'):
            return image.parent / value[len('@loader_path/'):]
        if value.startswith('@executable_path/'):
            return executable_dir / value[len('@executable_path/'):]
        if value.startswith('/'):
            return Path(value)
        return None

    if dependency.startswith('@rpath/'):
        candidates = []
        for entry in rpaths:
            directory = expand(entry)
            if directory is not None:
                candidates.append(directory / dependency[len('@rpath/'):])
    else:
        value = expand(dependency)
        candidates = [] if value is None else [value]
    for candidate in candidates:
        resolved = candidate.resolve()
        if resolved.is_relative_to(app.resolve()) and resolved.is_file():
            return resolved
    raise ValueError(f'Nonportable or unresolved dependency in {image.name}: {dependency}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build-macos'))
    parser.add_argument('--version', required=True)
    parser.add_argument('--minimum-macos', default='13.0',
                        help='Oldest OS this package must run on; must match the build target (default: 13.0)')
    parser.add_argument('--sdl-license', type=Path, required=True)
    parser.add_argument('--dependency-licenses', type=Path, required=True,
                        help='Directory of license/notice texts for the actual external dylib closure')
    parser.add_argument('--out-dir', type=Path, default=Path('dist'))
    args = parser.parse_args()
    try:
        baseline = validate_baseline(args.minimum_macos)
    except ValueError as error:
        parser.error(str(error))
    if platform.system() != 'Darwin':
        parser.error('Run on macOS after a successful native build.')
    if not re.fullmatch(r'[0-9A-Za-z][0-9A-Za-z._-]*', args.version):
        parser.error('Version must contain only letters, numbers, dots, underscores and hyphens.')
    for tool in ('cmake', 'otool', 'lipo', 'file', 'codesign', 'ditto'):
        if not shutil.which(tool):
            parser.error(f'Missing tool: {tool}')
    source = (ROOT / args.build_dir / 'BeetleRecomp.app').resolve()
    executable = source / 'Contents/MacOS/BeetleRecomp'
    assets = source / 'Contents/Resources/assets'
    if not executable.is_file() or not args.sdl_license.is_file():
        parser.error('The built executable and explicit SDL license must exist.')
    if 'arm64' not in run('lipo', '-archs', executable).split():
        parser.error('The executable does not contain native ARM64 code.')
    inspect_assets(assets)
    if not args.dependency_licenses.is_dir():
        parser.error('Supply license texts for the actual SDL/Freetype and transitive dylib builds.')
    external_notices = []
    for notice in args.dependency_licenses.rglob('*'):
        if notice.is_symlink():
            parser.error('External license directory must not contain symlinks.')
        if notice.is_file():
            if notice.suffix.lower() not in ('', '.txt', '.md', '.rst') or notice.stat().st_size > 1024 * 1024:
                parser.error(f'Expected a small license/notice text: {notice}')
            try:
                text = notice.read_text(encoding='utf-8')
            except (UnicodeError, OSError):
                parser.error(f'Expected UTF-8 license/notice text: {notice}')
            if not text.strip() or '\x00' in text:
                parser.error(f'Expected nonempty license/notice text: {notice}')
            external_notices.append(notice)
    if not external_notices:
        parser.error('The external dependency license directory is empty.')
    plist = plistlib.loads((source / 'Contents/Info.plist').read_bytes())
    if plist.get('CFBundleExecutable') != 'BeetleRecomp':
        parser.error('Unexpected app executable in Info.plist.')
    try:
        declared = validate_baseline(plist.get('LSMinimumSystemVersion', ''))
    except ValueError as error:
        parser.error(f'App has no valid explicit deployment baseline: {error}')
    if declared != baseline:
        parser.error('App deployment target differs from --minimum-macos; rebuild for the selected baseline.')
    out = (ROOT / args.out_dir).resolve()
    if out == ROOT or not out.is_relative_to(ROOT):
        parser.error('Output must be a subdirectory of this repository.')
    name = f'BeetleRecomp-v{args.version}-macos-arm64'
    stage = out / name
    archive = out / (name + '.zip')
    if stage.exists() or archive.exists():
        parser.error('Output already exists; choose a new version. No files overwritten.')
    stage.mkdir(parents=True)
    app = stage / 'BeetleRecomp.app'
    (app / 'Contents/MacOS').mkdir(parents=True)
    # Whitelist the built executable, plist and reviewed UI assets. Do not copy
    # the source bundle wholesale: it may contain private ROM/config files.
    shutil.copy2(executable, app / 'Contents/MacOS/BeetleRecomp')
    shutil.copy2(source / 'Contents/Info.plist', app / 'Contents/Info.plist')
    shutil.copytree(assets, app / 'Contents/Resources/assets')
    icon = source / 'Contents/Resources/BeetleRecomp.icns'
    if icon.is_file():
        shutil.copy2(icon, app / 'Contents/Resources/BeetleRecomp.icns')
    for name in ('COPYING', 'THIRD_PARTY_NOTICES.md', 'README.md'):
        shutil.copy2(ROOT / name, stage / name)
    subprocess.run([sys.executable, str(ROOT / 'scripts/stage-licenses.py'), str(stage / 'licenses'),
                    '--sdl-license', str(args.sdl_license.resolve())], check=True)
    for notice in external_notices:
        target = stage / 'licenses/external-macos' / notice.relative_to(args.dependency_licenses)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(notice, target)
    subprocess.run(['cmake', f'-DBEETLE_APP={app}', '-P', str(ROOT / 'cmake/FixupMacOSBundle.cmake')], check=True)
    images = [p for p in app.rglob('*') if p.is_file() and not p.is_symlink()
              and 'Mach-O' in run('file', '-b', p)]
    executable_rpaths = parse_rpaths(run('otool', '-arch', 'arm64', '-l', app / 'Contents/MacOS/BeetleRecomp'))
    minimum_versions = []
    for image in images:
        if 'arm64' not in run('lipo', '-archs', image).split():
            raise ValueError(f'Non-ARM64 dependency: {image}')
        load_commands = run('otool', '-arch', 'arm64', '-l', image)
        image_rpaths = parse_rpaths(load_commands)
        minimum = parse_minimum_macos(load_commands)
        require_compatible_minimum(image, minimum, baseline)
        minimum_versions.append(minimum)
        # Expand executable rpaths with the executable as loader, not this dylib.
        inherited = [str((app / 'Contents/MacOS') / r[len('@loader_path/'):])
                     if r.startswith('@loader_path/') else r for r in executable_rpaths]
        for dependency in parse_dependencies(run('otool', '-arch', 'arm64', '-L', image)):
            resolve_dependency(dependency, image, app, image_rpaths + inherited)
    if not minimum_versions:
        raise ValueError('No Mach-O executable found after bundle relocation')
    plist['LSMinimumSystemVersion'] = '.'.join(str(value) for value in baseline)
    (app / 'Contents/Info.plist').write_bytes(plistlib.dumps(plist))
    # Relocation changes load commands, invalidating original signatures.
    # Sign nested code before the outer app; ad-hoc signing is for local testing.
    for image in sorted(images, key=lambda p: len(p.parts), reverse=True):
        subprocess.run(['codesign', '--force', '--sign', '-', str(image)], check=True)
    for framework in sorted(app.rglob('*.framework'), key=lambda p: len(p.parts), reverse=True):
        subprocess.run(['codesign', '--force', '--sign', '-', str(framework)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(stage), str(archive)], check=True)
    print(f'Packaged {archive}. Ad-hoc signed; not notarized, published or gameplay-qualified.')
    print('For public distribution, include the exact corresponding source and finish release qualification.')


if __name__ == '__main__':
    main()
