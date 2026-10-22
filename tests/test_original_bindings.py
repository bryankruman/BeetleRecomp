"""Small synthetic ELF/ROM identity controls; no game files or C++ builds."""
import copy
import importlib.util
import json
import os
import shutil
import subprocess
from unittest import mock
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('strict_original_generator', Path(__file__).resolve().parents[1] / 'scripts' / 'generate-original-bindings.py')
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)


def fixture():
    raw = bytearray(8192)
    names = ['logical_'+str(i) for i in range(29)]
    strings = bytearray(b'\0')
    symbols = bytearray(16)
    rom = bytearray(8192)
    for i, name in enumerate(names):
        vma = 0x80000400 + i*8
        spelling = 'func_'+format(vma,'08X')
        name_offset = len(strings);strings.extend(spelling.encode()+b'\0')
        symbols.extend(struct.pack('>IIIBBH',name_offset,vma,8,0x12,0,1))
        code = struct.pack('>II',i+1,0x03e00008)
        raw[0x1000+i*8:0x1008+i*8] = code
        rom[0x1000+i*8:0x1008+i*8] = code
    shnames = b'\0.main\0.symtab\0.strtab\0.shstrtab\0'
    raw[0x200:0x200+len(shnames)] = shnames
    raw[0x500:0x500+len(symbols)] = symbols
    raw[0x800:0x800+len(strings)] = strings
    ident = b'\x7fELF\x01\x02\x01'+bytes(9)
    struct.pack_into('>16sHHIIIIIHHHHHH',raw,0,ident,2,8,1,0,52,0x300,0,52,32,1,40,5,4)
    struct.pack_into('>8I',raw,52,1,0x1000,0x80000400,0x1000,29*8,29*8,5,0x1000)
    sections = [(0,0,0,0,0,0,0,0,0,0),
                (1,1,6,0x80000400,0x1000,29*8,0,0,4,0),
                (7,2,0,0,0x500,len(symbols),3,1,4,16),
                (15,3,0,0,0x800,len(strings),0,0,1,0),
                (23,3,0,0,0x200,len(shnames),0,0,1,0)]
    for i,row in enumerate(sections):struct.pack_into('>10I',raw,0x300+40*i,*row)
    name_map = dict(schema=1,original_rom_sha256=g.H(rom),
                    functions=[dict(name=n,vma=0x80000400+i*8)for i,n in enumerate(names)])
    return bytes(raw),bytes(rom),name_map


