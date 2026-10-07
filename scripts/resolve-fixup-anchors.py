#!/usr/bin/env python3
"""Resolve original module offsets against this ELF; reject changed instructions."""
import argparse
from pathlib import Path
import re
import struct


def resolve(path):
    raw = path.read_bytes()
    header = struct.unpack_from('>16sHHIIIIIHHHHHH', raw)
    if header[0][:6] != b'\x7fELF\x01\x02' or header[2] != 8 or header[11] != 40:
        raise ValueError('Expected an ELF32 big-endian MIPS input')
    sections = [struct.unpack_from('>10I', raw, header[6]+n*40) for n in range(header[12])]
    def data(section):
        return raw[section[4]:section[4]+section[5]]
    def string(table, offset):
        return table[offset:table.index(b'\0', offset)].decode('ascii')
    names = data(sections[header[13]])
    named = {string(names, s[0]): (n, s) for n, s in enumerate(sections)}
    def instruction(section, offset, expected):
        _, s = named[section]
        if offset+4 > s[5] or struct.unpack_from('>I', raw, s[4]+offset)[0] != expected:
            raise ValueError(f'{section}+{offset:#x}: original instruction changed')
        return f'0x{s[3]+offset:08X}'
    functions = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = data(sections[section[6]])
        for offset in range(section[4], section[4]+section[5], 16):
            name, value, size, info, other, index = struct.unpack_from('>IIIBBH', raw, offset)
            if info & 15 == 2 and index != 0:
                functions.setdefault(value, set()).add(string(strings, name))
    def function(section, offset):
        address = named[section][1][3]+offset
        candidates = functions.get(address, set())
        if len(candidates) != 1:
            raise ValueError(f'Ambiguous/missing function at {address:#x}: {candidates}')
        name = next(iter(candidates))
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name):
            raise ValueError('Invalid function identifier')
        return name
    hardware = functions.get(0x8000E460, set())
    if hardware not in ({'func_8000E460'}, {'osAiGetLength'}):
        raise ValueError(f'Unexpected audio-length function: {hardware}')
    result = {
        'INTRO_SKIP': instruction('.intro', 0x618, 0x30B81000),
        'MIDI_WAIT_LABEL': 'L_'+instruction('.uvcmidi_rom', 0x984, 0x0C00048C)[2:],
        'CONT_PROBE': instruction('.uvcont_rom', 0x1794, 0x13200025),
        'CONT_STORE': instruction('.uvcont_rom', 0x1800, 0xA06A0016),
        'CONT_SECTION': str(named['.uvcont_rom'][0]),
        'CONT_CLASSIFIER': function('.uvcont_rom', 0x1760),
        'CONT_REINIT': function('.uvcont_rom', 0x1658),
        'AUDIO_LENGTH_NAME': next(iter(hardware)),
    }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    args = parser.parse_args()
    for name, value in resolve(args.elf).items():
        # Only validated identifiers, decimal indexes and hexadecimal addresses are emitted.
        if not re.fullmatch(r'[A-Za-z0-9_]+', value):
            raise ValueError('Unsafe shell value')
        print(f'{name}={value}')


if __name__ == '__main__':
    main()
