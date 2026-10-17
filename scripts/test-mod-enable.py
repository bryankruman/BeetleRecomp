#!/usr/bin/env python3
"""Generate and run focused ModContext regressions using an existing compiler."""
from pathlib import Path
import argparse
import json
import os
import runpy
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import uuid


class WindowsJob:
    """Own a kill-on-close job before the helper starts any compiler children."""
    def __init__(self):
        import ctypes as c
        from ctypes import wintypes as w
        class Basic(c.Structure):
            _fields_ = [('process_time', c.c_int64), ('job_time', c.c_int64),
                ('flags', w.DWORD), ('minimum_working_set', c.c_size_t),
                ('maximum_working_set', c.c_size_t), ('active_process_limit', w.DWORD),
                ('affinity', c.c_size_t), ('priority_class', w.DWORD), ('scheduling_class', w.DWORD)]
        class Limits(c.Structure):
            _fields_ = [('basic', Basic), ('io_counters', c.c_uint64 * 6),
                ('process_memory', c.c_size_t), ('job_memory', c.c_size_t),
                ('peak_process_memory', c.c_size_t), ('peak_job_memory', c.c_size_t)]
        self.api = c.WinDLL('kernel32', use_last_error=True)
        self.api.CreateJobObjectW.argtypes = (c.c_void_p, w.LPCWSTR)
        self.api.CreateJobObjectW.restype = w.HANDLE
        self.api.SetInformationJobObject.argtypes = (w.HANDLE, c.c_int, c.c_void_p, w.DWORD)
        self.api.SetInformationJobObject.restype = w.BOOL
        self.api.CloseHandle.argtypes = (w.HANDLE,)
        self.api.CloseHandle.restype = w.BOOL
        self.name = 'Local\\mod-enable-regression-' + uuid.uuid4().hex
        c.set_last_error(0)
        self.handle = self.api.CreateJobObjectW(None, self.name)
        if not self.handle:
            raise OSError(c.get_last_error(), 'CreateJobObjectW failed')
        if c.get_last_error() == 183:  # ERROR_ALREADY_EXISTS
            self.close()
            raise RuntimeError('Refusing an existing job object')
        limits = Limits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.api.SetInformationJobObject(self.handle, 9, c.byref(limits), c.sizeof(limits)):
            error = c.get_last_error()
            self.close()
            raise OSError(error, 'SetInformationJobObject failed')

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


def windows_job_child(name, command):
    """Join the parent's job before spawning; failure cannot launch a compiler."""
    import ctypes as c
    from ctypes import wintypes as w
    api = c.WinDLL('kernel32', use_last_error=True)
    api.OpenJobObjectW.argtypes = (w.DWORD, w.BOOL, w.LPCWSTR)
    api.OpenJobObjectW.restype = w.HANDLE
    api.GetCurrentProcess.argtypes = ()
    api.GetCurrentProcess.restype = w.HANDLE
    api.AssignProcessToJobObject.argtypes = (w.HANDLE, w.HANDLE)
    api.AssignProcessToJobObject.restype = w.BOOL
    api.CloseHandle.argtypes = (w.HANDLE,)
    api.CloseHandle.restype = w.BOOL
    handle = api.OpenJobObjectW(1, False, name)  # JOB_OBJECT_ASSIGN_PROCESS
    if not handle:
        raise OSError(c.get_last_error(), 'OpenJobObjectW failed')
    try:
        if not api.AssignProcessToJobObject(handle, api.GetCurrentProcess()):
            raise OSError(c.get_last_error(), 'AssignProcessToJobObject failed')
    finally:
        api.CloseHandle(handle)  # Parent retains the only controlling job handle.
    return subprocess.call(command, stdin=subprocess.DEVNULL)


def group_exists(pid):
    try:
        os.killpg(pid, 0)
    except ProcessLookupError:
        return False
    return True


def stop_owned(process, job):
    if job is not None:
        job.close()  # Kills helper, compiler and inherited descendants together.
    else:
        try: os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError: pass
        try: process.wait(timeout=0.5)
        except subprocess.TimeoutExpired: pass
        try: os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError: pass
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()  # Popen's own process handle; never a searched/reused PID.
        process.wait(timeout=2)
    if job is None:
        deadline = time.monotonic() + 2
        while group_exists(process.pid):
            if time.monotonic() >= deadline:
                raise RuntimeError('Owned process group still exists after cleanup')
            time.sleep(0.01)


