"""Refresh relative hashes for Git-tracked files after deliberate owner edits."""
from pathlib import Path
import argparse,hashlib,json,subprocess
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    a=p.parse_args();root=a.root.resolve();target=root/'provenance/MANIFEST.json'
    manifest=json.loads(target.read_text())
    names=subprocess.check_output(['git','-C',str(root),'ls-files','-z']).decode().split('\0')
    rows=[]
    for name in names:
        if not name or name=='provenance/MANIFEST.json':continue
        path=root/name
        if not path.is_file():raise ValueError('Tracked file missing: '+name)
        data=path.read_bytes()
        rows.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    if not rows:raise ValueError('No tracked files: stage the intended source/assets/docs first')
    manifest['files']=rows;manifest['hashed_file_bytes']=sum(x['bytes'] for x in rows)
    target.write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(dict(files=len(rows),bytes=manifest['hashed_file_bytes'],manifest='provenance/MANIFEST.json')))
if __name__=='__main__':main()

