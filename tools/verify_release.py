"""Verify the tracked release snapshot before edits/builds; does not render."""
from pathlib import Path
import argparse,hashlib,json
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    p.add_argument('--allow-generated',action='store_true',help='Permit only the two declared regenerated shader C/header files to differ after a local build')
    a=p.parse_args();root=a.root.resolve()
    manifest=json.loads((root/'provenance/MANIFEST.json').read_text())
    errors=[];generated=[]
    for row in manifest['files']:
        path=root/row['path']
        if not path.is_file():errors.append(row['path']+': missing');continue
        data=path.read_bytes()
        if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
            if a.allow_generated and row['path'] in manifest['regenerated_source_files']:generated.append(row['path'])
            else:errors.append(row['path']+': bytes/hash differ')
    print(json.dumps(dict(status='PASS' if not errors else 'FAIL',files=len(manifest['files']),expected_bytes=manifest['hashed_file_bytes'],errors=errors,permitted_regenerated_changes=generated),indent=2))
    return 1 if errors else 0
if __name__=='__main__':raise SystemExit(main())

