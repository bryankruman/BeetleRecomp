"""Verify reviewed dependency patches against the pinned, pristine Git sources."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
(ROOT / '.review').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='patch-check-', dir=ROOT / '.review') as scratch:
    for name, dependency in (('N64ModernRuntime', 'N64ModernRuntime'), ('rt64', 'rt64'), ('N64Recomp', 'N64Recomp'), ('rt64-windows-dxil', 'rt64')):
        patch = ROOT / 'scripts/dependency-patches' / (name + '.patch')
        fixture = Path(scratch) / name
        fixture.mkdir()
        for line in patch.read_text().splitlines():
            if not line.startswith('--- a/'):
                continue
            path = line[6:]
            original = subprocess.check_output(['git', '-C', str(ROOT / 'lib' / dependency), 'show', 'HEAD:' + path])
            target = fixture / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(original)
        command = ['git', '-c', 'core.excludesfile=', '-C', str(ROOT), 'apply',
                   '--directory=' + fixture.relative_to(ROOT).as_posix()]
        for flags in (['--check'], [], ['--reverse', '--check']):
            subprocess.run(command + flags + [str(patch)], check=True)
        print(name + ': pristine apply and reverse check passed')



def check_N64Recomp_migration_states():
    """Call the production helper on real Git patch fixtures, not a copied classifier."""
    import contextlib
    import importlib.util
    import io

    patch_root = ROOT / 'scripts/dependency-patches'
    combined = patch_root / 'N64Recomp.patch'
    legacy = patch_root / 'N64Recomp-legacy-originals.patch'
    migration = patch_root / 'N64Recomp-original-bindings-migration.patch'
    paths = [line[6:] for line in combined.read_text().splitlines() if line.startswith('--- a/')]
    assert paths and len(paths) == len(set(paths))
    pristine = {path: subprocess.check_output([
        'git', '-C', str(ROOT / 'lib/N64Recomp'), 'show', 'HEAD:' + path]) for path in paths}

    with tempfile.TemporaryDirectory(prefix='original-migration-', dir=ROOT / '.review') as scratch:
        scratch = Path(scratch)

        def materialize(directory):
            directory.mkdir(parents=True)
            subprocess.run(['git', 'init', '--quiet', str(directory)], check=True)
            for path, raw in pristine.items():
                target = directory / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(raw)

        expected = scratch / 'expected'
        materialize(expected)
        subprocess.run(['git', '-C', str(expected), 'apply', str(combined)], check=True)
        final = {path: (expected / path).read_bytes() for path in paths}

        for state in ('known-old', 'already-new', 'incompatible'):
            fixture_root = scratch / state
            directory = fixture_root / 'lib/N64Recomp'
            materialize(directory)
            copied_patches = fixture_root / 'scripts/dependency-patches'
            copied_patches.mkdir(parents=True)
            for patch in (combined, legacy, migration):
                (copied_patches / patch.name).write_bytes(patch.read_bytes())
            selected = combined if state == 'already-new' else legacy
            subprocess.run(['git', '-C', str(directory), 'apply', str(selected)], check=True)
            if state == 'incompatible':
                target = directory / 'src/main.cpp'
                raw = target.read_bytes()
                needle = b'for (const std::string& original : config.original_funcs)'
                assert raw.count(needle) == 1
                target.write_bytes(raw.replace(needle, needle.replace(b'original_funcs', b'unknown_funcs'), 1))
            before = {path: (directory / path).read_bytes() for path in paths}
            spec = importlib.util.spec_from_file_location(
                '_actual_dependency_patch_helper_' + state.replace('-', '_'),
                ROOT / 'scripts/apply-dependency-patches.py')
            helper = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(helper)
            helper.ROOT = fixture_root
            real_git = helper.git
            mutations = []
            mocked_other_checks = []

            def fixture_git(where, *args):
                if Path(where) == directory:
                    if '--check' not in args:
                        mutations.append(Path(args[-1]).name)
                    return real_git(where, *args)
                # Other dependencies are already reviewed/applied. Any attempt
                # to change them, or any different operation, fails the test.
                assert args[:3] == ('apply', '--reverse', '--check')
                assert Path(args[-1]).name in {
                    'N64ModernRuntime.patch', 'rt64.patch', 'rt64-windows-dxil.patch',
                    'rt64-macos-baseline.patch', 'rt64-sdl-filter-restore.patch',
                    'plume-metal-encoder-retain.patch', 'N64ModernRuntime-code-memory.patch',
                    'N64ModernRuntime-interrupt-mask.patch'}
                mocked_other_checks.append(Path(args[-1]).name)
                return subprocess.CompletedProcess(args, 0, '', '')

            helper.git = fixture_git
            rejected = False
            with contextlib.redirect_stdout(io.StringIO()):
                try:
                    helper.main()
                except SystemExit as error:
                    rejected = True
                    assert state == 'incompatible'
                    assert str(error).startswith('N64Recomp: incompatible checkout; no patches applied.')
            assert rejected == (state == 'incompatible')
            after = {path: (directory / path).read_bytes() for path in paths}
            if state == 'known-old':
                assert mutations == ['N64Recomp-original-bindings-migration.patch']
                assert after == final
            elif state == 'already-new':
                assert mutations == [] and after == before == final
            else:
                assert mutations == [] and after == before
            expected_other = {'N64ModernRuntime.patch', 'rt64.patch'}
            if not rejected:
                expected_other.update({
                    'rt64-windows-dxil.patch', 'rt64-macos-baseline.patch',
                    'rt64-sdl-filter-restore.patch', 'plume-metal-encoder-retain.patch',
                    'N64ModernRuntime-code-memory.patch', 'N64ModernRuntime-interrupt-mask.patch'})
            assert set(mocked_other_checks) == expected_other
            print('N64Recomp: production helper ' + state + ' regression passed')


check_N64Recomp_migration_states()
