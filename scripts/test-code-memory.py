#!/usr/bin/env python3
"""Exercise production code-mod protection helpers with mocked platform APIs.

No executable pages are modified. Native macOS code-mod loading remains a
separate qualification; these tests cover ranges, failure checks and cache flushes.
"""
import argparse
from pathlib import Path
import runpy
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMMON = r"""
#include <cstdint>
#include <cstddef>
#include <utility>
#include <string>
#include <stdexcept>
#include <cassert>
// Each fixture deliberately selects its own mocked platform, even on a Mac host.
#ifdef __APPLE__
#undef __APPLE__
#endif
static size_t page_size = 16384;
static uintptr_t seen_address;
static size_t seen_length;
static int seen_flags, result, flushes;
static void* flushed;
static int getpagesize() { return int(page_size); }
static void reset() { seen_address=seen_length=0; seen_flags=result=flushes=0; flushed=nullptr; }
static void flush(void* p, size_t n) { assert(n==16); flushed=p; ++flushes; }
"""
POSIX = r"""
static constexpr int PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4;
static int mprotect(void* p, size_t n, int f) { seen_address=uintptr_t(p); seen_length=n; seen_flags=f; return result; }
static void fake_clear_cache(char* a, char* b) { assert(b-a==16); flush(a,16); }
#define __builtin___clear_cache fake_clear_cache
"""
APPLE = r"""
#define __APPLE__ 1
static constexpr int VM_PROT_READ=1, VM_PROT_WRITE=2, VM_PROT_EXECUTE=4, VM_PROT_COPY=16, KERN_SUCCESS=0;
static int mach_task_self() { return 42; }
static int mach_vm_protect(int task, uintptr_t p, size_t n, bool maximum, int f) { assert(task==42 && !maximum); seen_address=p; seen_length=n; seen_flags=f; return result; }
static const char* mach_error_string(int) { return "mock protection failure"; }
static void sys_icache_invalidate(void* p,size_t n) { flush(p,n); }
"""
WINDOWS = r"""
using DWORD=unsigned long;
static constexpr DWORD PAGE_READWRITE=4;
static int VirtualProtect(void* p,size_t n,DWORD f,DWORD* old) { seen_address=uintptr_t(p); seen_length=n; seen_flags=int(f); *old=32; return !result; }
static int GetCurrentProcess() { return 42; }
static int FlushInstructionCache(int h,void* p,size_t n) { assert(h==42); flush(p,n); return !result; }
"""
CHECK = r"""
int main() {
    void* near_boundary=reinterpret_cast<void*>(uintptr_t(0x7ff8));
    uint64_t previous=0;
    reset(); unprotect(near_boundary,&previous);
#ifdef TEST_WINDOWS
    assert(seen_address==0x7ff8 && seen_length==16 && seen_flags==4 && previous==32);
#else
    assert(seen_address==0x4000 && seen_length==32768);
#ifdef __APPLE__
    assert(seen_flags==(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY));
#else
    assert(seen_flags==(PROT_READ|PROT_WRITE));
#endif
    assert(patch_page_range(reinterpret_cast<void*>(uintptr_t(0x7ff0))).second==16384);
    page_size=4096;
    assert(patch_page_range(reinterpret_cast<void*>(uintptr_t(0x1ff8))).second==8192);
    page_size=16384;
#endif
    assert(flushes==0);
    protect(near_boundary,previous);
    assert(flushes==1 && flushed==near_boundary);
#ifdef TEST_WINDOWS
    assert(seen_flags==32);
#else
    assert(seen_flags==5 && seen_address==0x4000 && seen_length==32768);
#endif
    reset(); result=-1;
    bool caught=false;
    try { unprotect(near_boundary,&previous); } catch(const std::runtime_error&) { caught=true; }
    assert(caught && flushes==0);
    caught=false;
    try { protect(near_boundary,previous); } catch(const std::runtime_error&) { caught=true; }
    assert(caught && flushes==0);
}
"""

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--cxx',default='c++')
    args=parser.parse_args()
    compiler=shutil.which(args.cxx)
    if not compiler: parser.error('Existing C++ compiler required')
    support=runpy.run_path(str(ROOT/'scripts/test-mod-enable.py'))
    source=(ROOT/'lib/N64ModernRuntime/librecomp/src/mods.cpp').read_text()
    first=source.index('void unprotect(void* target_func, uint64_t* old_flags) {')
    windows=source[first:source.index('\n#else\n#  include <unistd.h>',first)]
    first=source.index('std::pair<uintptr_t, size_t> patch_page_range')
    posix=source[first:source.index('\n#endif\n\nnamespace modpaths',first)]
    assert source.count('protect(target_func_u8, old_flags);')==1
    assert source.count('protect(target_func, old_flags);')==1
    style='msvc' if Path(compiler).stem.lower() in ('cl','clang-cl') else 'gnu'
    review=ROOT/'.review'; review.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='code-memory-',dir=review) as temp:
        workspace=Path(temp)
        for name,api,implementation in [('apple',APPLE,posix),('posix',POSIX,posix),('windows','#define TEST_WINDOWS 1\n'+WINDOWS,windows)]:
            path=workspace/(name+'.cpp'); exe=workspace/(name+'.exe')
            path.write_text(COMMON+api+implementation+CHECK)
            support['run_bounded'](support['compile_command'](compiler,path,exe,style),workspace,'compile-'+name,90)
            support['run_bounded']([str(exe)],workspace,'run-'+name,10)
            print('PASS: '+name+' production protection ranges, errors and instruction-cache invalidation')
    print('Native macOS execution and code signing are not exercised by this mock test.')

if __name__=='__main__': main()
