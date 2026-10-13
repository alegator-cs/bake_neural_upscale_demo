"""Portable selected C16 refit adapter. No rendering or guide generation.

Retains the accepted model, objective, target-only patch sampler and seeds.
The adapter does not claim byte-identical replay of private chunk scheduling.
"""
from pathlib import Path
import argparse,hashlib,json,sys,time
sys.path.insert(0,str(Path(__file__).parents[1]/'models'))
from common import ROOT,SCENE,GEOMETRY,read,load_manifest,digest

def main():
    p=argparse.ArgumentParser();p.add_argument('command',choices=('check','fit'))
    p.add_argument('--manifest',type=Path,required=True);p.add_argument('--verify-sha',action='store_true')
    p.add_argument('--small32',type=Path);p.add_argument('--out',type=Path)
    p.add_argument('--device',choices=('cpu','cuda'),default='cpu')
    p.add_argument('--wall-cap-seconds',type=float,default=3600);p.add_argument('--updates',type=int,default=10000)
    a=p.parse_args();manifest=load_manifest(a.manifest,a.verify_sha,purpose='c16')
    if a.command=='check':print(json.dumps(dict(status='metadata_checked',train_views=384,selection_views=24,SHA_verified=a.verify_sha)));return
    if a.small32 is None or a.out is None:raise ValueError('Actual refitted small32 and fresh output required')
    if a.out.exists():raise FileExistsError('Preserve old run; choose fresh output')
    if not 0<a.wall_cap_seconds<=28800 or not 1<=a.updates<=10000:raise ValueError('Finite <=8h/10000 updates')
    baseline_sha=digest(a.small32)
    if baseline_sha!=manifest['upstream_A_small32_sha256']:raise ValueError('Recapture must use the actual newly fitted A correction')
    import numpy as np
    import torch
    from c16_model import PixelResidual4x,objective,write_asset
    from c16_data import TrainingPairs
    torch.set_num_threads(1);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
    torch.backends.cudnn.benchmark=False;torch.backends.cudnn.deterministic=True
    torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
    config=read(ROOT/'protocols/c16.json');started=time.monotonic();deadline=started+a.wall_cap_seconds
    torch.manual_seed(config['initialization_seed'])
    if a.device=='cuda':
        if not torch.cuda.is_available():raise RuntimeError('CUDA unavailable')
        torch.cuda.manual_seed_all(config['initialization_seed'])
    device=torch.device(a.device);pairs=TrainingPairs(manifest,config,deadline,dict(upstream={},upstream_sha256=manifest['upstream_A_identity_sha256']))
    model=PixelResidual4x('C16').to(device)
    optimizer=torch.optim.Adam(model.parameters(),lr=.001,betas=(.9,.999),eps=1e-8)
    rng=np.random.default_rng(config['batch_seed']);a.out.mkdir(parents=True);last_loss=None;schedule=hashlib.sha256();steps=0
    for steps in range(a.updates):
        if time.monotonic()+10>=deadline:break
        lr,base,target,origins,shape,rows=pairs.draw(rng,32,8)
        tensors=[torch.from_numpy(x).to(device)for x in(lr,base,target)]
        schedule.update(np.asarray(rows,dtype='<i4').tobytes())
        optimizer.zero_grad(set_to_none=True);pred=model.forward_patches(tensors[0],tensors[1],origins,shape)
        loss,terms=objective(pred,tensors[2],model,1e-5)
        if not torch.isfinite(loss):raise FloatingPointError('Nonfinite loss')
        loss.backward();torch.nn.utils.clip_grad_norm_(model.parameters(),1.,error_if_nonfinite=True);optimizer.step()
        last_loss=float(loss.detach().cpu())
        if (steps+1)%500==0:
            torch.save(dict(model=model.state_dict(),optimizer=optimizer.state_dict(),steps=steps+1,rng=rng.bit_generator.state),a.out/f'checkpoint_{steps+1:05d}.pt')
            print('checkpoint',steps+1,'loss',last_loss,flush=True)
    else:steps=a.updates
    if steps!=a.updates:raise TimeoutError('Cap reached; partial checkpoints retained; not a completed fit')
    code_sha=digest(ROOT/'models/c16_model.py');protocol_sha=digest(ROOT/'protocols/c16.json')
    asset=write_asset(a.out/'C16.nlr4',model,baseline_sha,code_sha,protocol_sha,manifest['upstream_A_identity_sha256'])
    receipt=dict(schema='C16_RELEASE_C16_REFIT_ADAPTER_1',steps=steps,train_views=384,selection_used_for_gradients=False,
        same_model_objective_sampler=True,portable_adapter=True,byte_identical_replay_verified=False,
        original_trainer_torch_version='2.5.0+cu124',torch_version=torch.__version__,device=str(device),
        elapsed_seconds=time.monotonic()-started,last_loss=last_loss,sampling_schedule_sha256=schedule.hexdigest(),asset=asset)
    (a.out/'FIT.json').write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':main()
