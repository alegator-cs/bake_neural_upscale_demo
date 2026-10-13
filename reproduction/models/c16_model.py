"""Arm A pixel-only4x models. Imported only by explicitly authorized workers.

No camera/XY/geometry features. Integer patch origins enforce real-frame
replicate padding and phase alignment; they are never learned inputs.
"""
from pathlib import Path
import hashlib
import struct
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

CASES = {"C16": (16, 0), "C24_DS2": (24, 2)}


def ledger(case, width=480, height=270):
    channels, blocks = CASES[case]
    parameters = 9*channels*channels + 102*channels + 64 + blocks*(channels*channels + 11*channels)
    macs = 9*channels*channels + 100*channels + blocks*(channels*channels + 9*channels)
    return dict(case=case, channels=channels, depthwise_residual_blocks=blocks,
        parameters=parameters, weight_bytes=4*parameters, header_bytes=256,
        asset_bytes=256+4*parameters, LR_receptive_field=5+2*blocks,
        learned_MACs_per_LR_pixel=macs, learned_MACs_per_frame=macs*width*height,
        logical_runtime_display_and_two_latent_bytes=4*width*height*(4+2*channels),
        training_materialized_head_bytes=4*width*height*64,
        native_runtime_plan="Two LR C buffers: DW reads current latent into scratch; pointwise reads scratch plus same-channel current latent and writes current latent in place. Actual allocator/driver usage and latency remain unmeasured.")


def phase_shuffle(value):
    n, channels, height, width = value.shape
    if channels != 64:
        raise ValueError("Phase-major16xRGBA head required")
    return value.reshape(n, 4, 4, 4, height, width).permute(0, 3, 4, 1, 5, 2).reshape(n, 4, 4*height, 4*width)


def project(base, raw):
    delta = torch.tanh(raw)
    alpha = torch.clamp(base[:, 3:4] + delta[:, 3:4], 0, 1)
    rgb = torch.minimum(torch.clamp_min(base[:, :3] + delta[:, :3], 0), alpha)
    return torch.cat((rgb, alpha), 1)


class PixelResidual4x(nn.Module):
    def __init__(self, case):
        super().__init__()
        channels, blocks = CASES[case]
        self.case, self.halo = case, 2+blocks
        self.conv1 = nn.Conv2d(4, channels, 3)
        self.conv2 = nn.Conv2d(channels, channels, 3)
        self.depthwise = nn.ModuleList(nn.Conv2d(channels, channels, 3, groups=channels) for _ in range(blocks))
        self.pointwise = nn.ModuleList(nn.Conv2d(channels, channels, 1) for _ in range(blocks))
        self.head = nn.Conv2d(channels, 64, 1)
        for layer in (self.conv1, self.conv2, *self.depthwise):
            nn.init.xavier_uniform_(layer.weight); nn.init.zeros_(layer.bias)
        # Identity residual blocks initially; exact zero residual output head.
        for layer in (*self.pointwise, self.head):
            nn.init.zeros_(layer.weight); nn.init.zeros_(layer.bias)
        if sum(item.numel() for item in self.parameters()) != ledger(case)["parameters"]:
            raise ValueError("Parameter ledger mismatch")

    def forward(self, lr, base):
        value = torch.relu(self.conv1(F.pad(lr, (1,1,1,1), mode="replicate")))
        value = torch.relu(self.conv2(F.pad(value, (1,1,1,1), mode="replicate")))
        for depthwise, pointwise in zip(self.depthwise, self.pointwise):
            hidden = torch.relu(depthwise(F.pad(value, (1,1,1,1), mode="replicate")))
            value = torch.relu(value + pointwise(hidden))
        return project(base, phase_shuffle(self.head(value)))

    @staticmethod
    def _global_clamp(value, origins, offset, shape):
        height, width = shape
        n,c,h,w=value.shape
        ox,oy=origins[:,0:1]+offset,origins[:,1:2]+offset
        gx=torch.arange(w,device=value.device)[None,:]+ox
        gy=torch.arange(h,device=value.device)[None,:]+oy
        ix=gx.clamp(0,width-1)-ox;iy=gy.clamp(0,height-1)-oy
        value=value.gather(2,iy[:,None,:,None].expand(n,c,h,w))
        return value.gather(3,ix[:,None,None,:].expand(n,c,h,w))

    def forward_patches(self, lr, base, origins, frame_shape):
        core = base.shape[-1]//4
        halo = 4  # Shared9x9 context for paired candidate sampling.
        if base.shape[1:] != (4,4*core,4*core) or lr.shape[1:] != (4,core+2*halo,core+2*halo):
            raise ValueError("Aligned square4x core and shared4LR halo required")
        if len(origins) != lr.shape[0] or any(not(0 <= x <= frame_shape[1]-core and 0 <= y <= frame_shape[0]-core) for x,y in origins):
            raise ValueError("Patch outside real frame")
        origins=torch.as_tensor(origins,dtype=torch.long,device=lr.device)
        offset = -halo+1
        value = torch.relu(self.conv1(lr))
        value = self._global_clamp(value, origins, offset, frame_shape)
        offset += 1
        value = torch.relu(self.conv2(value))
        value = self._global_clamp(value, origins, offset, frame_shape)
        for depthwise, pointwise in zip(self.depthwise, self.pointwise):
            hidden = torch.relu(depthwise(value))
            value = torch.relu(value[:,:,1:-1,1:-1] + pointwise(hidden))
            offset += 1
            value = self._global_clamp(value, origins, offset, frame_shape)
        crop = -offset
        value = value[:,:,crop:crop+core,crop:crop+core]
        return project(base, phase_shuffle(self.head(value)))

    def layers_for_export(self):
        layers = [self.conv1,self.conv2]
        for depthwise, pointwise in zip(self.depthwise, self.pointwise):
            layers.extend((depthwise,pointwise))
        return (*layers,self.head)


