"""Generate the final asset checksums; optionally verify downloaded assets."""
from pathlib import Path
import argparse, hashlib, json
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,default=ROOT/'dist')
    parser.add_argument('--verify',action='store_true')
    args=parser.parse_args();folder=args.directory
    manifest=folder/'SHA256SUMS.txt'
    if args.verify:
        for line in manifest.read_text().splitlines():
            digest,name=line.split('  ',1)
            if Path(name).name!=name:raise RuntimeError('Invalid asset path')
            assert hashlib.sha256((folder/name).read_bytes()).hexdigest()==digest,name
        print('All downloaded asset checksums match');return
    for name in ['H1GAM4980','H1GAM4980-profile']:
        meta=json.loads((folder/(name+'.build.json')).read_text())
        assert meta['version']=='0.1.8' and meta['test_frames']==0
        assert hashlib.sha256((folder/(name+'.bda')).read_bytes()).hexdigest()==meta['sha256']
        for suffix in ['.bda','.build.json','.bda.sha256','-install.zip']:
            assert (folder/(name+suffix)).is_file()
    assets=sorted(p for p in folder.iterdir() if p.is_file() and p.name!=manifest.name)
    manifest.write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in assets),encoding='ascii')
    print('Prepared',len(assets),'release assets')
if __name__=='__main__':main()
