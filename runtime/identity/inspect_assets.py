"""Validate newly fitted bytes and print review-only allowlist rows.

This does not edit headers, admit a fit, certify a teacher, build or launch.
Keep a new fit in a separate work directory; see docs/IDENTITY.md.
"""
from pathlib import Path
import argparse, json
import codec_reference as codec

REPO = Path(__file__).resolve().parents[2]
REFERENCE_BASE_SHA = '546a3d123111056e155f4f0491245775375f4047711441997072cb98bd944f01'
REFERENCE_CORRECTION_SHA = '02707afc205514bdd4a70675bc5e74e3e08f483783545822ff2b71406f062ece'

def bounded_snapshot_read(path,cap):
    if path.stat().st_size > cap: raise ValueError('Oversized input: '+str(path))
    # A size check alone cannot bound a file that grows before the read.
    with path.open('rb') as stream: raw=stream.read(cap+1)
    if len(raw)>cap: raise ValueError('Input grew beyond cap')
    return raw

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',type=Path,required=True)
    p.add_argument('--correction',type=Path,required=True)
    p.add_argument('--cnn',type=Path)
    p.add_argument('--c16-source',type=Path)
    p.add_argument('--c16-protocol',type=Path)
    p.add_argument('--tag',required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args(); tag=codec.token(a.tag)
    if len(tag)>52: raise ValueError('Tag must leave space for the A_/C16_ label prefixes')
    if (a.cnn is None) != (a.c16_source is None) or (a.cnn is None) != (a.c16_protocol is None):
        raise ValueError('CNN inspection requires its actual model-source and protocol byte files')
    if a.out.exists(): raise FileExistsError('Use a fresh output directory; preserve previous inspection')
    snapshots={}; caps={}
    def read(path,cap):
        path=path.resolve(); raw=bounded_snapshot_read(path,cap)
        snapshots[path]=raw
        caps[path]=cap
        return raw
    original_base=read(REPO/'assets/matched_A.nlsf',codec.MAX_BASE_BYTES)
    original_correction=read(REPO/'assets/matched_A.nlsr',codec.MAX_CORRECTION_BYTES)
    if codec.sha(original_base)!=REFERENCE_BASE_SHA or codec.sha(original_correction)!=REFERENCE_CORRECTION_SHA:
        raise ValueError('Packaged layout reference changed; restore the preserved release assets')
    reference=codec.validate_pair(original_base,original_correction)
    rawbase=read(a.base,codec.MAX_BASE_BYTES); rawcorrection=read(a.correction,codec.MAX_CORRECTION_BYTES)
    pair=codec.validate_coarse(rawbase,rawcorrection,reference['charts'])
    rowA='    {"A_%s",16384u,%uULL,%uULL,"%s","%s",0},' % (
        tag,pair['base_bytes'],pair['correction_bytes'],pair['base_sha256'],pair['correction_sha256'])
    proposed=['/* CODEC CHECK ONLY: do not insert until owner evidence review is complete. */',rowA]
    cnn=None
    if a.cnn is not None:
        code=read(a.c16_source,2*1024**2); protocol=read(a.c16_protocol,20*1024**2)
        json.loads(protocol.decode('utf-8-sig'))
        rawcnn=read(a.cnn,16256)
        cnn=codec.validate_cnn(rawcnn,pair['correction_sha256'],codec.sha(code),codec.sha(protocol))
        proposed.append('    {"C16_%s","%s","%s","%s","%s","unreviewed_codec_only_not_admitted",16u,4000u,16256u,1u},' % (
            tag,cnn['sha256'],cnn['baseline_sha256'],cnn['code_sha256'],cnn['protocol_sha256']))
    # Freeze all inspected inputs before writing any proposal.
    for path,raw in snapshots.items():
        if bounded_snapshot_read(path,caps[path])!=raw: raise ValueError('Inspected input changed: '+path.name)
    result=dict(schema='C16_RELEASE_IDENTITY_CODEC_INSPECTION_1',status='codec_checks_passed_owner_review_required',
        byte_validation_only=True,fit_or_teacher_or_runtime_admission_verified=False,
        compiled_headers_modified=False,model_source_or_protocol_identity_inferred=False,
        exact_original_88_chart_records=True,A_texels=pair['texels'],A_pair=pair,C16=cnn,
        same_A_bytes_as_preserved_release=(pair['base_sha256']==REFERENCE_BASE_SHA and pair['correction_sha256']==REFERENCE_CORRECTION_SHA),
        inputs=[dict(name=path.name,bytes=len(raw),sha256=codec.sha(raw)) for path,raw in snapshots.items()],
        next_step='Review real fit/selection/teacher/recapture evidence; then a reviewed isolated branch may append exact rows, preserve all gates, regenerate launch manifest and rebuild. See docs/IDENTITY.md.',
        GPU_build_render_train_jobs=0)
    a.out.mkdir(parents=True)
    (a.out/'INSPECTION.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    (a.out/'PROPOSED_ROWS.txt').write_text('\n'.join(proposed)+'\n',encoding='utf-8')
    print(json.dumps(dict(status=result['status'],A_sha256=pair['base_sha256'],correction_sha256=pair['correction_sha256'],C16_sha256=cnn['sha256']if cnn else None,headers_modified=False)))

if __name__=='__main__': main()
