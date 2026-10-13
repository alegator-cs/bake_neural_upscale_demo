"""Build a portable manifest from explicit native payload locations; never render.

The registry supplies all 408 view IDs with native files. Teacher/capture receipt
and camera matching must be completed before use; this script is not a physical
first-hit or noise certification. It hashes all inputs and writes metadata only.
"""
from pathlib import Path
import argparse,json,sys,time
import numpy as np
from common import ROOT,SCENE,GEOMETRY,TIERS,read,descriptor,digest
from c16_data import DerivedBaseline

def main():
    p=argparse.ArgumentParser();p.add_argument('--registry',type=Path,required=True)
    p.add_argument('--purpose',choices=('a','c16'),required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--small32',type=Path);p.add_argument('--directional',type=Path)
    p.add_argument('--wall-cap-seconds',type=float,default=3600);a=p.parse_args()
    if a.out.exists():raise FileExistsError('Choose fresh manifest output')
    if not 0<a.wall_cap_seconds<=28800:raise ValueError('Finite <=8h cap required')
    registry=read(a.registry);by_id={r['view_id']:r for r in registry['views']}
    if len(by_id)!=408:raise ValueError('Exact384TRAIN+24selection registry required')
    deadline=time.monotonic()+a.wall_cap_seconds;rows=[]
    for camera in read(ROOT/'cameras/COHORT.json')['rows']:
        if time.monotonic()>=deadline:raise TimeoutError('Manifest hashing cap')
        row=by_id[camera['view_id']];lw,lh=TIERS[camera['tier']][0];hw,hh=TIERS[camera['tier']][1]
        frame=Path(row['lr_frame']);frame=frame if frame.is_absolute()else a.registry.parent/frame
        raw=frame.read_bytes()
        if len(raw)!=96 or raw[:64].hex()!=camera['raw_first64_hex']:raise ValueError('Actual raw96B camera/frame mismatch')
        result=dict(view_id=camera['view_id'],role=camera['split'],resolution_tag=camera['tier'],raw_first64_hex=raw[:64].hex(),
            actual_frame=descriptor(frame),camera=camera['camera'])
        specs={'lr_linear':([lh,lw,4],'<f4'),'target_rgba':([hh,hw,4],'<f4')}
        if a.purpose=='a':specs.update(target_linear=([hh,hw,4],'<f4'),lr_ids=([lh*2,lw*2],'<i4'),
            lr_barycentric=([lh*2,lw*2,2],'<f4'),lr_gate=([lh,lw],'<u4'))
        for key,(shape,dtype)in specs.items():
            path=Path(row[key]);path=path if path.is_absolute()else a.registry.parent/path
            container='NPY'if path.suffix=='.npy'else 'RAW'
            if container=='NPY':
                value=np.load(path,mmap_mode='r',allow_pickle=False)
                if value.shape!=tuple(shape)or value.dtype!=np.dtype(dtype):raise ValueError('Native header mismatch '+key)
                if isinstance(value,np.memmap):value._mmap.close()
            elif path.stat().st_size!=int(np.prod(shape))*np.dtype(dtype).itemsize:raise ValueError('Native RAW extent mismatch '+key)
            result[key]=descriptor(path,container,dtype,shape)
        if a.purpose=='c16':
            recipe=dict(schema='NL_FULL384_A2_BASELINE_RECIPE_1',input=result['lr_linear'],shape=[hh,hw,4],dtype='<f4')
            base=DerivedBaseline(recipe);h=hashlib.sha256()
            for y in range(0,hh,24):h.update(base[y:min(y+24,hh),0:hw].tobytes())
            result['CPU_reconstructed_display_sha256']=h.hexdigest()
        rows.append(result)
    identity=None
    if a.purpose=='c16':
        if not a.small32 or not a.directional:raise ValueError('Actual newly refitted A assets required')
        identity=dict(scene_sha256=SCENE,directional_sha256=digest(a.directional),small32_sha256=digest(a.small32))
    value=dict(schema='C16_RELEASE_DATASET_1',purpose=a.purpose,scene_sha256=SCENE,geometry_sha256=GEOMETRY,
        target_semantics='finite_native_Cycles512_associated_linear_and_display',views=rows,
        reference_is_finite_Monte_Carlo=True,reference_noise_quality_certified=False,
        input_data_physical_QA_required=True,selection_used_for_gradients=False)
    if identity:
        value.update(upstream_A_identity=identity,upstream_A_identity_sha256=hashlib.sha256(json.dumps(identity,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
            upstream_A_small32_sha256=identity['small32_sha256'])
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(value,indent=2)+'\n')
if __name__=='__main__':
    import hashlib
    main()