class OriginalBindingTests(unittest.TestCase):
    def setUp(self):
        self.elf,self.rom,self.name_map = fixture()
        self.policy = g.freeze_policy(self.elf,self.rom,self.name_map,g.H(self.elf))

    def reject(self, function, *args):
        with self.assertRaises((ValueError,UnicodeError,struct.error,KeyError,TypeError)):
            function(*args)

    def test_positive_two_phase_and_no_reference_required_at_resolve(self):
        bindings = g.resolve_bindings(self.elf,self.policy)
        self.assertEqual(len(bindings),29)
        self.assertEqual([b['name']for b in bindings],[r['name']for r in self.name_map['functions']])
        self.assertTrue(all(len(b['instruction_bytes'])==16 for b in bindings))
        self.assertFalse(any('instruction_bytes'in r for r in self.policy['functions']))

    def test_reference_and_rom_whole_hashes(self):
        self.reject(g.freeze_policy,self.elf,self.rom,self.name_map,'0'*64)
        self.reject(g.freeze_policy,self.elf,self.rom[:-1]+b'1',self.name_map,g.H(self.elf))

    def test_verified_load_mapping_not_vma_subtraction_guess(self):
        raw=bytearray(self.elf);struct.pack_into('>I',raw,64,0x1004)
        self.reject(g.freeze_policy,bytes(raw),self.rom,self.name_map,g.H(raw))

    def test_authenticated_rom_wrong_window_rejects(self):
        rom=bytearray(self.rom);rom[0x1000]^=1;mapping=copy.deepcopy(self.name_map);mapping['original_rom_sha256']=g.H(rom)
        self.reject(g.freeze_policy,self.elf,bytes(rom),mapping,g.H(self.elf))

    def test_header_machine_byteorder_and_truncation(self):
        for offset,value in [(5,1),(19,3)]:
            raw=bytearray(self.elf);raw[offset]=value;self.reject(g.Elf,bytes(raw))
        self.reject(g.Elf,self.elf[:80])

    def test_zero_notyped_and_absolute_are_not_functions(self):
        for mode in ('zero','notype','absolute'):
            raw=bytearray(self.elf);at=0x510
            if mode=='zero':struct.pack_into('>I',raw,at+8,0)
            if mode=='notype':raw[at+12]=0x10
            if mode=='absolute':struct.pack_into('>H',raw,at+14,0xfff1)
            self.reject(g.resolve_bindings,bytes(raw),self.policy)

    def test_duplicate_positive_extent_rejects(self):
        raw=bytearray(self.elf);struct.pack_into('>I',raw,0x520+4,0x80000400)
        self.reject(g.resolve_bindings,bytes(raw),self.policy)

    def test_nonexecutable_and_out_of_bounds_reject(self):
        raw=bytearray(self.elf);struct.pack_into('>I',raw,0x300+40+8,2)
        self.reject(g.resolve_bindings,bytes(raw),self.policy)
        raw=bytearray(self.elf);struct.pack_into('>I',raw,0x510+8,0x10000)
        self.reject(g.resolve_bindings,bytes(raw),self.policy)

    def test_frozen_extent_and_instruction_changes_reject(self):
        raw=bytearray(self.elf);struct.pack_into('>I',raw,0x510+8,4)
        self.reject(g.resolve_bindings,bytes(raw),self.policy)
        raw=bytearray(self.elf);raw[0x1000]^=1
        self.reject(g.resolve_bindings,bytes(raw),self.policy)

    def test_missing_duplicate_overlapping_and_changed_policy(self):
        for mode in ('missing','duplicate','overlap','hash'):
            p=copy.deepcopy(self.policy)
            if mode=='missing':p['functions'].pop()
            if mode=='duplicate':p['functions'][1]=copy.deepcopy(p['functions'][0])
            if mode=='overlap':p['functions'][0]['size']=12
            if mode=='hash':p['functions'][0]['instruction_sha256']='0'*64
            self.reject(g.resolve_bindings,self.elf,p)

    def test_effective_config_keeps_all_originals_and_private_paths(self):
        bindings=g.resolve_bindings(self.elf,self.policy)
        names=[r['name']for r in self.policy['functions']]
        base=('[input]\nentrypoint=2147484672\nelf_path="elf/recomp.elf"\n'
              'output_func_path="RecompiledFuncs"\nrelocatable_sections_path="overlays.us.txt"\n'
              '[patches]\noriginal='+json.dumps(names)+'\nstubs=[]\nignored=[]\n').encode()
        with tempfile.TemporaryDirectory()as directory:
            root=Path(directory);(root/'elf').mkdir();PrivateGitOutputTests().git(root,'init','--quiet');(root/'.gitignore').write_text('/elf/*.original-bound.toml\n')
            out=g.private_output(root,root/'elf'/'test.original-bound.toml')
            raw=g.effective_toml(base,root/'base.toml',root/'elf'/'current.elf',self.policy,bindings)
            g.write_new(out,raw);self.assertEqual(g.read_file(out),raw)
            with self.assertRaises(FileExistsError):g.write_new(out,b'overwrite')
            self.reject(g.private_output,root,root/'public.toml')
            (root/'.gitignore').write_text('/elf/*.elf\n')
            self.reject(g.private_output,root,out)
        fewer=base.replace(json.dumps(names).encode(),json.dumps(names[:-1]).encode())
        self.reject(g.effective_toml,fewer,Path('base.toml'),Path('current.elf'),self.policy,bindings)




