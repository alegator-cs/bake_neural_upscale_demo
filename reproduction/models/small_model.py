"""Small reflection-specific residuals over the immutable directional baseline.

NLSR0001 contains correction parameters only. Coverage, gate and resolve remain
the prior surface contract. Neither target pixels nor camera IDs enter inference.
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

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'residual_experiment'))
from residual_model import neighbours_numpy, _sample_torch, BASELINE_PATH
from surface_model import load_asset, _validate_chart_meta, resolve_samples, resolve_numpy

MAGIC = b'NLSR0001'
HEADER_BYTES = 256
TEXELS, CHANNELS, INPUTS = 16384, 4, 34
STATIC_INPUTS = INPUTS - CHANNELS
REFLECTION_BANDS = (1, 2, 4)
WIDTHS = (16, 32)


def decoder_count(width):
    if width not in WIDTHS:
        raise ValueError('Unsupported small decoder width')
    return INPUTS * width + width + width * width + width + width * 3 + 3


def static_features_torch(samples, chart_meta, base_logits):
    get = lambda key: torch.as_tensor(samples[key], dtype=torch.float32, device=chart_meta.device)
    hit = torch.as_tensor(samples['hit'], dtype=torch.bool, device=chart_meta.device)
    chart = torch.where(hit, torch.as_tensor(samples['chart_id'], dtype=torch.long, device=chart_meta.device), 0)
    color = F.softplus(base_logits)
    reflection = get('reflection')
    derivatives = (get('uv_derivatives') * chart_meta[chart][..., [1, 2, 1, 2]]).clamp(-8, 8) / 8
    blocks = [color / (1 + color), reflection, get('roughness')[..., None],
              get('ndotv').clamp(0, 1)[..., None], derivatives]
    for band in REFLECTION_BANDS:
        phase = reflection * np.float32(np.pi * band)
        blocks.extend((torch.sin(phase), torch.cos(phase)))
    return torch.cat(blocks, -1)


def static_features_numpy(samples, chart_meta, base_logits):
    get = lambda key: np.asarray(samples[key], dtype='f4')
    chart = np.where(samples['hit'], samples['chart_id'], 0)
    color = np.maximum(base_logits, 0) + np.log1p(np.exp(-np.abs(base_logits)))
    reflection = get('reflection')
    derivatives = np.clip(get('uv_derivatives') * chart_meta[chart][..., [1, 2, 1, 2]].astype('f4'), -8, 8) / np.float32(8)
    blocks = [color / (1 + color), reflection, get('roughness')[..., None],
              np.clip(get('ndotv'), 0, 1)[..., None], derivatives]
    for band in REFLECTION_BANDS:
        phase = reflection * np.float32(np.pi * band)
        blocks.extend((np.sin(phase), np.cos(phase)))
    return np.concatenate(blocks, -1).astype('f4')


def adjacent_edges(chart_meta):
    """One undirected horizontal/vertical edge, including physical chart wraps.

    No pooled-storage neighbours, padding or unproved cross-chart seam edges.
    """
    edges = []
    for offset, width, height, flags in np.asarray(chart_meta, dtype='i8'):
        grid = offset + np.arange(width * height, dtype='i8').reshape(height, width)
        if width > 1:
            edges.append(np.stack((grid[:, :-1].ravel(), grid[:, 1:].ravel()), -1))
            if flags & 1 and width > 2:
                edges.append(np.stack((grid[:, -1], grid[:, 0]), -1))
        if height > 1:
            edges.append(np.stack((grid[:-1].ravel(), grid[1:].ravel()), -1))
            if flags & 2 and height > 2:
                edges.append(np.stack((grid[-1], grid[0]), -1))
    if not edges:
        raise ValueError('No valid surface adjacency edges')
    return np.ascontiguousarray(np.concatenate(edges), dtype='i8')


class SmallModel(nn.Module):
    def __init__(self, width, baseline):
        super().__init__()
        decoder_count(width)
        if baseline['mode'] != 'directional' or not 0 < baseline['texels'] <= 262144:
            raise ValueError('Expected isolated directional baseline within262144 texels')
        self.width = width
        self.texels = int(baseline["texels"])
        self.baseline_sha256 = baseline['asset_sha256']
        meta = _validate_chart_meta(baseline['chart_meta'], self.texels).astype('i8')
        self.register_buffer('chart_meta', torch.from_numpy(meta.copy()))
        self.register_buffer('baseline_chart_meta', torch.from_numpy(meta.copy()))
        self.register_buffer('baseline_atlas', torch.from_numpy(baseline['atlas'].copy()))
        self.register_buffer('adjacency_edges', torch.from_numpy(adjacent_edges(meta)))
        self.atlas = nn.Parameter(torch.empty(self.texels, CHANNELS))
        nn.init.normal_(self.atlas, 0, .01)
        self.layers = nn.Sequential(nn.Linear(INPUTS, width), nn.ReLU(), nn.Linear(width, width),
                                    nn.ReLU(), nn.Linear(width, 3))
        for layer in (self.layers[0], self.layers[2]):
            nn.init.xavier_uniform_(layer.weight)
            nn.init.zeros_(layer.bias)
        nn.init.zeros_(self.layers[4].weight)
        nn.init.zeros_(self.layers[4].bias)

    def penalty_terms(self):
        edge = self.adjacency_edges
        latent = (self.atlas[edge[:, 0]] - self.atlas[edge[:, 1]]).square().mean() / .1**2
        # Equal weight per affine layer, independently of decoder width. Biases
        # are excluded; no learned/current-RMS denominator changes this scale.
        head = sum(layer.in_features * layer.weight.square().mean()
                   for layer in (self.layers[0], self.layers[2], self.layers[4])) / 3
        return latent, head

    def forward(self, samples):
        get = lambda key, dtype=torch.float32: torch.as_tensor(samples[key], dtype=dtype, device=self.atlas.device)
        hit = get('hit', torch.bool)
        chart = torch.where(hit, get('chart_id', torch.long), 0)
        reflection, uv = get('reflection'), get('uv')
        base = _sample_torch(self.baseline_atlas, self.baseline_chart_meta, chart, uv).reshape(-1, 3, 4)
        logits = base[:, :, 0] + (base[:, :, 1:] * reflection[:, None, :]).sum(-1)
        local = _sample_torch(self.atlas, self.chart_meta, chart, uv)
        delta = self.layers(torch.cat((local, static_features_torch(samples, self.chart_meta, logits)), -1))
        return F.softplus(logits + delta) * hit[..., None]


def _decoder(model):
    return np.concatenate([p.detach().cpu().numpy().ravel()
                           for layer in (model.layers[0], model.layers[2], model.layers[4])
                           for p in (layer.weight, layer.bias)]).astype('<f4')


def write_small(path, model, baseline):
    texels = int(model.texels)
    meta = model.chart_meta.cpu().numpy().astype('<u4')
    atlas = model.atlas.detach().cpu().numpy().astype('<f4')
    decoder = _decoder(model)
    count = decoder_count(model.width)
    if not np.array_equal(meta, baseline['chart_meta']):
        raise ValueError('Frozen baseline layout changed')
    if len(decoder) != count or not np.isfinite(atlas).all() or not np.isfinite(decoder).all():
        raise ValueError('Invalid model parameter payload')
    payload = meta.tobytes() + atlas.tobytes() + decoder.tobytes()
    header = bytearray(HEADER_BYTES)
    header[:8] = MAGIC
    struct.pack_into('<12I', header, 8, HEADER_BYTES, 1, texels, CHANNELS, INPUTS, model.width,
                     count, len(meta), 1, 1, 1, 0)
    header[64:96] = bytes.fromhex(baseline['asset_sha256'])
    header[96:128] = bytes.fromhex(baseline['geometry_sha256'])
    header[128:160] = bytes.fromhex(baseline['scene_sha256'])
    header[160:192] = hashlib.sha256(payload).digest()
    struct.pack_into('<QQ', header, 192, len(payload), texels * CHANNELS + count)
    Path(path).write_bytes(header + payload)
    return load_small(path, baseline)


def load_small(path, baseline):
    texels = int(baseline["texels"])
    if not 0 < texels <= 262144: raise ValueError("Texel cap")
    raw = Path(path).read_bytes()
    if len(raw) < HEADER_BYTES or raw[:8] != MAGIC:
        raise ValueError('Invalid NLSR0001 asset')
    h = struct.unpack_from('<12I', raw, 8)
    width = h[5]
    count = decoder_count(width)
    if h != (HEADER_BYTES, 1, texels, CHANNELS, INPUTS, width, count, baseline['chart_count'], 1, 1, 1, 0):
        raise ValueError('Small asset contract mismatch')
    if any(raw[56:64]) or any(raw[208:256]):
        raise ValueError('Reserved header fields are nonzero')
    if raw[64:96].hex() != baseline['asset_sha256'] or raw[96:128].hex() != baseline['geometry_sha256'] or raw[128:160].hex() != baseline['scene_sha256']:
        raise ValueError('Wrong frozen baseline/geometry/scene')
    size, learned = struct.unpack_from('<QQ', raw, 192)
    if len(raw) != HEADER_BYTES + size or size != baseline['chart_count'] * 16 + (texels * CHANNELS + count) * 4 or learned != texels * CHANNELS + count:
        raise ValueError('Small asset payload size mismatch')
    payload = raw[HEADER_BYTES:]
    if hashlib.sha256(payload).digest() != raw[160:192]:
        raise ValueError('Small asset payload checksum mismatch')
    meta = _validate_chart_meta(np.frombuffer(payload, dtype='<u4', count=baseline['chart_count'] * 4).reshape(-1, 4), texels)
    if not np.array_equal(meta, baseline['chart_meta']):
        raise ValueError('Frozen chart layout mismatch')
    data = np.frombuffer(payload, dtype='<f4', offset=len(meta) * 16).copy()
    if not np.isfinite(data).all():
        raise ValueError('Small asset contains nonfinite parameters')
    return dict(format='NLSR0001', mode='small_reflection', texels=texels, channels=CHANNELS,
                decoder_input=INPUTS, hidden_width=width, hidden_layers=2, reflection_bands=list(REFLECTION_BANDS),
                decoder_weight_count=count, atlas_scalar_count=texels * CHANNELS, learned_param_count=learned,
                asset_bytes=len(raw), asset_sha256=hashlib.sha256(raw).hexdigest(),
                baseline_sha256=baseline['asset_sha256'], geometry_sha256=baseline['geometry_sha256'],
                scene_sha256=baseline['scene_sha256'], chart_meta=meta,
                atlas=data[:texels * CHANNELS].reshape(texels, CHANNELS), decoder_weights=data[texels * CHANNELS:])


def model_from_asset(asset, baseline):
    if asset['baseline_sha256'] != baseline['asset_sha256']:
        raise ValueError('Wrong frozen baseline')
    model = SmallModel(asset['hidden_width'], baseline)
    with torch.no_grad():
        model.atlas.copy_(torch.from_numpy(asset['atlas']))
        offset = 0
        for layer in (model.layers[0], model.layers[2], model.layers[4]):
            for p in (layer.weight, layer.bias):
                count = p.numel()
                p.copy_(torch.from_numpy(asset['decoder_weights'][offset:offset + count].reshape(p.shape)))
                offset += count
        assert offset == decoder_count(model.width)
    return model.eval()


def metadata(asset):
    return {key: value for key, value in asset.items() if key not in ('chart_meta', 'atlas', 'decoder_weights')}


def decode_numpy(baseline, asset, samples, batch_size=8192):
    if asset['baseline_sha256'] != baseline['asset_sha256']:
        raise ValueError('Wrong frozen baseline')
    n = len(samples['hit'])
    output = np.empty((n, 3), 'f4')
    width = asset['hidden_width']
    for begin in range(0, n, batch_size):
        end = min(n, begin + batch_size)
        s = {key: np.asarray(value[begin:end]) for key, value in samples.items()}
        hit = s['hit'].astype(bool)
        chart = np.where(hit, s['chart_id'], 0)
        ix, weight = neighbours_numpy(s['uv'], chart, baseline['chart_meta'])
        coeff = (baseline['atlas'][ix] * weight[..., None]).sum(-2).reshape(-1, 3, 4)
        logits = coeff[:, :, 0] + (coeff[:, :, 1:] * s['reflection'][:, None, :]).sum(-1)
        local = (asset['atlas'][ix] * weight[..., None]).sum(-2)
        x = np.concatenate((local, static_features_numpy(s, asset['chart_meta'], logits)), -1)
        offset = 0
        for i, (nin, nout) in enumerate(zip((INPUTS, width, width), (width, width, 3))):
            count = nin * nout
            weights = asset['decoder_weights'][offset:offset + count].reshape(nout, nin)
            offset += count
            bias = asset['decoder_weights'][offset:offset + nout]
            offset += nout
            x = x @ weights.T + bias
            if i < 2:
                x = np.maximum(x, 0)
        logits = logits + x
        rgb = np.maximum(logits, 0) + np.log1p(np.exp(-np.abs(logits)))
        rgb[~hit] = 0
        output[begin:end] = rgb
    return output
