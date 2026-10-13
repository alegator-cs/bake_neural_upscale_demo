"""Frozen directional appearance plus a zero-initialized local correction.

NLRF0001 stores only correction parameters. The baseline SHA256 is mandatory.
No labels, alpha image, camera ID, or display-space sharpening enter inference.
"""
from __future__ import annotations
import hashlib
import struct
import sys
from pathlib import Path
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'experiment'))
from surface_model import load_asset, resolve_samples, resolve_numpy, _validate_chart_meta

MAGIC = b'NLRF0001'
HEADER_BYTES = 256
SPECS = {'neural': (1, 16384, 4, 16, 16, 595),
         'quadratic': (2, 4096, 18, 0, 0, 0)}
BASELINE_PATH = Path(__file__).resolve().parents[1] / 'experiment/run001/directional128/selected.nlsf'


def neighbours_numpy(uv, chart, meta):
    m = meta[chart]
    xy = uv * m[..., 1:3].astype('f4') - np.float32(.5)
    ij = np.floor(xy).astype('i8')
    frac = (xy - ij).astype('f4')
    ids, weights = [], []
    for dy in (0, 1):
        for dx in (0, 1):
            x, y = ij[..., 0] + dx, ij[..., 1] + dy
            x = np.where(m[..., 3] & 1, x % m[..., 1], np.clip(x, 0, m[..., 1]-1))
            y = np.where(m[..., 3] & 2, y % m[..., 2], np.clip(y, 0, m[..., 2]-1))
            ids.append(m[..., 0] + y*m[..., 1] + x)
            weights.append((frac[..., 0] if dx else 1-frac[..., 0]) *
                           (frac[..., 1] if dy else 1-frac[..., 1]))
    return np.stack(ids, -1), np.stack(weights, -1)


def quadratic_basis(reflection):
    x, y, z = reflection.unbind(-1)
    return torch.stack((torch.ones_like(x), x*y, y*z, z*x, x*x-y*y,
                        (3*z*z-1)*.5), -1)


def quadratic_basis_numpy(reflection):
    x, y, z = np.moveaxis(reflection, -1, 0)
    return np.stack((np.ones_like(x), x*y, y*z, z*x, x*x-y*y,
                     (3*z*z-1)*np.float32(.5)), -1)


def _sample_torch(atlas, meta, chart, uv):
    meta = torch.as_tensor(meta, dtype=torch.long, device=atlas.device)
    chart = torch.as_tensor(chart, dtype=torch.long, device=atlas.device)
    uv = torch.as_tensor(uv, dtype=atlas.dtype, device=atlas.device)
    m = meta[chart]
    xy = uv*m[..., 1:3]-.5
    ij = torch.floor(xy).long()
    frac = xy-ij
    result = torch.zeros((*uv.shape[:-1], atlas.shape[-1]), dtype=atlas.dtype, device=atlas.device)
    for dy in (0, 1):
        for dx in (0, 1):
            x, y = ij[..., 0]+dx, ij[..., 1]+dy
            x = torch.where((m[..., 3]&1)!=0, torch.remainder(x,m[..., 1]),
                            torch.minimum(torch.clamp_min(x,0),m[..., 1]-1))
            y = torch.where((m[..., 3]&2)!=0, torch.remainder(y,m[..., 2]),
                            torch.minimum(torch.clamp_min(y,0),m[..., 2]-1))
            w = (frac[..., 0] if dx else 1-frac[..., 0])*(frac[..., 1] if dy else 1-frac[..., 1])
            result = result + atlas[m[..., 0]+y*m[..., 1]+x]*w[..., None]
    return result


