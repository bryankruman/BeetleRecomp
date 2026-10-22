"""Two-phase strict original-function identity binding. Never prints instruction data.

freeze: one trusted reference ELF plus original ROM -> public metadata-only policy.
resolve: target ELF plus policy -> private, ignored effective TOML; no ROM needed.
This tool runs read-only Git metadata checks; no compiler, recompiler, or network request.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import shutil
import subprocess
import struct
import sys
import tomllib

LIMIT = 64 * 1024 * 1024
IDENTIFIER = re.compile(r'[A-Za-z_][A-Za-z0-9_]*\Z')
DIGEST = re.compile(r'[0-9a-f]{64}\Z')
H = lambda raw: hashlib.sha256(raw).hexdigest()
E = lambda value: (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def no_links(path):
    for parent in (path, *path.parents):
        if parent.exists() or parent.is_symlink():
            info = parent.lstat()
            require(not stat.S_ISLNK(info.st_mode) and not
                    (getattr(info, 'st_file_attributes', 0) & 0x400),
                    'symlink/reparse path rejected')


def read_file(path, maximum=LIMIT):
    path = Path(path).absolute()
    no_links(path)
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0)
    fd = os.open(path, flags)
    try:
        before = os.fstat(fd)
        require(stat.S_ISREG(before.st_mode) and 0 < before.st_size <= maximum,
                'input type/size rejected')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            raw = stream.read(maximum + 1)
        after = os.fstat(fd)
        current = path.lstat()
        key = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns)
        require(key(before) == key(after) == key(current) and len(raw) == after.st_size,
                'input changed during read')
        no_links(path)
        return raw
    finally:
        os.close(fd)


def load_json(raw):
    def pairs(rows):
        result = {}
        for key, value in rows:
            require(key not in result, 'duplicate JSON key')
            result[key] = value
        return result
    return json.loads(raw, object_pairs_hook=pairs,
                      parse_constant=lambda _: require(False, 'nonfinite JSON'))


class Elf:
    """Bounded ELF32 big-endian MIPS metadata; no guessed function extents."""
    def __init__(self, raw):
        self.raw = raw
        require(52 <= len(raw) <= LIMIT, 'ELF size')
        h = struct.unpack_from('>16sHHIIIIIHHHHHH', raw)
        require(h[0][:7] == b'\x7fELF\x01\x02\x01' and h[1] == 2 and h[2] == 8
                and h[3] == 1 and h[8] == 52, 'ELF32-BE MIPS executable required')
        require(h[12] > 0 and h[11] == 40 and h[13] < h[12], 'ELF section table')
        require(h[10] > 0 and h[9] == 32, 'ELF load table required')
        self.span(h[6], h[12] * 40)
        self.span(h[5], h[10] * 32)
        sh = [struct.unpack_from('>10I', raw, h[6] + 40*i) for i in range(h[12])]
        strings = self.span(sh[h[13]][4], sh[h[13]][5])
        self.sections = []
        for index, s in enumerate(sh):
            name = self.string(strings, s[0])
            if s[1] != 8:
                self.span(s[4], s[5])
            require(s[3] + s[5] <= 2**32, 'section address overflow')
            self.sections.append(dict(index=index, name=name, type=s[1], flags=s[2],
                vma=s[3], offset=s[4], size=s[5], link=s[6], entsize=s[9]))
        self.loads = []
        for i in range(h[10]):
            p = struct.unpack_from('>8I', raw, h[5] + 32*i)
            if p[0] != 1:
                continue
            require(p[4] <= p[5] and p[2] + p[5] <= 2**32
                    and p[3] + p[4] <= 2**32, 'invalid load span')
            self.span(p[1], p[4])
            self.loads.append(dict(offset=p[1], vma=p[2], lma=p[3], size=p[4]))
        require(bool(self.loads), 'missing load segment')
        self.symbols = []
        for s in self.sections:
            if s['type'] != 2:
                continue
            require(s['entsize'] == 16 and s['size'] % 16 == 0
                    and s['link'] < len(self.sections), 'symbol table shape')
            linked = self.sections[s['link']]
            require(linked['type'] == 3, 'symbol string table type')
            names = self.span(linked['offset'], linked['size'])
            for offset in range(s['offset'], s['offset'] + s['size'], 16):
                name, value, size, info, other, index = struct.unpack_from('>IIIBBH', raw, offset)
                require(index != 0xffff, 'extended symbol section unsupported')
                self.symbols.append(dict(name=self.string(names, name), vma=value,
                    size=size, type=info & 15, bind=info >> 4, section_index=index))
        require(bool(self.symbols), 'missing complete static symbol table')

    def span(self, start, size):
        require(type(start) is int and type(size) is int and start >= 0 and size >= 0
                and start <= len(self.raw) and size <= len(self.raw)-start, 'ELF file bounds')
        return self.raw[start:start+size]

    @staticmethod
    def string(table, offset):
        require(offset < len(table), 'string offset bounds')
        end = table.find(b'\0', offset)
        require(end != -1, 'unterminated ELF string')
        return table[offset:end].decode('ascii')

    def positives(self, vma, section_name=None):
        found = []
        for symbol in self.symbols:
            index = symbol['section_index']
            if symbol['vma'] != vma or symbol['type'] != 2 or symbol['size'] == 0:
                continue
            require(0 < index < len(self.sections), 'positive FUNC has invalid section')
            section = self.sections[index]
            if section_name is not None and section['name'] != section_name:
                continue
            require(symbol['bind'] == 1 and section['flags'] & 2 and section['flags'] & 4
                    and section['type'] == 1, 'global allocated executable FUNC required')
            require(symbol['size'] <= 65536 and symbol['size'] % 4 == 0 and vma % 4 == 0,
                    'positive FUNC alignment/size')
            delta = vma - section['vma']
            require(0 <= delta and delta + symbol['size'] <= section['size'], 'FUNC section bounds')
            require(IDENTIFIER.fullmatch(symbol['name']), 'invalid FUNC identifier')
            found.append((symbol, section))
        return found

    def function_bytes(self, symbol, section):
        return self.span(section['offset'] + symbol['vma'] - section['vma'], symbol['size'])

    def original_rom_offset(self, symbol, section):
        offset = section['offset'] + symbol['vma'] - section['vma']
        size = symbol['size']
        loads = [p for p in self.loads if p['offset'] <= offset and offset+size <= p['offset']+p['size']
                 and p['vma'] <= symbol['vma'] and symbol['vma']+size <= p['vma']+p['size']
                 and offset-p['offset'] == symbol['vma']-p['vma']]
        require(len(loads) == 1, 'unique VMA/file/LMA load mapping required')
        p = loads[0]
        return p['lma'] + symbol['vma'] - p['vma']


def check_name_map(name_map):
    require(set(name_map) == {'schema','original_rom_sha256','functions'}
            and name_map['schema'] == 1 and DIGEST.fullmatch(name_map['original_rom_sha256']),
            'source name-map schema')
    rows = name_map['functions']
    require(type(rows) is list and len(rows) == 29, 'exactly 29 logical originals required')
    require(all(set(r) == {'name','vma'} and IDENTIFIER.fullmatch(r['name'])
                and type(r['vma']) is int and 0 <= r['vma'] < 2**32 and r['vma'] % 4 == 0
                for r in rows), 'logical name/VMA shape')
    require(len({r['name'] for r in rows}) == len({r['vma'] for r in rows}) == 29,
            'logical names and VMAs must be distinct')


def freeze_policy(reference_raw, rom, name_map, reference_sha256):
    check_name_map(name_map)
    require(DIGEST.fullmatch(reference_sha256) and H(reference_raw) == reference_sha256,
            'reference ELF whole-file hash')
    require(H(rom) == name_map['original_rom_sha256'], 'original ROM whole-file hash')
    elf = Elf(reference_raw)
    rows = []
    windows = []
    for source in name_map['functions']:
        candidates = elf.positives(source['vma'])
        require(len(candidates) == 1, 'unique real positive reference FUNC: '+source['name'])
        symbol, section = candidates[0]
        code = elf.function_bytes(symbol, section)
        offset = elf.original_rom_offset(symbol, section)
        require(offset + len(code) <= len(rom) and rom[offset:offset+len(code)] == code,
                'reference instructions differ from authenticated ROM: '+source['name'])
        require(all(symbol['vma']+symbol['size'] <= a or b <= symbol['vma'] for a,b in windows),
                'overlapping original function extents')
        windows.append((symbol['vma'],symbol['vma']+symbol['size']))
        rows.append(dict(name=source['name'],vma=symbol['vma'],size=symbol['size'],
                         section=section['name'],reference_name=symbol['name'],instruction_sha256=H(code)))
    return dict(schema=1,original_rom_sha256=H(rom),reference_elf_sha256=H(reference_raw),
                source_name_map_sha256=H(E(name_map)),functions=rows)


def check_policy(policy):
    require(set(policy) == {'schema','original_rom_sha256','reference_elf_sha256',
                           'source_name_map_sha256','functions'} and policy['schema'] == 1,
            'policy schema')
    for field in ('original_rom_sha256','reference_elf_sha256','source_name_map_sha256'):
        require(type(policy[field]) is str and DIGEST.fullmatch(policy[field]), 'policy hash field')
    rows = policy['functions']
    require(type(rows) is list and len(rows) == 29, 'policy requires 29 originals')
    seen = []
    for r in rows:
        require(set(r) == {'name','vma','size','section','reference_name','instruction_sha256'},
                'policy function shape')
        require(IDENTIFIER.fullmatch(r['name']) and IDENTIFIER.fullmatch(r['reference_name'])
                and type(r['section']) is str and r['section'].startswith('.')
                and type(r['vma']) is int and type(r['size']) is int
                and 0 < r['size'] <= 65536 and r['vma'] % 4 == r['size'] % 4 == 0
                and 0 <= r['vma'] < r['vma']+r['size'] <= 2**32
                and DIGEST.fullmatch(r['instruction_sha256']), 'policy identity fields')
        require(all(r['vma']+r['size'] <= a or b <= r['vma'] for a,b in seen), 'policy overlap')
        seen.append((r['vma'],r['vma']+r['size']))
    require(len({r['name'] for r in rows}) == len({r['vma'] for r in rows}) == 29,
            'policy identities not distinct')


def resolve_bindings(target_raw, policy):
    check_policy(policy)
    elf = Elf(target_raw)
    bindings = []
    used = set()
    for row in policy['functions']:
        candidates = elf.positives(row['vma'], row['section'])
        require(len(candidates) == 1, 'unique real positive target FUNC: '+row['name'])
        symbol, section = candidates[0]
        require(symbol['size'] == row['size'], 'frozen extent mismatch: '+row['name'])
        allowed = {row['name'],row['reference_name'],f"func_{row['vma']:08X}"}
        require(symbol['name'] in allowed, 'unreviewed target function spelling: '+row['name'])
        for named in elf.symbols:
            if named['name'] == row['name']:
                require(named['vma'] == row['vma'], 'conflicting canonical alias: '+row['name'])
        code = elf.function_bytes(symbol, section)
        require(H(code) == row['instruction_sha256'], 'frozen instruction hash mismatch: '+row['name'])
        identity = (section['index'],symbol['vma'])
        require(identity not in used, 'function identity reused')
        used.add(identity)
        bindings.append(dict(name=row['name'],elf_name=symbol['name'],section=row['section'],
                             vram=row['vma'],size=row['size'],instruction_bytes=code.hex()))
    require(len(used) == 29, 'incomplete original resolution')
    return bindings


def effective_toml(base_raw, base_path, target_path, policy, bindings):
    check_policy(policy)
    source = base_raw.decode('utf-8')
    base = tomllib.loads(source)
    names = [r['name'] for r in policy['functions']]
    require(base['patches']['original'] == names and len(names) == 29,
            'all 29 logical original requirements must remain literal')
    require('original_bindings' not in base['patches'], 'base already contains private bindings')
    settings = base['input']
    require(not settings.get('manual_funcs') and not settings.get('function_sizes')
            and not settings.get('use_mdebug',False) and not settings.get('use_absolute_symbols',False),
            'manual/symbol promotion modes rejected')
    require(set(settings) <= {'entrypoint','elf_path','output_func_path','relocatable_sections_path'},
            'unreviewed input settings')
    # The private TOML lives under elf/, so retain the original root-relative meanings.
    root = Path(base_path).absolute().parent
    for key in ('elf_path','output_func_path','relocatable_sections_path'):
        require(type(settings.get(key)) is str, 'required input path')
        value = str(Path(target_path).absolute()) if key == 'elf_path' else str((root/settings[key]).absolute())
        pattern = re.compile(r'(?m)^([ \t]*'+key+r'[ \t]*=[ \t]*)[^\n]+$')
        matches = list(pattern.finditer(source))
        require(len(matches) == 1, 'unique input path assignment required')
        source = pattern.sub(lambda m:m[1]+json.dumps(value),source,count=1)
    for binding in bindings:
        source += '\n[[patches.original_bindings]]\n'
        for key in ('name','elf_name','section','vram','size','instruction_bytes'):
            value = binding[key]
            source += key+' = '+(str(value) if type(value) is int else json.dumps(value))+'\n'
    parsed = tomllib.loads(source)
    require(parsed['patches']['original'] == base['patches']['original']
            and parsed['patches']['original_bindings'] == bindings, 'effective TOML readback')
    return source.encode()


def write_new(path, raw):
    path = Path(path).absolute()
    require(path.parent.is_dir(), 'output parent must already exist')
    no_links(path)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os,'O_BINARY',0) | getattr(os,'O_NOFOLLOW',0)
    fd = os.open(path,flags,0o600)
    try:
        with os.fdopen(fd,'wb',closefd=False) as stream:
            stream.write(raw);stream.flush();os.fsync(fd)
    finally:
        os.close(fd)
    require(read_file(path) == raw, 'output readback mismatch')


def private_output(root, output):
    root = Path(root).absolute();output = Path(output).absolute()
    require(output.parent == root/'elf' and output.name.endswith('.original-bound.toml'),
            'expected bytes may only be written to elf/*.original-bound.toml')
    no_links(root);no_links(output.parent)
    # Query the real index and Git's final ignore decision for this exact path.
    # A nested negation can override the root rule; ignored files can be tracked.
    executable = shutil.which('git')
    require(executable is not None, 'Git is required to establish private output status')
    executable = Path(executable).resolve(strict=True)
    no_links(executable)
    require(executable.is_file(), 'physical Git executable required')
    env = {k:v for k,v in os.environ.items() if not k.upper().startswith('GIT_')}
    env.update(GIT_OPTIONAL_LOCKS='0', GIT_TERMINAL_PROMPT='0',
               GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull)
    command = [str(executable), '-c', 'core.fsmonitor=false',
               '-c', 'core.untrackedCache=false', '-c', 'core.excludesFile='+os.devnull,
               '-C', str(root)]
    def query(arguments, data=None):
        try:
            result = subprocess.run(command+arguments, input=data, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, env=env, timeout=10, check=False)
        except (OSError, subprocess.TimeoutExpired):
            raise ValueError('private output Git metadata query failed') from None
        require(len(result.stdout) <= 65536 and len(result.stderr) <= 65536,
                'private output Git metadata response too large')
        return result
    top = query(['rev-parse', '--show-toplevel'])
    require(top.returncode == 0 and not top.stderr
            and Path(os.fsdecode(top.stdout.rstrip(b'\r\n'))).resolve(strict=True)
                == root.resolve(strict=True), 'exact Git repository root required')
    relative = output.relative_to(root).as_posix()
    require('\n' not in relative and '\r' not in relative, 'output path newline rejected')
    tracked = query(['ls-files', '--cached', '-z', '--', ':(icase,literal)'+relative])
    require(tracked.returncode == 0 and not tracked.stdout and not tracked.stderr,
            'private output path is tracked in Git index')
    ignored = query(['check-ignore', '--no-index', '--verbose', '--non-matching',
                     '-z', '--stdin'], os.fsencode(relative)+b'\0')
    fields = ignored.stdout.split(b'\0')
    require(ignored.returncode == 0 and not ignored.stderr and len(fields) == 5
            and fields[-1] == b'' and fields[0] == b'.gitignore'
            and fields[1].isdigit() and fields[2] == b'/elf/*.original-bound.toml'
            and fields[3] == os.fsencode(relative),
            'exact private output must be finally ignored by the root rule')
    no_links(output)
    return output


def write_private(root, output, raw):
    # This final query happens after all policy and ELF validation, just before O_EXCL.
    write_new(private_output(root, output), raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode',required=True)
    freeze = modes.add_parser('freeze')
    for name in ('reference-elf','reference-sha256','rom','name-map','output-policy'):
        freeze.add_argument('--'+name,required=True)
    resolve = modes.add_parser('resolve')
    for name in ('elf','policy','policy-sha256','base-config','output-toml'):
        resolve.add_argument('--'+name,required=True)
    args = parser.parse_args()
    if args.mode == 'freeze':
        policy = freeze_policy(read_file(args.reference_elf),read_file(args.rom),
            load_json(read_file(args.name_map)),args.reference_sha256)
        out = E(policy);write_new(args.output_policy,out)
        print(json.dumps(dict(status='FROZEN_METADATA_ONLY_ORIGINAL_POLICY',functions=29,
                              output=Path(args.output_policy).name,sha256=H(out)),sort_keys=True))
    else:
        raw = read_file(args.policy)
        require(DIGEST.fullmatch(args.policy_sha256) and H(raw) == args.policy_sha256,
                'Root-reviewed policy hash required')
        policy = load_json(raw);target = read_file(args.elf)
        bindings = resolve_bindings(target,policy)
        output = private_output(Path(args.base_config).absolute().parent,args.output_toml)
        config = effective_toml(read_file(args.base_config),args.base_config,args.elf,policy,bindings)
        write_private(Path(args.base_config).absolute().parent,output,config)
        print(json.dumps(dict(status='RESOLVED_29_PRIVATE_ORIGINAL_BINDINGS_NOT_RECOMPILED',
            functions=29,output=output.name,sha256=H(config),target_elf_sha256=H(target),
            policy_sha256=H(raw)),sort_keys=True))


if __name__ == '__main__':
    try:
        main()
    except (ValueError,OSError,KeyError,TypeError,struct.error,UnicodeError) as error:
        print('Strict original binding failed: '+type(error).__name__+': '+str(error),file=sys.stderr)
        sys.exit(1)
