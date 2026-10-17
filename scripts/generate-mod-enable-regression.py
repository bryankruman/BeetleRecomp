"""Render a hermetic test from complete current production methods.

The template contains only in-memory state scaffolding and assertions. Production
methods/Version remain sourced from the dependency files on each test invocation.
No regex rewrite of their bodies, compiler invocation or application launch occurs.
"""
from pathlib import Path
import argparse
import hashlib

def block(text, start, end):
    assert text.count(start) == text.count(end) == 1
    return text[text.index(start):text.index(end)].rstrip() + '\n'

def render(mods_raw, game_raw, template_raw):
    mods = mods_raw.decode('utf-8')
    game = game_raw.decode('utf-8')
    methods = block(mods, 'void recomp::mods::ModContext::enable_mod(',
        'recomp::mods::DeprecationStatus recomp::mods::ModContext::get_mod_deprecation_status(')
    for signature in ('void recomp::mods::ModContext::enable_mod(',
        'bool recomp::mods::ModContext::is_mod_enabled(',
        'bool recomp::mods::ModContext::is_mod_auto_enabled(',
        'bool recomp::mods::ModContext::is_mod_deprecated('):
        assert methods.count(signature) == 1, signature
    assert '#include' not in methods
    version = block(game, '    struct Version {', '    enum class RomValidationError {')
    template = template_raw.decode('utf-8')
    assert template.count('@@METHODS@@') == template.count('@@VERSION@@') == 1
    result = template.replace('@@METHODS@@', methods).replace('@@VERSION@@', version).encode()
    assert methods.encode() in result and version.encode() in result
    return result

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--mods-source', type=Path, required=True)
    p.add_argument('--game-header', type=Path, required=True)
    p.add_argument('--template', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    raw = render(a.mods_source.read_bytes(), a.game_header.read_bytes(), a.template.read_bytes())
    # Build scripts supply a fresh regression workspace; never overwrite outputs.
    with a.output.open('xb') as stream:
        stream.write(raw)
    print(a.output, len(raw), hashlib.sha256(raw).hexdigest())

if __name__ == '__main__': main()