def run_bounded(command, workspace, label, timeout):
    stdout = workspace / (label + '.stdout')
    stderr = workspace / (label + '.stderr')
    job = WindowsJob() if os.name == 'nt' else None
    process = None
    try:
        launch = ([sys.executable, str(Path(__file__).resolve()), '--_job-child', job.name, '--', *command]
                  if job is not None else command)
        with stdout.open('wb') as out, stderr.open('wb') as err:
            process = subprocess.Popen(launch, cwd=workspace, stdin=subprocess.DEVNULL,
                stdout=out, stderr=err, start_new_session=(os.name != 'nt'))
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                stop_owned(process, job)
                raise RuntimeError(label + ' timed out; owned children were stopped') from None
        descendants = False
        if job is None and group_exists(process.pid):
            descendants = True
            stop_owned(process, None)
        if code != 0:
            # Keep diagnostics bounded; all generated/log files stay in the
            # temporary workspace and are removed after this report is printed.
            for log in (stdout, stderr):
                with log.open('rb') as stream:
                    stream.seek(0, 2)
                    stream.seek(max(0, stream.tell() - 65536))
                    sys.stderr.write(stream.read().decode('utf-8', errors='replace'))
            suffix = '; unexpected owned descendants were stopped' if descendants else ''
            raise RuntimeError(label + ' failed with return code ' + str(code) + suffix)
        if descendants:
            raise RuntimeError(label + ' exited with return code 0 but left owned descendants; group stopped')
        return code
    finally:
        if process is not None and process.poll() is None:
            stop_owned(process, job)
        elif process is not None and job is None and group_exists(process.pid):
            stop_owned(process, None)
        if job is not None:
            job.close()


def compile_command(cxx, source, executable, style):
    if style == 'msvc':
        return [cxx, '/nologo', '/std:c++20', '/EHsc', str(source),
                '/Fe' + str(executable), '/Fo' + str(source.with_suffix('.obj'))]
    return [cxx, '-std=c++20', '-pthread', str(source), '-o', str(executable)]


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo-root', type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument('--cxx', default=os.environ.get('CXX', 'c++'),
                   help='Existing compiler executable, without flags; no shell is used')
    p.add_argument('--compiler-style', choices=('auto', 'gnu', 'msvc'), default='auto')
    p.add_argument('--mods-source', type=Path, help='Optional detached production preview for regression checking')
    p.add_argument('--compile-timeout', type=float, default=60)
    p.add_argument('--test-timeout', type=float, default=10)
    args = p.parse_args(argv)
    for value in (args.compile_timeout, args.test_timeout):
        if not 0 < value <= 300:
            p.error('timeouts must be finite, positive and no greater than 300 seconds')
    root = args.repo_root.resolve()
    cxx = shutil.which(args.cxx)
    if cxx is None:
        p.error('existing compiler not found: ' + args.cxx)
    cxx = str(Path(cxx).resolve())
    style = args.compiler_style
    if style == 'auto':
        style = 'msvc' if Path(cxx).stem.lower() in ('cl', 'clang-cl') else 'gnu'
    generator = root / 'scripts/generate-mod-enable-regression.py'
    template = root / 'tests/mod_enable_regression.cpp.in'
    mods = args.mods_source.resolve() if args.mods_source else root / 'lib/N64ModernRuntime/librecomp/src/mods.cpp'
    header = root / 'lib/N64ModernRuntime/librecomp/include/librecomp/game.hpp'
    # Load the repository's reviewed generator, never the generated test itself.
    render = runpy.run_path(str(generator))['render']
    fixture = render(mods.read_bytes(), header.read_bytes(), template.read_bytes())
    with tempfile.TemporaryDirectory(prefix='mod-enable-regression-') as temporary:
        workspace = Path(temporary)
        source = workspace / 'mod_enable_regression.cpp'
        executable = workspace / ('mod_enable_regression.exe' if os.name == 'nt' else 'mod_enable_regression')
        source.write_bytes(fixture)
        run_bounded(compile_command(cxx, source, executable, style), workspace, 'compile', args.compile_timeout)
        run_bounded([str(executable)], workspace, 'test', args.test_timeout)
    print(json.dumps(dict(status='PASS_MOD_ENABLE_FOCUSED_REGRESSIONS', compiler=cxx,
        production_methods_generated=True, compiler_invocations=1,
        full_application_or_gameplay_qualification=False), sort_keys=True))
    return 0


if __name__ == '__main__':
    if len(sys.argv) > 3 and sys.argv[1] == '--_job-child':
        if os.name != 'nt' or sys.argv[3] != '--' or len(sys.argv) < 5:
            raise SystemExit('Invalid owned-child invocation')
        raise SystemExit(windows_job_child(sys.argv[2], sys.argv[4:]))
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, AssertionError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        raise SystemExit(1)
