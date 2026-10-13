"""Portable A directional/small32 refit adapter, preserving accepted numeric core.

Default check reads metadata/headers only. Fit requires a complete native dataset,
an explicit device, fresh output and a finite wall cap. Never runs a renderer.
This adapter is not a byte-identical replay of private job/chunk machinery.
"""
from pathlib import Path
import argparse,hashlib,json,math,sys,time
sys.path.insert(0,str(Path(__file__).parents[1]/'models'))
from common import ROOT,SCENE,GEOMETRY,TIERS,read,load_manifest,AData,digest

def main():
    p=argparse.ArgumentParser();p.add_argument('command',choices=('check','fit'))
    p.add_argument('--manifest',type=Path,required=True);p.add_argument('--verify-sha',action='store_true')
    p.add_argument('--stage',choices=('directional','small32'));p.add_argument('--baseline',type=Path)
    p.add_argument('--out',type=Path);p.add_argument('--device',choices=('cpu','cuda'),default='cpu')
    p.add_argument('--wall-cap-seconds',type=float,default=3600);p.add_argument('--updates',type=int,default=10000)
    p.add_argument('--mesh',type=Path,default=ROOT/'scene/mesh_v1.npz')
    a=p.parse_args();manifest=load_manifest(a.manifest,a.verify_sha)
    if a.command=='check':print(json.dumps(dict(status='metadata_checked',train_views=384,selection_views=24,SHA_verified=a.verify_sha)));return
    if a.stage is None or a.out is None:raise ValueError('Explicit stage and fresh output required')
    if a.out.exists():raise FileExistsError('Preserve previous fit; use fresh output')
    if not 0<a.wall_cap_seconds<=28800 or not 1<=a.updates<=10000:raise ValueError('Finite <=8h and <=10000 updates')
    import numpy as np
    import torch
    import surface_model,small_model
    from a_numerics import predict
    torch.set_num_threads(1);torch.set_num_interop_threads(1)
    torch.backends.cudnn.benchmark=False;torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
    torch.manual_seed(11)
    if a.device=='cuda':
        if not torch.cuda.is_available():raise RuntimeError('CUDA unavailable')
        torch.cuda.manual_seed_all(11)
    device=torch.device(a.device);a.out.mkdir(parents=True);started=time.monotonic();deadline=started+a.wall_cap_seconds
    config=read(ROOT/('protocols/a_directional.json'if a.stage=='directional'else 'protocols/a_small32.json'))
    meta=np.load(ROOT/'scene/chart_metadata_16384.npy',allow_pickle=False)
    baseline=None
    if a.stage=='directional':model=surface_model.SurfaceModel('directional',meta,16384).to(device)
    else:
        if a.baseline is None:raise ValueError('Small32 requires newly fitted A directional base')
        baseline=surface_model.load_asset(a.baseline,GEOMETRY,SCENE)
        if not np.array_equal(meta,baseline['chart_meta']):raise ValueError('Original88chartlayout required')
        model=small_model.SmallModel(32,baseline).to(device)
    groups=[dict(params=[model.atlas],lr=.01)]
    if a.stage=='small32':groups.append(dict(params=model.layers.parameters(),lr=.003))
    optimizer=torch.optim.Adam(groups,weight_decay=0);dataset=AData(manifest,a.mesh);loss_value=None;steps=0
    for steps in range(a.updates):
        if time.monotonic()+10>=deadline:break
        rng=np.random.default_rng(config['batch_seed']+steps);row=dataset.rows[int(rng.integers(len(dataset.rows)))]
        w,h=TIERS[row['resolution_tag']][0];indices=rng.integers(0,w*h,size=1024)
        samples,gate,target,bad=dataset.sample(row,indices)
        multiplier=.1+.9*(1+np.cos(np.pi*min(steps/10000,1)))/2
        for i,group in enumerate(optimizer.param_groups):group['lr']=(.01 if i==0 else .003)*multiplier
        optimizer.zero_grad(set_to_none=True)
        predicted=predict(torch,surface_model,model,samples,gate,'directional'if a.stage=='directional'else 'correction')
        truth=torch.as_tensor(target,device=device,dtype=torch.float32);loss=(predicted[:,:3]-truth[:,:3]).square().mean()
        if a.stage=='small32':
            edge,head=model.penalty_terms();loss=loss+1e-5*edge+1e-5*head
        if not torch.isfinite(loss):raise FloatingPointError('Nonfinite loss')
        loss.backward();optimizer.step();loss_value=float(loss.detach().cpu())
        if (steps+1)%500==0:
            torch.save(dict(model=model.state_dict(),optimizer=optimizer.state_dict(),steps=steps+1),a.out/f'checkpoint_{steps+1:05d}.pt')
            print('checkpoint',steps+1,'loss',loss_value,flush=True)
    else:steps=a.updates
    if steps!=a.updates:raise TimeoutError('Cap reached; partial checkpoints retained; not a completed fit')
    filename='directional.nlsf'if a.stage=='directional'else 'small32.nlsr'
    if a.stage=='directional':asset=surface_model.write_asset(a.out/filename,model,GEOMETRY,SCENE)
    else:asset=small_model.write_small(a.out/filename,model,baseline)
    receipt=dict(schema='C16_RELEASE_A_REFIT_ADAPTER_1',steps=steps,stage=a.stage,train_views=384,
        selection_used_for_gradients=False,portable_adapter=True,byte_identical_replay_verified=False,
        seed_initialization=11,batch_seed=config['batch_seed'],batch_pixels=1024,last_loss=loss_value,
        torch_version=torch.__version__,device=str(device),elapsed_seconds=time.monotonic()-started,
        asset={k:v for k,v in asset.items()if k not in ('atlas','chart_meta','decoder_weights')})
    (a.out/'FIT.json').write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':main()