class ResidualModel(nn.Module):
    def __init__(self, mode, chart_meta, baseline):
        super().__init__()
        self.mode, self.baseline_sha256 = mode, baseline['asset_sha256']
        _, texels, channels, inputs, hidden, weights = SPECS[mode]
        assert baseline['mode']=='directional' and baseline['texels']==16384
        self.register_buffer('chart_meta', torch.from_numpy(_validate_chart_meta(chart_meta,texels).astype('i8')))
        self.register_buffer('baseline_chart_meta', torch.from_numpy(baseline['chart_meta'].astype('i8')))
        self.register_buffer('baseline_atlas', torch.from_numpy(baseline['atlas'].copy()))
        self.atlas = nn.Parameter(torch.zeros(texels,channels))
        self.layers = None
        if mode == 'neural':
            nn.init.normal_(self.atlas, 0, .01)
            self.layers = nn.Sequential(nn.Linear(inputs,hidden),nn.ReLU(),nn.Linear(hidden,hidden),
                                        nn.ReLU(),nn.Linear(hidden,3))
            for layer in (self.layers[0],self.layers[2]):
                nn.init.xavier_uniform_(layer.weight)
                nn.init.zeros_(layer.bias)
            nn.init.zeros_(self.layers[4].weight)
            nn.init.zeros_(self.layers[4].bias)

    def correction(self, values, base_logits, reflection, roughness, ndotv, derivatives):
        if self.mode=='quadratic':
            return (values.reshape(*values.shape[:-1],3,6)*quadratic_basis(reflection)[...,None,:]).sum(-1)
        color=F.softplus(base_logits)
        features=torch.cat((values,color/(1+color),reflection,roughness[...,None],
                            ndotv.clamp(0,1)[...,None],derivatives),-1)
        return self.layers(features)

    def forward(self, samples):
        get=lambda k,dtype=torch.float32: torch.as_tensor(samples[k],dtype=dtype,device=self.atlas.device)
        hit=get('hit',torch.bool)
        chart=torch.where(hit,get('chart_id',torch.long),0)
        uv,reflection=get('uv'),get('reflection')
        base=_sample_torch(self.baseline_atlas,self.baseline_chart_meta,chart,uv).reshape(-1,3,4)
        logits=base[...,0]+(base[...,1:]*reflection[:,None,:]).sum(-1)
        values=_sample_torch(self.atlas,self.chart_meta,chart,uv)
        derivatives=(get('uv_derivatives')*self.chart_meta[chart][:,[1,2,1,2]]).clamp(-8,8)/8
        delta=self.correction(values,logits,reflection,get('roughness'),get('ndotv'),derivatives)
        return F.softplus(logits+delta)*hit[...,None]


def _decoder_array(model):
    if model.layers is None: return np.empty(0,'<f4')
    return np.concatenate([p.detach().numpy().ravel() for l in (model.layers[0],model.layers[2],model.layers[4])
                           for p in (l.weight,l.bias)]).astype('<f4')


def write_residual(path, model, baseline):
    modeid,texels,channels,inputs,hidden,weights=SPECS[model.mode]
    meta=model.chart_meta.numpy().astype('<u4')
    atlas=model.atlas.detach().numpy().astype('<f4')
    decoder=_decoder_array(model)
    assert len(decoder)==weights and np.isfinite(atlas).all() and np.isfinite(decoder).all()
    payload=meta.tobytes()+atlas.tobytes()+decoder.tobytes()
    header=bytearray(HEADER_BYTES)
    header[:8]=MAGIC
    struct.pack_into('<12I',header,8,HEADER_BYTES,modeid,texels,channels,inputs,hidden,weights,len(meta),1,1,1,0)
    header[64:96]=bytes.fromhex(baseline['asset_sha256'])
    header[96:128]=bytes.fromhex(baseline['geometry_sha256'])
    header[128:160]=bytes.fromhex(baseline['scene_sha256'])
    header[160:192]=hashlib.sha256(payload).digest()
    struct.pack_into('<QQ',header,192,len(payload),texels*channels+weights)
    Path(path).write_bytes(header+payload)
    return load_residual(path,baseline)


