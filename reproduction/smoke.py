"""Small CPU release check. Default: headers/hash/scene/camera/source checks only.

--numerics additionally runs tiny CPU model/phase/padding checks without backward,
optimization, rendering, CUDA initialization or performance measurements.
"""
from pathlib import Path
import argparse,ast,hashlib,json,math,os,struct,sys

ROOT=Path(__file__).resolve().parent
EXPECTED={
    'matched_A.nlsf':(788096,'546a3d123111056e155f4f0491245775375f4047711441997072cb98bd944f01'),
    'matched_A.nlsr':(272908,'02707afc205514bdd4a70675bc5e74e3e08f483783545822ff2b71406f062ece'),
    'matched_C16.nlr4':(16256,'d56767cf48ff7e2933867c8a7ddec43a4dabc4808eb5b1d53c5bf6e29441e3a5')}
def sha(raw):return hashlib.sha256(raw).hexdigest()
def require(value,message):
    if not value:raise ValueError(message)

def check(assets,numerics=False):
    checks=[];raws={}
    for filename,(size,digest)in EXPECTED.items():
        raw=(assets/filename).read_bytes();require(len(raw)==size and sha(raw)==digest,'Wrong selected '+filename)
        raws[filename]=raw;checks.append('exact identity '+filename)
    a=raws['matched_A.nlsf'];s=raws['matched_A.nlsr'];c=raws['matched_C16.nlr4']
    require(a[:8]==b'NLSF0001','A format');h=struct.unpack_from('<14I',a,8)
    require(h[:9]==(256,2,16384,12,12,0,0,0,88),'A directional shape')
    require(struct.unpack_from('<QQ',a,192)==(787840,196608),'A scalar/payload count')
    chart=a[256:256+88*16]
    require(sha(chart)==a[128:160].hex(),'A layout digest')
    checksum=2166136261;zero=bytearray(a);zero[60:64]=b'\0'*4
    for value in zero:checksum=((checksum^value)*16777619)&0xffffffff
    require(checksum==h[13],'A FNV checksum')
    require(s[:8]==b'NLSR0001'and struct.unpack_from('<12I',s,8)==(256,1,16384,4,34,32,2275,88,1,1,1,0),'Small32 shape')
    require(s[64:96].hex()==EXPECTED['matched_A.nlsf'][1],'Small32 baseline binding')
    require(s[256:256+88*16]==chart and sha(s[256:])==s[160:192].hex(),'Small32 chart/payload identity')
    require(c[:8]==b'NLR40001' and struct.unpack_from('<12I',c,8)==(256,1,1,16,4,64,3,4,4000,1,1,1),'C16 shape')
    require(c[64:96].hex()==EXPECTED['matched_A.nlsr'][1]and sha(c[256:])==c[160:192].hex(),'C16 baseline/payload binding')
    require(all(math.isfinite(x[0])for x in struct.iter_unpack('<f',c[256:])),'C16 finite weights')
    checks.extend(['asset headers and payloads','upstream baseline chain','same88chartlayout','finite4000C16weights'])
    redaction=json.loads((ROOT/'scene/FILE_UI_REDACTION.json').read_text());require(sha((ROOT/'scene/indirect_alcove.blend').read_bytes())==redaction['released_file_sha256'],'UI-redacted frozen scene identity')
    cohort=json.loads((ROOT/'cameras/COHORT.json').read_text());rows=cohort['rows']
    require(len(rows)==408 and len({r['view_id']for r in rows})==408,'408 unique views')
    require(sum(r['split']=='train'for r in rows)==384 and sum(r['split']=='selection'for r in rows)==24,'Disjoint splits')
    for row in rows:
        require(len(bytes.fromhex(row['raw_first64_hex']))==64,'Camera raw length')
        require(all(math.isfinite(v)for v in struct.unpack('<16f',bytes.fromhex(row['raw_first64_hex']))),'Camera finite bits')
    checks.append('frozen scene and408cameras')
    for path in ROOT.rglob('*.py'):ast.parse(path.read_text(encoding='utf-8'),filename=str(path))
    checks.append('Python source syntax')
    result=dict(status='PASS',checks=checks,selected_model_bytes=sum(v[0]for v in EXPECTED.values()),
        GPU_used=False,training_updates=0,renderer_runs=0,builds=0,numerics_requested=numerics,
        native_parity_rerun=False,full_retraining_verified=False)
    if numerics:
        os.environ['CUDA_VISIBLE_DEVICES']='';os.environ['OMP_NUM_THREADS']='1';os.environ['MKL_NUM_THREADS']='1'
        import numpy as np
        import torch
        sys.path.insert(0,str(ROOT/'models'));sys.path.insert(0,str(ROOT/'training'))
        from c16_model import PixelResidual4x,phase_shuffle
        from c16_data import display_from_linear,DerivedBaseline
        import surface_model,small_model
        torch.set_num_threads(1);torch.set_num_interop_threads(1)
        base=surface_model.load_asset(assets/'matched_A.nlsf');correction=small_model.load_small(assets/'matched_A.nlsr',base)
        require(base['learned_param_count']==196608 and correction['learned_param_count']==67811,'Original codec import')
        model=PixelResidual4x('C16');weights=np.frombuffer(c,dtype='<f4',offset=256).copy();offset=0
        with torch.no_grad():
            for layer in model.layers_for_export():
                for parameter in(layer.weight,layer.bias):
                    count=parameter.numel();parameter.copy_(torch.from_numpy(weights[offset:offset+count].reshape(tuple(parameter.shape))));offset+=count
            require(offset==4000,'C16 export order')
            rng=np.random.default_rng(11);linear=rng.uniform(0,.15,(12,12,4)).astype('f4');linear[...,3]=.5
            lr=display_from_linear(linear);base_np=np.full((48,48,4),.2,'f4');base_np[...,3]=.5
            full=model(torch.from_numpy(lr.transpose(2,0,1).copy()[None]),torch.from_numpy(base_np.transpose(2,0,1).copy()[None]))
            maximum=0.
            for x,y in((0,0),(4,4),(8,8)):
                ix=np.clip(np.arange(x-4,x+8),0,11);iy=np.clip(np.arange(y-4,y+8),0,11)
                tile=lr[iy[:,None],ix[None,:]].transpose(2,0,1).copy()[None]
                baseline=base_np[4*y:4*(y+4),4*x:4*(x+4)].transpose(2,0,1).copy()[None]
                pred=model.forward_patches(torch.from_numpy(tile),torch.from_numpy(baseline),[(x,y)],(12,12))
                maximum=max(maximum,float((pred-full[:,:,4*y:4*(y+4),4*x:4*(x+4)]).abs().max()))
            require(maximum<=1e-6,'Real-frame patch/whole-padding mismatch')
            require(torch.all(full>=0)and torch.all(full<=1)and torch.all(full[:,:3]<=full[:,3:4]),'Associated output bounds')
            phase=phase_shuffle(torch.arange(64,dtype=torch.float32).reshape(1,64,1,1))[0].permute(1,2,0).numpy()
            require(np.array_equal(phase,np.arange(64,dtype='f4').reshape(4,4,4)),'Phase-majorRGBA ordering')
        require(not torch.cuda.is_initialized(),'CUDA must remain uninitialized')
        result.update(numpy_version=np.__version__,torch_version=torch.__version__,CUDA_initialized=False,
            whole_vs_patch_max_abs_error=maximum,original_codec_imported=True,C16_phase_export_padding_checked=True)
    return result

def main():
    p=argparse.ArgumentParser();p.add_argument('--assets',type=Path,required=True);p.add_argument('--out',type=Path)
    p.add_argument('--numerics',action='store_true');a=p.parse_args();result=check(a.assets,a.numerics)
    if a.out:
        if a.out.exists():raise FileExistsError('Use fresh receipt path')
        a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
