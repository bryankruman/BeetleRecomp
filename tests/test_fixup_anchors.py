#!/usr/bin/env python3
"""Exercise fixup-anchor rebasing and rejection against a private, real input ELF."""
import argparse
import importlib.util
from pathlib import Path
import struct
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=Path('elf/recomp.elf'))
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('anchors', Path(__file__).resolve().parents[1]/'scripts/resolve-fixup-anchors.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original = args.elf.read_bytes()
    baseline = module.resolve(args.elf)
    h = struct.unpack_from('>16sHHIIIIIHHHHHH', original)
    sections = [struct.unpack_from('>10I', original, h[6]+i*40) for i in range(h[12])]
    strings = sections[h[13]]
    names = original[strings[4]:strings[4]+strings[5]]
    named = {names[s[0]:names.index(b'\0', s[0])].decode(): (i, s) for i,s in enumerate(sections)}
    with tempfile.TemporaryDirectory(prefix='bar-fixup-anchors-') as folder:
        path = Path(folder)/'fixture.elf'
        corrupted = bytearray(original)
        offset = named['.intro'][1][4]+0x618
        corrupted[offset:offset+4] = b'\0'*4
        path.write_bytes(corrupted)
        try:
            module.resolve(path)
        except ValueError as error:
            assert 'original instruction changed' in str(error)
        else:
            raise AssertionError('Changed target instruction was accepted')
        # The projection hook must reject an ABI-changing prologue, even when
        # a valid function name still exists at the original module offset.
        for target in (0x1F74, 0x1F78):
            corrupted = bytearray(original)
            offset = named['.uvfmtx_rom'][1][4]+target
            corrupted[offset:offset+4] = b'\0'*4
            path.write_bytes(corrupted)
            try:
                module.resolve(path)
            except ValueError as error:
                assert 'original instruction changed' in str(error)
            else:
                raise AssertionError('Changed projection prologue was accepted')
        # Rename only the ELF identifier, preserving all instructions and sizes.
        renamed = bytearray(original)
        address = named['.uvfmtx_rom'][1][3]+0x1F74
        hits = []
        for section in sections:
            if section[1] != 2:
                continue
            for offset in range(section[4], section[4]+section[5], 16):
                name, value, size, info, other, index = struct.unpack_from('>IIIBBH', original, offset)
                if value == address and info & 15 == 2:
                    start = sections[section[6]][4]+name
                    end = original.index(b'\0', start)
                    replacement = b'barProjection_'+b'X'*(end-start-len(b'barProjection_'))
                    assert len(replacement) == end-start
                    renamed[start:end] = replacement
                    hits.append(replacement.decode())
        assert len(hits) == 1
        path.write_bytes(renamed)
        renamed_anchors = module.resolve(path)
        assert renamed_anchors['FRUSTUM_PROJECTION'] == hits[0]
        assert {k:v for k,v in renamed_anchors.items() if k != 'FRUSTUM_PROJECTION'} == {k:v for k,v in baseline.items() if k != 'FRUSTUM_PROJECTION'}
        rebased = bytearray(original)
        selected = {named[n][0] for n in ('.intro','.uvcmidi_rom','.uvcont_rom','.uvfmtx_rom')}
        shift = 0x100000
        for index in selected:
            struct.pack_into('>I', rebased, h[6]+index*40+12, sections[index][3]+shift)
        for s in sections:
            if s[1] == 2:
                for offset in range(s[4],s[4]+s[5],16):
                    index = struct.unpack_from('>H', original, offset+14)[0]
                    value = struct.unpack_from('>I', original, offset+4)[0]
                    if index in selected:
                        struct.pack_into('>I', rebased, offset+4, value+shift)
        path.write_bytes(rebased)
        moved = module.resolve(path)
        for key in ('INTRO_SKIP','CONT_PROBE','CONT_STORE'):
            assert int(moved[key],16) == int(baseline[key],16)+shift
        assert int(moved['MIDI_WAIT_LABEL'][2:],16) == int(baseline['MIDI_WAIT_LABEL'][2:],16)+shift
        for key in ('CONT_SECTION','CONT_CLASSIFIER','CONT_REINIT','AUDIO_LENGTH_NAME','FRUSTUM_PROJECTION'):
            assert moved[key] == baseline[key]
    print('PASS: real ELF resolves; modified target instructions/projection prologue are rejected; renamed projection resolves; rebased modules preserve names/indexes and move all addresses.')


if __name__ == '__main__':
    main()