def objective(pred, target, model, regularizer=1e-5):
    """Common loss for both cases; target weights never enter inference."""
    luma = .2126*target[:,0:1] + .7152*target[:,1:2] + .0722*target[:,2:3]
    weight = 1+2*torch.clamp((luma-.6)/.4,0,1)
    rgb = ((pred[:,:3]-target[:,:3]).square()*weight).mean()/weight.mean()
    alpha = (pred[:,3:4]-target[:,3:4]).square().mean()
    white = (pred[:,:3]-pred[:,3:4]-target[:,:3]+target[:,3:4]).square().mean()
    def gradient(a,b):
        return torch.stack([F.huber_loss(torch.diff(a,dim=axis),torch.diff(b,dim=axis),delta=.02) for axis in (-2,-1)]).mean()
    rgb_gradient, alpha_gradient = gradient(pred[:,:3],target[:,:3]),gradient(pred[:,3:4],target[:,3:4])
    terms=[]
    for layer in model.layers_for_export():
        fan_in=(layer.in_channels//layer.groups)*layer.kernel_size[0]*layer.kernel_size[1]
        terms.extend((layer.weight.square().mean()*fan_in,layer.bias.square().mean()))
    fan_in_l2=torch.stack(terms).mean()
    total=rgb+.25*alpha+.25*white+.1*rgb_gradient+.05*alpha_gradient+regularizer*fan_in_l2
    return total,dict(rgb=rgb,alpha=alpha,white=white,rgb_gradient=rgb_gradient,alpha_gradient=alpha_gradient,fan_in_l2=fan_in_l2)


def write_asset(path, model, baseline_sha256, code_sha256, protocol_sha256, upstream_sha256):
    """C16 exact existing NLR40001 ABI; C24 remains offline NLA40001.

    NLR4 baseline SHA is the actual whole small32 correction asset. Its reserved
    bytes remain zero; complete upstream identities live in authenticated receipts.
    """
    count=ledger(model.case); channels,blocks=CASES[model.case]
    payload=b"".join(parameter.detach().cpu().numpy().astype("<f4").tobytes()
        for layer in model.layers_for_export() for parameter in (layer.weight,layer.bias))
    if len(payload)!=count["weight_bytes"] or not np.isfinite(np.frombuffer(payload,dtype="<f4")).all():
        raise ValueError("Invalid asset payload")
    header=bytearray(256)
    if model.case=="C16":
        header[:8]=b"NLR40001"
        struct.pack_into("<12I",header,8,256,1,1,channels,4,64,3,4,count["parameters"],1,1,1)
        struct.pack_into("<2I",header,56,1,1)
    else:
        header[:8]=b"NLA40001"
        struct.pack_into("<12I",header,8,256,1,2,channels,blocks,4,4,64,count["LR_receptive_field"],count["parameters"],1,1)
    for offset,digest in ((64,baseline_sha256),(96,code_sha256),(128,protocol_sha256)):
        header[offset:offset+32]=bytes.fromhex(digest)
    header[160:192]=hashlib.sha256(payload).digest()
    struct.pack_into("<Q2f",header,192,len(payload),1.,1.)
    if model.case!="C16":header[208:240]=bytes.fromhex(upstream_sha256)
    raw=bytes(header)+payload
    with Path(path).open("xb") as stream:stream.write(raw)
    return dict(path=str(Path(path).resolve()),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),
        format="NLR40001" if model.case=="C16" else "NLA40001",native_ABI_compatible=model.case=="C16",
        native_runtime_measured=False,upstream_A_identity_sha256=upstream_sha256,**count)
