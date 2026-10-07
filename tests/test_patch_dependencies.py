"""Verify that the real patch Makefile rebuilds after header and linker changes."""
from pathlib import Path
import os,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='patch-deps-') as scratch:
    folder=Path(scratch);shutil.copyfile(root/'patches/Makefile',folder/'Makefile')
    for name in ['sample.c','sample.h','sample.o','patches.elf','patches.ld','syms.ld']:(folder/name).write_text('')
    (folder/'sample.d').write_text('sample.o: sample.c sample.h\nsample.h:\n')
    for p in folder.iterdir():os.utime(p,(1000,1000))
    def commands():return subprocess.check_output(['make','-n','CC=clang-18','LD=ld.lld-18'],cwd=folder,text=True)
    assert 'clang-18' not in commands() and 'ld.lld-18' not in commands()
    os.utime(folder/'sample.h',(2000,2000));result=commands();assert '-MMD -MP -c' in result and 'ld.lld-18' in result,result
    os.utime(folder/'sample.h',(1000,1000))
    for name in ['syms.ld','patches.ld']:
        os.utime(folder/name,(2000,2000));result=commands();assert 'clang-18' not in result and 'ld.lld-18' in result,result;os.utime(folder/name,(1000,1000))
print('PASS: changed headers rebuild MIPS objects; either linker script relinks the patch ELF.')
