"""Read-only native mode9 completion checker; metadata/frame checks by default.

Optional --verify-sha streams existing raw files. Never imports a model, traces,
renders, changes files or creates data. A passing check is not physical/noise QA.
"""
from pathlib import Path
import argparse,hashlib,json,struct
def load(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb')as f:
        for raw in iter(lambda:f.read(1048576),b''):h.update(raw)
    return h.hexdigest()
def check(plan_path,batch_index,home,base_sha,correction_sha,verify=False):
    plan=load(plan_path);batch=plan['batches'][batch_index];game=(home/'baseoa').resolve()
    if plan['schema']!='C16_RELEASE_MODE9_CAPTURE_PACKETS_1':raise ValueError('Packet schema')
    hw,hh=batch['native_output'];lw,lh=batch['native_LR'];expected={
        'hr_rgba':([hh,hw,4],'<f4',hw*hh*16),'lr_rgba':([lh,lw,4],'<f4',lw*lh*16),
        'lr_ids':([lh*2,lw*2],'<u4',lw*lh*16),'lr_bary':([lh*2,lw*2,2],'<f4',lw*lh*32),
        'lr_center_ids':([lh,lw],'<u4',lw*lh*4),'hr_frame':([96],'|u1',96),
        'lr_frame':([96],'|u1',96),'upscale_extent':([16],'|u1',16)}
    checked=[]
    for row in batch['views']:
        path=game/(row['stem']+'.json');m=load(path)
        fields=dict(schema='NL4X_MULTIRES_RUNTIME_CAPTURE_1',complete=True,mode='bilinear',mode_index=9,
            pair_name=batch['tier'],view_id=row['view_id'],output_width=hw,output_height=hh,lr_width=lw,lr_height=lh,
            framebuffer_width=hw,framebuffer_height=hh,pixel_origin='top_left',quarter_order='row_major_00_10_01_11',
            lr_transfer='linear_associated',lr_center_semantics='incident_bool01',output_semantics='display_associated',
            hr_guides_valid=False,lr_valid=True,hr_bvh_quarter_rays=0,hr_analytic_center_gates=0,
            baseline_sha256=base_sha,directional_base_sha256=base_sha,correction_sha256=correction_sha)
        for key,value in fields.items():
            if m.get(key)!=value:raise ValueError('Capture mismatch '+str(row['view_id'])+'/'+key)
        paths={}
        for key,(shape,dtype,size)in expected.items():
            a=m['arrays'][key];p=(game/a['path']).resolve()
            if not p.is_relative_to(game)or a['shape']!=shape or a['dtype']!=dtype or a['bytes']!=size or p.stat().st_size!=size:
                raise ValueError('Native array header/path/extent mismatch '+key)
            if verify and digest(p)!=a['sha256']:raise ValueError('Native array SHA differs '+key)
            paths[key]=p
        for key,width,height in [('hr_frame',hw,hh),('lr_frame',lw,lh)]:
            raw=paths[key].read_bytes()
            if raw[:64].hex()!=row['raw_first64_hex']or struct.unpack_from('<8I',raw,64)!=(width,height,2,10936,0,0,0,0):
                raise ValueError('Native96B camera/frame differs '+key)
        if struct.unpack('<4I',paths['upscale_extent'].read_bytes())!=(hw,hh,lw,lh):raise ValueError('True4x extent')
        checked.append(dict(view_id=row['view_id'],split=row['split'],tier=row['tier'],capture_manifest_sha256=digest(path)))
    return dict(status='PASS',scope='native_completion_metadata_file_sizes_and_actual96B_frames',views=checked,
        whole_payload_SHA_verified=verify,physical_first_hit_QA=False,reference_noise_QA=False,GPU_jobs=0,images_generated=0)
def main():
    p=argparse.ArgumentParser();p.add_argument('--plan',type=Path,required=True);p.add_argument('--batch',type=int,required=True)
    p.add_argument('--home',type=Path,required=True);p.add_argument('--directional-sha256',required=True);p.add_argument('--small32-sha256',required=True)
    p.add_argument('--verify-sha',action='store_true');a=p.parse_args()
    print(json.dumps(check(a.plan,a.batch,a.home,a.directional_sha256,a.small32_sha256,a.verify_sha),indent=2))
if __name__=='__main__':main()
