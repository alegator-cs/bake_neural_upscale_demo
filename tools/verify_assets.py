"""CPU-only integrity check for the released October10 C16 runtime assets."""
from pathlib import Path
import argparse,hashlib,json,math,struct
def check(root):
    manifest=json.loads((root/'assets/MANIFEST.json').read_text())
    rows=[]
    for row in manifest['assets']:
        path=root/row['relative'];data=path.read_bytes()
        if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
            raise ValueError('Asset integrity mismatch: '+row['relative'])
        rows.append(dict(path=row['relative'],bytes=len(data)))
    data=(root/'assets/matched_C16.nlr4').read_bytes();words=struct.unpack('<64I',data[:256])
    if data[:8]!=b'NLR40001' or words[2]!=256 or words[5]!=16 or words[10]!=4000 or len(data)!=16256:
        raise ValueError('Expected C16 NLR40001/4000-parameter ABI')
    if not all(math.isfinite(x) for x in struct.unpack('<4000f',data[256:])):
        raise ValueError('Nonfinite C16 weights')
    return dict(status='PASS',asset_files=len(rows),serialized_asset_bytes=sum(x['bytes'] for x in rows),appearance_bytes=manifest['appearance_bytes'],C16_parameters=4000,CPU_only=True)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]);a=p.parse_args()
    print(json.dumps(check(a.root.resolve()),indent=2))

