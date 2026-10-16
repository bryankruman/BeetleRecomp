#!/usr/bin/env python3
"""Export native Windows DXIL libraries; validate/import their portable provenance."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

LIBRARIES = {
    'RasterVSLibrary.hlsl.dxil': ('RasterVS.hlsl', []),
    'RasterPSLibrary.hlsl.dxil': ('RasterPS.hlsl', []),
    'RasterPSLibraryMS.hlsl.dxil': ('RasterPS.hlsl', ['-D', 'MULTISAMPLING']),
}
TOOLS = ('dxc.exe', 'dxcompiler.dll', 'dxil.dll')
CPU_INCLUDES = {
    ('shared/rt64_hlsl.h', 'stdint.h'),
    ('common/rt64_hlslpp.h', 'hlsl++.h'),
    ('shared/rt64_color_combiner.h', 'string'),
    ('shared/rt64_blender.h', 'string'),
}
H = lambda data: hashlib.sha256(data).hexdigest()
encode = lambda value: (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def regular(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), f'Not a regular file: {path}')
    require(not getattr(path, 'is_junction', lambda: False)(), f'Junction: {path}')
    before = path.stat()
    data = path.read_bytes()
    after = path.stat()
    require((before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns), f'File changed: {path}')
    return data


def pin(path):
    data = regular(path)
    return dict(sha256=H(data), bytes=len(data))


def source_inputs(source):
    """Guard both complete shader/shared folders and the quoted include closure."""
    source = Path(source).resolve()
    paths = set()
    for folder in ('shaders', 'shared'):
        for path in (source / folder).rglob('*'):
            require(not path.is_symlink() and not getattr(path, 'is_junction', lambda: False)(), f'Linked input: {path}')
            if path.is_file():
                paths.add(path)
    todo = [source / 'shaders' / name for name in ('RasterVS.hlsl', 'RasterPS.hlsl')]
    visited = set()
    unresolved = set()
    while todo:
        path = todo.pop().resolve()
        require(path.is_relative_to(source), 'Shader include escaped the source directory')
        if path in visited:
            continue
        visited.add(path)
        paths.add(path)
        text = regular(path).decode('utf-8-sig')
        require(not re.search(r'^\s*#\s*define\s+HLSL_CPU\b', text, re.M), 'HLSL_CPU is invalid for a shader library')
        for directive in re.findall(r'^\s*#\s*include\s+([^\n]+)', text, re.M):
            match = re.fullmatch(r'["<]([^">]+)[">]\s*', directive.strip())
            require(match is not None, f'Unsupported include: {directive}')
            include = match[1]
            found = next((candidate for candidate in (path.parent / include, source / include) if candidate.is_file()), None)
            if found is None:
                unresolved.add((path.relative_to(source).as_posix(), include))
            else:
                todo.append(found)
    require(unresolved == CPU_INCLUDES, 'Unresolved shader includes changed; review the dependency closure')
    require('#ifdef HLSL_CPU\n\n#include <stdint.h>' in (source / 'shared/rt64_hlsl.h').read_text(encoding='utf-8-sig'), 'CPU-only stdint guard changed')
    require('#ifdef HLSL_CPU\n#include <string>\n#endif' in (source / 'shared/rt64_color_combiner.h').read_text(encoding='utf-8-sig'), 'CPU-only combiner string guard changed')
    require('#ifdef HLSL_CPU\n#include <string>\nnamespace interop' in (source / 'shared/rt64_blender.h').read_text(encoding='utf-8-sig'), 'CPU-only blender string guard changed')
    return {path.relative_to(source).as_posix(): pin(path) for path in sorted(paths)}


def recipe():
    commands = [['{dxc}/dxc.exe', '--version']]
    for output, (source, defines) in LIBRARIES.items():
        commands.append(['{dxc}/dxc.exe', '-Wno-ignored-attributes', '-I{source}', '-D', 'LIBRARY', '-T', 'lib_6_3', *defines, '{source}/shaders/' + source, '/Fo', '{bank}/' + output])
    return commands


def dxbc(data):
    """Validate container bounds; absence of a VERS part is never a version test."""
    require(len(data) >= 32 and data[:4] == b'DXBC', 'Invalid DXIL container')
    total, count = struct.unpack_from('<II', data, 24)
    require(total == len(data) and 0 < count <= 64 and 32 + 4 * count <= total, 'Invalid DXIL extent')
    ranges = []
    tags = []
    for index in range(count):
        offset = struct.unpack_from('<I', data, 32 + 4 * index)[0]
        require(offset >= 32 + 4 * count and offset + 8 <= total, 'Invalid DXIL part offset')
        size = struct.unpack_from('<I', data, offset + 4)[0]
        require(offset + 8 + size <= total, 'Invalid DXIL part size')
        ranges.append((offset, offset + 8 + size))
        tags.append(data[offset:offset + 4])
    require(all(a[1] <= b[0] for a, b in zip(sorted(ranges), sorted(ranges)[1:])), 'Overlapping DXIL parts')
    require(tags.count(b'DXIL') == 1, 'DXIL program part must occur exactly once')


def bank_location(path, must_exist=True):
    # Check the spelling supplied by the caller before resolve can hide links.
    path = Path(path).absolute()
    require(all(not parent.is_symlink() and not getattr(parent, 'is_junction', lambda: False)() for parent in (path, *path.parents)), 'Shader bank path contains a symlink or junction')
    require(path.resolve() == path, 'Shader bank path must be canonical')
    require(path.is_dir() if must_exist else not path.exists() or path.is_dir(), 'Shader bank must be a directory')
    return path


def validate(source, bank):
    source, bank = Path(source).resolve(), bank_location(bank)
    manifest = json.loads(regular(bank / 'manifest.json'))
    require(manifest['schema'] == 1 and manifest['native_platform'] == 'win32', 'Bank must be exported on native Windows')
    require(manifest['exporter_sha256'] == H(regular(Path(__file__))), 'Exporter/validator recipe changed; regenerate the bank')
    require(manifest['commands'] == recipe(), 'Shader compile arguments changed')
    require(manifest['source_inputs'] == source_inputs(source), 'Shader/header inputs differ; regenerate the bank')
    tools = source / 'contrib/dxc/bin/x64'
    require(manifest['compiler_files'] == {name: pin(tools / name) for name in TOOLS}, 'Native compiler/runtime DLL pair differs')
    require(set(manifest['outputs']) == set(LIBRARIES), 'Bank must contain exactly the three linked libraries')
    require(len(manifest['executions']) == 4 and all(row['returncode'] == 0 for row in manifest['executions']), 'Incomplete native export')
    roots = manifest['execution_roots']
    require(set(roots) == {'dxc', 'source', 'bank'} and all(isinstance(value, str) and value for value in roots.values()), 'Missing native execution roots')
    for row, command in zip(manifest['executions'], recipe()):
        expected = [value.replace('{dxc}', roots['dxc']).replace('{source}', roots['source']).replace('{bank}', roots['bank']) for value in command]
        require(row['command'] == command and row['actual_argv'] == expected, 'Native export command association differs')
    require({path.name for path in bank.iterdir()} == {'manifest.json', *LIBRARIES}, 'Unexpected or missing bank files')
    for name, expected in manifest['outputs'].items():
        data = regular(bank / name)
        dxbc(data)
        require(dict(sha256=H(data), bytes=len(data)) == expected, f'Shader bank output differs: {name}')
    return manifest


def export(source, bank):
    require(sys.platform == 'win32', 'Export uses native Windows dxc.exe; validate/import can run on Linux')
    source, bank = Path(source).resolve(), bank_location(bank, must_exist=False)
    before = source_inputs(source)
    tools = source / 'contrib/dxc/bin/x64'
    compiler = {name: pin(tools / name) for name in TOOLS}
    bank.parent.mkdir(parents=True, exist_ok=True)
    # Leave a previous valid bank intact until all three compilations succeed.
    with tempfile.TemporaryDirectory(prefix='windows-dxil-', dir=bank.parent) as temporary:
        work = Path(temporary)
        executions = []
        env = dict(os.environ)
        env.pop('INCLUDE', None)
        for command in recipe():
            argv = [value.replace('{dxc}', str(tools)).replace('{source}', str(source)).replace('{bank}', str(work)) for value in command]
            result = subprocess.run(argv, cwd=source, env=env, stdin=subprocess.DEVNULL, capture_output=True, timeout=40, creationflags=subprocess.CREATE_NO_WINDOW)
            require(result.returncode == 0, 'Native DXC failed: ' + result.stderr.decode(errors='replace')[-2000:])
            executions.append(dict(command=command, actual_argv=argv, returncode=result.returncode, stdout_sha256=H(result.stdout), stderr_sha256=H(result.stderr)))
        require(source_inputs(source) == before and {name: pin(tools / name) for name in TOOLS} == compiler, 'Export inputs changed during compilation')
        manifest = dict(schema=1, native_platform='win32', execution_roots=dict(dxc=str(tools), source=str(source), bank=str(work)), exporter_sha256=H(regular(Path(__file__))), source_inputs=before, compiler_files=compiler, commands=recipe(), executions=executions, outputs={name: pin(work / name) for name in LIBRARIES})
        (work / 'manifest.json').write_bytes(encode(manifest))
        validate(source, work)
        bank.mkdir(exist_ok=True)
        require(not bank.is_symlink() and not getattr(bank, 'is_junction', lambda: False)(), 'Bank must not be a link')
        require({path.name for path in bank.iterdir()} <= {'manifest.json', *LIBRARIES}, 'Refusing to replace an unrelated directory')
        # Manifest is published last: interrupted replacement fails validation.
        for name in [*LIBRARIES, 'manifest.json']:
            os.replace(work / name, bank / name)
    validate(source, bank)


def import_library(source, bank, name, output):
    require(name in LIBRARIES, 'Unknown linked shader library')
    manifest = validate(source, bank)
    data = regular(Path(bank) / name)
    require(dict(sha256=H(data), bytes=len(data)) == manifest['outputs'][name], 'Bank changed while importing')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.is_file() and regular(output) == data:
        return
    require(not output.is_symlink(), 'Import destination is a symlink')
    with tempfile.NamedTemporaryFile(dir=output.parent, prefix=output.name + '.', delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


def verify_package(source, bank, executable, dll_dir, shader_dir):
    manifest = validate(source, bank)
    binary = regular(executable)
    for name in LIBRARIES:
        data = regular(Path(bank) / name)
        require(dict(sha256=H(data), bytes=len(data)) == manifest['outputs'][name], 'Bank changed during package validation')
        require(binary.count(data) == 1, f'Executable must contain exactly one authenticated {name}')
        require(regular(Path(shader_dir) / name) == data, f'Build shader differs from its bank: {name}')
    for name in ('dxcompiler.dll', 'dxil.dll'):
        require(pin(Path(dll_dir) / name) == manifest['compiler_files'][name], f'Packaged runtime differs: {name}')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('export', 'validate', 'import', 'package'))
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--bank', required=True, type=Path)
    parser.add_argument('--name')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--executable', type=Path)
    parser.add_argument('--dll-dir', type=Path)
    parser.add_argument('--shader-dir', type=Path)
    args = parser.parse_args()
    if args.action == 'export':
        export(args.source, args.bank)
    elif args.action == 'validate':
        validate(args.source, args.bank)
    elif args.action == 'import':
        require(args.name and args.output, 'Import needs --name and --output')
        import_library(args.source, args.bank, args.name, args.output)
    else:
        require(args.executable and args.dll_dir and args.shader_dir, 'Package needs executable, DLL and shader paths')
        verify_package(args.source, args.bank, args.executable, args.dll_dir, args.shader_dir)
    print('Windows DXIL bank: ' + args.action + ' passed')


if __name__ == '__main__':
    main()