def load_residual(path, baseline=None):
    raw=Path(path).read_bytes()
    if len(raw)<HEADER_BYTES or raw[:8]!=MAGIC: raise ValueError('invalid NLRF0001 asset')
    header,modeid,texels,channels,inputs,hidden,weights,charts,feature,resolve,flags,reserved=struct.unpack_from('<12I',raw,8)
    modes=[k for k,v in SPECS.items() if v[0]==modeid]
    if not modes: raise ValueError('unknown mode')
    mode=modes[0]
    if (modeid,texels,channels,inputs,hidden,weights)!=SPECS[mode]: raise ValueError('shape mismatch')
    payload_bytes,learned=struct.unpack_from('<QQ',raw,192)
    if header!=256 or (feature,resolve,flags,reserved)!=(1,1,1,0) or any(raw[56:64]) or any(raw[208:256]):
        raise ValueError('unsupported header')
    if len(raw)!=256+payload_bytes or payload_bytes!=charts*16+(texels*channels+weights)*4 or learned!=texels*channels+weights:
        raise ValueError('payload size mismatch')
    payload=raw[256:]
    if hashlib.sha256(payload).digest()!=raw[160:192]: raise ValueError('payload checksum mismatch')
    baseline_sha=raw[64:96].hex()
    if baseline is not None and (baseline_sha!=baseline['asset_sha256'] or raw[96:128].hex()!=baseline['geometry_sha256'] or raw[128:160].hex()!=baseline['scene_sha256']):
        raise ValueError('frozen baseline or scene mismatch')
    meta=_validate_chart_meta(np.frombuffer(payload,dtype='<u4',count=charts*4).reshape(charts,4),texels)
    if baseline is not None and (charts!=baseline['chart_count'] or not np.array_equal(meta[:,3],baseline['chart_meta'][:,3])):
        raise ValueError('baseline chart count/wrap mismatch')
    data=np.frombuffer(payload,dtype='<f4',offset=charts*16).copy()
    if not np.isfinite(data).all(): raise ValueError('nonfinite parameters')
    return dict(format='NLRF0001',mode=mode,texels=texels,channels=channels,decoder_input=inputs,
                hidden_width=hidden,decoder_weight_count=weights,learned_param_count=learned,
                chart_count=charts,chart_meta=meta,atlas=data[:texels*channels].reshape(texels,channels),
                decoder_weights=data[texels*channels:],asset_bytes=len(raw),asset_sha256=hashlib.sha256(raw).hexdigest(),
                baseline_sha256=baseline_sha,geometry_sha256=raw[96:128].hex(),scene_sha256=raw[128:160].hex())


def model_from_asset(asset, baseline):
    if asset['baseline_sha256']!=baseline['asset_sha256']: raise ValueError('wrong frozen baseline')
    model=ResidualModel(asset['mode'],asset['chart_meta'],baseline)
    with torch.no_grad():
        model.atlas.copy_(torch.from_numpy(asset['atlas']))
        offset=0
        if model.layers is not None:
            for layer in (model.layers[0],model.layers[2],model.layers[4]):
                for parameter in (layer.weight,layer.bias):
                    count=parameter.numel()
                    parameter.copy_(torch.from_numpy(asset['decoder_weights'][offset:offset+count].reshape(parameter.shape)))
                    offset+=count
        assert offset==len(asset['decoder_weights'])
    return model.eval()


def metadata(asset):
    return {k:v for k,v in asset.items() if k not in ('chart_meta','atlas','decoder_weights')}


def decode_numpy(baseline, residual, samples, batch_size=8192):
    if residual['baseline_sha256']!=baseline['asset_sha256']: raise ValueError('wrong frozen baseline')
    n=len(samples['hit']); output=np.empty((n,3),'f4')
    for begin in range(0,n,batch_size):
        end=min(n,begin+batch_size); s={k:np.asarray(v[begin:end]) for k,v in samples.items()}
        hit=s['hit'].astype(bool); chart=np.where(hit,s['chart_id'],0)
        ix,wt=neighbours_numpy(s['uv'],chart,baseline['chart_meta'])
        base=(baseline['atlas'][ix]*wt[...,None]).sum(-2).reshape(-1,3,4)
        logits=base[:,:,0]+(base[:,:,1:]*s['reflection'][:,None,:]).sum(-1)
        ix,wt=neighbours_numpy(s['uv'],chart,residual['chart_meta'])
        val=(residual['atlas'][ix]*wt[...,None]).sum(-2)
        if residual['mode']=='quadratic':
            delta=(val.reshape(-1,3,6)*quadratic_basis_numpy(s['reflection'])[:,None,:]).sum(-1)
        else:
            color=np.maximum(logits,0)+np.log1p(np.exp(-np.abs(logits)))
            deriv=np.clip(s['uv_derivatives']*residual['chart_meta'][chart][:,[1,2,1,2]].astype('f4'),-8,8)/np.float32(8)
            x=np.concatenate((val,color/(1+color),s['reflection'],s['roughness'][:,None],
                              np.clip(s['ndotv'],0,1)[:,None],deriv),-1)
            offset=0
            for i,(nin,nout) in enumerate(zip((16,16,16),(16,16,3))):
                count=nin*nout
                w=residual['decoder_weights'][offset:offset+count].reshape(nout,nin);offset+=count
                b=residual['decoder_weights'][offset:offset+nout];offset+=nout
                x=x@w.T+b
                if i<2:x=np.maximum(x,0)
            delta=x
        logits=logits+delta
        rgb=np.maximum(logits,0)+np.log1p(np.exp(-np.abs(logits)))
        rgb[~hit]=0
        output[begin:end]=rgb
    return output