class PrivateGitOutputTests(unittest.TestCase):
    def repository(self, directory):
        root=Path(directory);(root/'elf').mkdir()
        self.git(root,'init','--quiet')
        (root/'.gitignore').write_text('/elf/*.original-bound.toml\n')
        return root,root/'elf'/'private.original-bound.toml'

    def git(self, root, *arguments):
        env={k:v for k,v in os.environ.items() if not k.upper().startswith('GIT_')}
        env.update(GIT_CONFIG_NOSYSTEM='1',GIT_CONFIG_GLOBAL=os.devnull)
        result=subprocess.run([shutil.which('git'),'-C',str(root),*arguments],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,timeout=10)
        self.assertEqual(result.returncode,0,'synthetic Git fixture setup failed')

    def test_ignored_untracked_positive_and_create_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root,out=self.repository(directory)
            g.write_private(root,out,b'synthetic-private-fixture')
            self.assertEqual(out.read_bytes(),b'synthetic-private-fixture')
            with self.assertRaises(FileExistsError):g.write_private(root,out,b'replace')

    def test_nested_negation_rejects_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root,out=self.repository(directory)
            (root/'elf'/'.gitignore').write_text('!*.original-bound.toml\n')
            with self.assertRaises(ValueError):g.write_private(root,out,b'fixture')
            self.assertFalse(out.exists())

    def test_tracked_existing_and_deleted_index_path_reject(self):
        for deleted in (False,True):
            with self.subTest(deleted=deleted), tempfile.TemporaryDirectory() as directory:
                root,out=self.repository(directory);out.write_bytes(b'public-placeholder')
                self.git(root,'add','--force','--',out.relative_to(root).as_posix())
                if deleted:out.unlink()
                with self.assertRaises(ValueError):g.write_private(root,out,b'fixture')
                self.assertEqual(out.exists(),not deleted)
                if not deleted:self.assertEqual(out.read_bytes(),b'public-placeholder')

    def test_missing_ignore_and_nonrepository_reject(self):
        with tempfile.TemporaryDirectory() as directory:
            root,out=self.repository(directory);(root/'.gitignore').unlink()
            with self.assertRaises(ValueError):g.write_private(root,out,b'fixture')
            self.assertFalse(out.exists())
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'elf').mkdir();out=root/'elf'/'x.original-bound.toml'
            (root/'.gitignore').write_text('/elf/*.original-bound.toml\n')
            with self.assertRaises(ValueError):g.write_private(root,out,b'fixture')
            self.assertFalse(out.exists())

    def test_final_recheck_detects_ignore_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root,out=self.repository(directory);g.private_output(root,out)
            (root/'elf'/'.gitignore').write_text('!*.original-bound.toml\n')
            with self.assertRaises(ValueError):g.write_private(root,out,b'fixture')
            self.assertFalse(out.exists())

    def test_inherited_git_index_cannot_hide_tracked_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root,out=self.repository(directory);out.write_bytes(b'placeholder')
            self.git(root,'add','--force','--',out.relative_to(root).as_posix());out.unlink()
            with mock.patch.dict(os.environ,{'GIT_INDEX_FILE':str(root/'unrelated-index')}):
                with self.assertRaises(ValueError):g.write_private(root,out,b'fixture')
            self.assertFalse(out.exists())


    def test_deleted_tracked_windows_case_alias_rejects(self):
        with tempfile.TemporaryDirectory() as directory:
            root,out=self.repository(directory)
            self.git(root,'config','core.ignoreCase','true')
            (root/'ELF').mkdir(exist_ok=True)
            tracked=root/'ELF'/'Case.original-bound.toml'
            tracked.write_bytes(b'public-placeholder')
            self.git(root,'add','--force','--','ELF/Case.original-bound.toml')
            tracked.unlink();out=root/'elf'/'case.original-bound.toml'
            with self.assertRaises(ValueError):g.write_private(root,out,b'fixture')
            self.assertFalse(out.exists())

    def test_missing_git_rejects_and_literal_metacharacters_are_exact(self):
        with tempfile.TemporaryDirectory() as directory:
            root,out=self.repository(directory)
            with mock.patch.object(g.shutil,'which',return_value=None):
                with self.assertRaises(ValueError):g.write_private(root,out,b'fixture')
            self.assertFalse(out.exists())
            out=root/'elf'/'[literal].original-bound.toml'
            g.write_private(root,out,b'fixture');self.assertEqual(out.read_bytes(),b'fixture')


if __name__=='__main__':unittest.main()
