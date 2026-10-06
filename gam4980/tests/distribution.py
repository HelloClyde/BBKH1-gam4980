"""Validate both local installation packages and their source provenance."""
from pathlib import Path
import sys,json,hashlib,zipfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'sdk'))
from h1_bda.validate import validate_bda
expected=['应用/程序/H1GAM4980.bda','gam4980/8.BIN','gam4980/E.BIN','README.md']
rows=[]
for name in ['H1GAM4980','H1GAM4980-profile']:
    bda=ROOT/'dist'/(name+'.bda');meta=json.loads(bda.with_suffix('.build.json').read_text())
    assert validate_bda(bda)['ok']
    assert meta['sha256']==hashlib.sha256(bda.read_bytes()).hexdigest()
    assert bda.with_suffix('.bda.sha256').read_text().strip()==meta['sha256']+'  '+name+'.bda'
    with zipfile.ZipFile(ROOT/'dist'/(name+'-install.zip')) as z:
        assert z.namelist()==expected
        assert z.read(expected[0])==bda.read_bytes()
        assert z.read('README.md')==(ROOT/'gam4980/README.md').read_bytes()
        for rom in ['8.BIN','E.BIN']:assert z.read('gam4980/'+rom)==(ROOT/'gam4980/runtime'/rom).read_bytes()
    for source,sha in meta['source_sha256'].items():
        assert hashlib.sha256((ROOT/'gam4980'/source).read_bytes()).hexdigest()==sha,source
    rows.append({'name':name,'version':meta['version'],'sha256':meta['sha256'],'size':bda.stat().st_size,'package_ok':True})
upstream_hashes={'s6502.c': 'e9f5b4fb6799cd9ef8d14f9cce6d3609c4ec0aad657e1eaedce56b51602d0658', 'gam4980_core.h': '80cdfe9c04d46343027598a77ab4303a5987a5034cb797b51ac519d133a81557'}
for source,sha in upstream_hashes.items():
    assert hashlib.sha256((ROOT/"gam4980/core"/source).read_bytes()).hexdigest()==sha
report={'packages':rows,'commercial_games_included':False,'s6502_and_core_header_unchanged':True,
        'core_change':'Stop the frame scheduler at the first BRK and ignore subsequent step_frame calls.'}
version=rows[0]['version'].replace('.','')
assert all(row['version']==rows[0]['version'] for row in rows)
(ROOT/'gam4980/verification'/('package-v'+version+'.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
