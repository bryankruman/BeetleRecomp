"""Verify reviewed dependency patches against the pinned, pristine Git sources."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
(ROOT / '.review').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='patch-check-', dir=ROOT / '.review') as scratch:
    for name in ('N64ModernRuntime', 'rt64', 'N64Recomp'):
        patch = ROOT / 'scripts/dependency-patches' / (name + '.patch')
        fixture = Path(scratch) / name
        fixture.mkdir()
        for line in patch.read_text().splitlines():
            if not line.startswith('--- a/'):
                continue
            path = line[6:]
            original = subprocess.check_output(['git', '-C', str(ROOT / 'lib' / name), 'show', 'HEAD:' + path])
            target = fixture / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(original)
        command = ['git', '-c', 'core.excludesfile=', '-C', str(ROOT), 'apply',
                   '--directory=' + fixture.relative_to(ROOT).as_posix()]
        for flags in (['--check'], [], ['--reverse', '--check']):
            subprocess.run(command + flags + [str(patch)], check=True)
        print(name + ': pristine apply and reverse check passed')
