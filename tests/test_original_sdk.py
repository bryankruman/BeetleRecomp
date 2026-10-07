"""Check actual generated save code and fail-closed original-function selection."""
from pathlib import Path
import re,subprocess,sys,tempfile,tomllib
root=Path(__file__).resolve().parents[1]
compiler=Path(sys.argv[1]).resolve()
config=(root/'BeetleRecomp.toml').read_text()
settings=tomllib.loads(config);names=settings['patches']['original']
generated='\n'.join(p.read_text() for p in (root/'RecompiledFuncs').glob('*.c'))
for name in names:
    original='original_'+name
    body=re.search(r'RECOMP_FUNC void '+re.escape(original)+r'\([^\n]+\)\s*\{(.*?)(?=\nRECOMP_FUNC void |\Z)',generated,re.S)
    assert body and len(body[1])>100,original+' did not retain its original code'
    assert not re.search(r'(?<![A-Za-z0-9_])'+re.escape(name)+r'_recomp\(rdram, ctx\)',generated),name+' still calls the host stub'
with tempfile.TemporaryDirectory(prefix='original-sdk-') as scratch:
    scratch=Path(scratch)
    for label,extra in [('missing',['barMissingOriginalSdkFunction']),('duplicate',[names[0]])]:
        candidate=config.replace('original = [','original = [\n'+''.join('    "'+name+'",\n' for name in extra),1)
        for key in ['elf_path','relocatable_sections_path']:
            value=settings['input'][key];candidate=candidate.replace(key+' = "'+value+'"',key+' = "'+(root/value).as_posix()+'"')
        candidate=candidate.replace('output_func_path = "RecompiledFuncs"','output_func_path = "'+(scratch/label).as_posix()+'"')
        path=scratch/(label+'.toml');path.write_text(candidate)
        result=subprocess.run([str(compiler),str(path)],cwd=root,capture_output=True,text=True)
        assert result.returncode!=0 and 'Original function' in result.stderr,(label,result.returncode,result.stderr[-1000:])
print('PASS: all '+str(len(names))+' original SDK bodies and calls retained; missing/duplicate selectors rejected.')
