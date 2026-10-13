"""Canonical NLSF0001 appearance, chart sampling, asset and resolve contract.

No rendering/training is launched by this module. Geometry owners supply the
oriented shading normal, unit reflection and raw native-pixel UV derivatives.
GLSL users provide SF_CHART(i):uvec4, SF_ATLAS(i):float and SF_WEIGHT(i):float.
SF_ATLAS addresses packed scalar storage (including padding), not texels.
"""
from __future__ import annotations

import hashlib
import math
import struct
from pathlib import Path

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

MAGIC = b"NLSF0001"
HEADER_BYTES = 256
BBOX_MIN = np.array([-174, -166, 34], dtype=np.float32)
BBOX_MAX = np.array([112, 166, 302], dtype=np.float32)
DERIVATIVE_CLIP = 8.0
MODES = {
    "texture": (1, 3, 4, 0, 0, 0),
    "directional": (2, 12, 12, 0, 0, 0),
    "local": (3, 11, 12, 16, 17, 611),
    "global": (4, 0, 0, 64, 28, 6211),
}
ALIASES = {"texture3": "texture", "directional12": "directional",
           "local11": "local", "global28": "global"}


def canonical_mode(mode):
    mode = ALIASES.get(str(mode), str(mode))
    if mode not in MODES:
        raise ValueError(f"Unknown surface mode {mode!r}")
    return mode


def _validate_chart_meta(meta, limit):
    raw = np.asarray(meta)
    if raw.ndim != 2 or raw.shape[1] != 4 or len(raw) == 0:
        raise ValueError("chart_meta must be nonempty [N,4]")
    if not np.issubdtype(raw.dtype, np.integer):
        raise ValueError("chart metadata must have integer dtype")
    if np.any(raw < 0) or np.any(raw > np.iinfo(np.uint32).max):
        raise ValueError("chart metadata does not fit uint32")
    m = raw.astype(np.uint64)
    if np.any(m[:, 1:3] == 0) or np.any(m[:, 3] > 3):
        raise ValueError("zero chart dimensions or unknown wrap flags")
    ends = m[:, 0] + m[:, 1] * m[:, 2]
    if np.any(ends > limit):
        raise ValueError("chart rectangle exceeds its pooled texel budget")
    order = np.argsort(m[:, 0], kind="stable")
    if len(order) > 1 and np.any(m[order[1:], 0] < ends[order[:-1]]):
        raise ValueError("chart storage rectangles overlap")
    return np.ascontiguousarray(raw, dtype="<u4")


def _meta_extent(meta):
    m = np.asarray(meta, dtype=np.int64)
    return int(np.max(m[:, 0] + m[:, 1] * m[:, 2]))


def _sample_field(samples, key, device, dtype=torch.float32):
    return torch.as_tensor(samples[key], device=device, dtype=dtype)


class SurfaceModel(nn.Module):
    """Learned appearance only; geometry-derived coverage has no parameters."""

    def __init__(self, mode, chart_meta, texel_count=None, base_logits=None):
        super().__init__()
        self.mode = canonical_mode(mode)
        _, channels, _, hidden, inputs, _ = MODES[self.mode]
        if self.mode == "global":
            if texel_count not in (None, 0):
                raise ValueError("global mode stores no atlas; texel_count must be 0")
            self.texel_count = 0
            meta = _validate_chart_meta(chart_meta, 16384)
        else:
            self.texel_count = int(texel_count if texel_count is not None else _meta_extent(chart_meta))
            if not 0 < self.texel_count <= 262144:
                raise ValueError("isolated comparison appearance pool must contain 1..262144 texels")
            meta = _validate_chart_meta(chart_meta, self.texel_count)
        self.register_buffer("chart_meta", torch.from_numpy(meta.astype(np.int64)))
        self.atlas = None
        self.layers = None
        if channels:
            initial = torch.zeros((self.texel_count, channels), dtype=torch.float32)
            if self.mode == "local":
                initial[:, :8].normal_(0, 0.01)
            if base_logits is not None:
                base = torch.as_tensor(base_logits, dtype=torch.float32)
                if base.shape == (3,):
                    base = base.expand(self.texel_count, 3)
                if base.shape != (self.texel_count, 3) or not torch.isfinite(base).all():
                    raise ValueError("base_logits must be finite RGB or [texels,3]")
                if self.mode == "texture":
                    initial[:] = base
                elif self.mode == "directional":
                    initial[:, [0, 4, 8]] = base
                else:
                    initial[:, 8:11] = base
            self.atlas = nn.Parameter(initial)
        if hidden:
            self.layers = nn.Sequential(nn.Linear(inputs, hidden), nn.ReLU(),
                                        nn.Linear(hidden, hidden), nn.ReLU(), nn.Linear(hidden, 3))
            for layer in (self.layers[0], self.layers[2], self.layers[4]):
                nn.init.xavier_uniform_(layer.weight)
                nn.init.zeros_(layer.bias)
            if self.mode == "local":
                nn.init.zeros_(self.layers[4].weight)

    def learned_parameter_count(self):
        return sum(p.numel() for p in self.parameters())

    def _chart_info(self, samples):
        dev = self.chart_meta.device
        hit = _sample_field(samples, "hit", dev, torch.bool).reshape(-1)
        chart = _sample_field(samples, "chart_id", dev, torch.long).reshape(-1)
        if chart.shape != hit.shape:
            raise ValueError("chart_id/hit lengths differ")
        if torch.any(hit & ((chart < 0) | (chart >= len(self.chart_meta)))):
            raise ValueError("active sample chart ID is invalid")
        chart = torch.where(hit, chart, 0)
        return hit, self.chart_meta[chart]

    def sample_atlas(self, uv, meta):
        if self.atlas is None:
            raise ValueError("global mode has no atlas")
        uv = torch.as_tensor(uv, device=self.chart_meta.device, dtype=torch.float32)
        xy = uv * meta[:, 1:3] - 0.5
        ij = torch.floor(xy).long()
        frac = xy - ij
        result = torch.zeros((len(uv), self.atlas.shape[1]), device=uv.device, dtype=torch.float32)
        for dy in (0, 1):
            for dx in (0, 1):
                x = ij[:, 0] + dx
                y = ij[:, 1] + dy
                x = torch.where((meta[:, 3] & 1) != 0, torch.remainder(x, meta[:, 1]),
                                torch.minimum(torch.clamp_min(x, 0), meta[:, 1] - 1))
                y = torch.where((meta[:, 3] & 2) != 0, torch.remainder(y, meta[:, 2]),
                                torch.minimum(torch.clamp_min(y, 0), meta[:, 2] - 1))
                weight = (frac[:, 0] if dx else 1 - frac[:, 0]) * (frac[:, 1] if dy else 1 - frac[:, 1])
                result = result + self.atlas[meta[:, 0] + y * meta[:, 1] + x] * weight[:, None]
        return result

    def input_features(self, samples, meta=None, values=None):
        if meta is None:
            _, meta = self._chart_info(samples)
        dev = self.chart_meta.device
        get = lambda key: _sample_field(samples, key, dev)
        derivatives = get("uv_derivatives") * meta[:, [1, 2, 1, 2]]
        derivatives = derivatives.clamp(-DERIVATIVE_CLIP, DERIVATIVE_CLIP) / DERIVATIVE_CLIP
        reflection = get("reflection")
        roughness = get("roughness").reshape(-1, 1)
        ndotv = get("ndotv").reshape(-1, 1).clamp(0, 1)
        if self.mode == "local":
            if values is None:
                values = self.sample_atlas(get("uv"), meta)
            return torch.cat((values[:, :8], reflection, roughness, ndotv, derivatives), dim=1)
        if self.mode != "global":
            raise ValueError("only local/global have neural input features")
        mn = torch.as_tensor(BBOX_MIN, device=dev)
        mx = torch.as_tensor(BBOX_MAX, device=dev)
        position = 2 * (get("position") - mn) / (mx - mn) - 1
        blocks = [position]
        for band in (1, 2):
            phase = position * np.float32(math.pi * band)
            blocks.extend((torch.sin(phase), torch.cos(phase)))
        blocks.extend((get("normal"), reflection, roughness,
                       get("metallic").reshape(-1, 1), ndotv, derivatives))
        return torch.cat(blocks, dim=1)

    def forward(self, samples):
        hit, meta = self._chart_info(samples)
        if self.mode == "global":
            logits = self.layers(self.input_features(samples, meta))
        else:
            values = self.sample_atlas(samples["uv"], meta)
            if self.mode == "texture":
                logits = values
            elif self.mode == "directional":
                reflection = _sample_field(samples, "reflection", self.chart_meta.device)
                coefficients = values.reshape(-1, 3, 4)
                logits = coefficients[:, :, 0] + torch.sum(coefficients[:, :, 1:] * reflection[:, None, :], dim=2)
            else:
                logits = values[:, 8:11] + self.layers(self.input_features(samples, meta, values))
        # F.softplus has the correct derivative at zero, unlike a naive
        # max(x,0)+log1p(exp(-abs(x))) autograd expression at that point.
        return F.softplus(logits) * hit[:, None]


def resolve_samples(rgb, hit, incident, return_linear=False):
    """Resolve four linear samples, transfer once, then apply center incident gate."""
    if rgb.ndim != 3 or rgb.shape[1:] != (4, 3):
        raise ValueError("rgb must be [pixels,4,3]")
    h = torch.as_tensor(hit, device=rgb.device, dtype=rgb.dtype)
    if h.shape != rgb.shape[:2]:
        raise ValueError("hit must be [pixels,4]")
    gate = torch.as_tensor(incident, device=rgb.device, dtype=rgb.dtype).reshape(-1, 1)
    if len(gate) != len(rgb):
        raise ValueError("incident length mismatch")
    alpha = h.mean(dim=1, keepdim=True)
    associated = (rgb * h[:, :, None]).mean(dim=1)
    straight = torch.clamp_min(associated, 0) / alpha.clamp_min(1e-8)
    # Empty-pixel straight=0 and gamma<1 otherwise yield an infinite derivative
    # at zero. The positive clamp gives exactly zero through the alpha mask.
    mapped = (straight / (1 + straight)).clamp_min(1e-20).pow(1 / 2.2)
    rgba = torch.cat((alpha * mapped, alpha), dim=1) * gate
    if return_linear:
        linear = torch.cat((associated, alpha), dim=1) * gate
        return rgba, linear
    return rgba


def resolve_numpy(rgb, hit, incident, return_linear=False):
    rgb = np.asarray(rgb, dtype=np.float32)
    h = np.asarray(hit, dtype=np.float32)
    if rgb.ndim != 3 or rgb.shape[1:] != (4, 3) or h.shape != rgb.shape[:2]:
        raise ValueError("expected RGB[pixels,4,3] and hit[pixels,4]")
    gate = np.asarray(incident, dtype=np.float32).reshape(-1, 1)
    if len(gate) != len(rgb):
        raise ValueError("incident length mismatch")
    alpha = h.mean(1, keepdims=True)
    associated = (rgb * h[:, :, None]).mean(1)
    straight = np.maximum(associated, 0) / np.maximum(alpha, np.float32(1e-8))
    mapped = np.power(straight / (1 + straight), np.float32(1 / 2.2))
    rgba = np.concatenate((alpha * mapped, alpha), axis=1) * gate
    if return_linear:
        return rgba, np.concatenate((associated, alpha), axis=1) * gate
    return rgba


def orient_surface_normals(normals, geometric_normals, view):
    """NumPy geometry helper: Ng-based flip; raw-N reflection, no BSDF correction."""
    n = np.asarray(normals, dtype=np.float32)
    ng = np.asarray(geometric_normals, dtype=np.float32)
    v = np.asarray(view, dtype=np.float32)
    unit = lambda a: a / np.maximum(np.linalg.norm(a, axis=-1, keepdims=True), np.float32(1e-20))
    n, ng, v = unit(n), unit(ng), unit(v)
    flip = np.where(np.sum(ng * v, axis=-1, keepdims=True) < 0, -1, 1).astype(np.float32)
    n, ng = n * flip, ng * flip
    nv = np.sum(n * v, axis=-1, keepdims=True)
    reflection = unit(2 * nv * n - v)
    return n, ng, reflection, np.clip(nv[:, 0], 0, 1)


def fnv1a(raw):
    value = 2166136261
    for byte in raw:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def _sha_bytes(value):
    try:
        raw = bytes.fromhex(value)
    except (TypeError, ValueError) as exc:
        raise ValueError("SHA256 must be a 64-character hexadecimal string") from exc
    if len(raw) != 32:
        raise ValueError("SHA256 must contain 32 bytes")
    return raw


def _flat_decoder(model):
    if model.layers is None:
        return np.empty(0, dtype="<f4")
    return np.concatenate([p.detach().cpu().numpy().ravel()
                           for layer in (model.layers[0], model.layers[2], model.layers[4])
                           for p in (layer.weight, layer.bias)]).astype("<f4")


def write_asset(path, model, geometry_sha, scene_sha):
    mode_id, channels, stride, hidden, inputs, weights = MODES[model.mode]
    meta = _validate_chart_meta(model.chart_meta.detach().cpu().numpy(),
                                model.texel_count if stride else 16384)
    padded = np.zeros((model.texel_count, stride), dtype="<f4")
    if channels:
        padded[:, :channels] = model.atlas.detach().cpu().numpy()
    decoder = _flat_decoder(model)
    if len(decoder) != weights or not np.isfinite(padded).all() or not np.isfinite(decoder).all():
        raise ValueError("invalid model parameters")
    expected_learned = model.texel_count * channels + weights
    if model.learned_parameter_count() != expected_learned:
        raise ValueError("model parameter count differs from format")
    chart_bytes = meta.tobytes()
    payload = chart_bytes + padded.tobytes() + decoder.tobytes()
    header = bytearray(HEADER_BYTES)
    header[:8] = MAGIC
    struct.pack_into("<14I", header, 8, HEADER_BYTES, mode_id, model.texel_count,
                     channels, stride, hidden, inputs, weights, len(meta), 1, 1, 1, 1, 0)
    header[64:96] = _sha_bytes(geometry_sha)
    header[96:128] = _sha_bytes(scene_sha)
    header[128:160] = hashlib.sha256(chart_bytes).digest()
    struct.pack_into("<7fIQQ", header, 160, *BBOX_MIN, *BBOX_MAX,
                     DERIVATIVE_CLIP, 0, len(payload), expected_learned)
    raw = header + payload
    struct.pack_into("<I", raw, 60, fnv1a(raw))
    Path(path).write_bytes(raw)
    asset = load_asset(path, geometry_sha, scene_sha)
    return {k: v for k, v in asset.items() if k not in ("chart_meta", "atlas", "decoder_weights")}


def load_asset(path, expected_geometry_sha=None, expected_scene_sha=None):
    raw = Path(path).read_bytes()
    if len(raw) < HEADER_BYTES or raw[:8] != MAGIC:
        raise ValueError("truncated or unknown NLSF asset")
    (header, mode_id, texels, channels, stride, hidden, inputs, weights, charts,
     feature, resolve, transfer, flags, checksum) = struct.unpack_from("<14I", raw, 8)
    candidates = [name for name, spec in MODES.items() if spec[0] == mode_id]
    if not candidates:
        raise ValueError("unknown mode ID")
    mode = candidates[0]
    if (mode_id, channels, stride, hidden, inputs, weights) != MODES[mode]:
        raise ValueError("model shape/stride contract mismatch")
    if header != HEADER_BYTES or (feature, resolve, transfer, flags) != (1, 1, 1, 1):
        raise ValueError("unsupported feature/resolve/transfer/flags contract")
    if (mode == "global" and texels != 0) or (mode != "global" and not 0 < texels <= 262144):
        raise ValueError("invalid texel budget")
    if charts == 0 or any(raw[208:256]) or struct.unpack_from("<I", raw, 188)[0] != 0:
        raise ValueError("invalid chart count or nonzero reserved header")
    bbox = np.frombuffer(raw, dtype="<f4", count=6, offset=160)
    if not np.array_equal(bbox, np.concatenate((BBOX_MIN, BBOX_MAX))):
        raise ValueError("unsupported geometry normalization")
    if struct.unpack_from("<f", raw, 184)[0] != DERIVATIVE_CLIP:
        raise ValueError("unsupported derivative normalization")
    payload_size, learned = struct.unpack_from("<QQ", raw, 192)
    expected_size = charts * 16 + texels * stride * 4 + weights * 4
    if payload_size != expected_size or len(raw) != HEADER_BYTES + expected_size:
        raise ValueError("payload/file length mismatch")
    if learned != texels * channels + weights:
        raise ValueError("learned-parameter count mismatch")
    check = bytearray(raw)
    check[60:64] = b"\0" * 4
    if fnv1a(check) != checksum:
        raise ValueError("whole-file FNV1a mismatch")
    geometry_sha, scene_sha, layout_sha = raw[64:96].hex(), raw[96:128].hex(), raw[128:160].hex()
    if expected_geometry_sha is not None and geometry_sha != _sha_bytes(expected_geometry_sha).hex():
        raise ValueError("geometry fingerprint mismatch")
    if expected_scene_sha is not None and scene_sha != _sha_bytes(expected_scene_sha).hex():
        raise ValueError("scene fingerprint mismatch")
    chart_bytes = raw[HEADER_BYTES:HEADER_BYTES + charts * 16]
    if hashlib.sha256(chart_bytes).hexdigest() != layout_sha:
        raise ValueError("chart layout SHA256 mismatch")
    meta = _validate_chart_meta(np.frombuffer(chart_bytes, dtype="<u4").reshape(charts, 4),
                                texels if stride else 16384)
    floats = np.frombuffer(raw, dtype="<f4", offset=HEADER_BYTES + charts * 16).copy()
    if not np.isfinite(floats).all():
        raise ValueError("nonfinite stored appearance parameters")
    atlas = floats[:texels * stride].reshape(texels, stride)
    if stride > channels and np.any(atlas[:, channels:] != 0):
        raise ValueError("stored padding channels must be zero")
    return dict(format="NLSF0001", mode=mode, mode_id=mode_id, texels=texels,
                learned_channels=channels, stored_stride=stride, hidden_width=hidden,
                decoder_input=inputs, decoder_weight_count=weights, chart_count=charts,
                learned_param_count=int(learned), payload_bytes=int(payload_size),
                asset_bytes=len(raw), asset_sha256=hashlib.sha256(raw).hexdigest(),
                geometry_sha256=geometry_sha, scene_sha256=scene_sha, layout_sha256=layout_sha,
                fnv1a=f"{checksum:08x}", chart_meta=meta.copy(), atlas=atlas,
                decoder_weights=floats[texels * stride:])


def model_from_asset(asset):
    model = SurfaceModel(asset["mode"], asset["chart_meta"], asset["texels"])
    with torch.no_grad():
        if model.atlas is not None:
            model.atlas.copy_(torch.from_numpy(asset["atlas"][:, :MODES[model.mode][1]].copy()))
        offset = 0
        if model.layers is not None:
            for layer in (model.layers[0], model.layers[2], model.layers[4]):
                for parameter in (layer.weight, layer.bias):
                    size = parameter.numel()
                    values = asset["decoder_weights"][offset:offset + size].reshape(tuple(parameter.shape))
                    parameter.copy_(torch.from_numpy(values.copy()))
                    offset += size
        if offset != len(asset["decoder_weights"]):
            raise ValueError("decoder packing mismatch")
    return model.eval()


def _numpy_batch(asset, samples):
    get = lambda key: np.asarray(samples[key], dtype=np.float32)
    hit = np.asarray(samples["hit"], dtype=bool).reshape(-1)
    ids = np.asarray(samples["chart_id"], dtype=np.int64).reshape(-1)
    if ids.shape != hit.shape or np.any(hit & ((ids < 0) | (ids >= len(asset["chart_meta"])))):
        raise ValueError("invalid active chart IDs")
    meta = asset["chart_meta"][np.where(hit, ids, 0)].astype(np.int64)
    mode = asset["mode"]
    values = None
    if mode != "global":
        uv = get("uv")
        xy = uv * meta[:, 1:3].astype(np.float32) - np.float32(.5)
        ij = np.floor(xy).astype(np.int64)
        frac = xy - ij.astype(np.float32)
        values = np.zeros((len(hit), asset["stored_stride"]), dtype=np.float32)
        for dy in (0, 1):
            for dx in (0, 1):
                x, y = ij[:, 0] + dx, ij[:, 1] + dy
                x = np.where(meta[:, 3] & 1, x % meta[:, 1], np.clip(x, 0, meta[:, 1] - 1))
                y = np.where(meta[:, 3] & 2, y % meta[:, 2], np.clip(y, 0, meta[:, 2] - 1))
                w = (frac[:, 0] if dx else 1 - frac[:, 0]) * (frac[:, 1] if dy else 1 - frac[:, 1])
                values += asset["atlas"][meta[:, 0] + y * meta[:, 1] + x] * w[:, None]
    if mode == "texture":
        logits = values[:, :3]
    elif mode == "directional":
        c = values.reshape(-1, 3, 4)
        logits = c[:, :, 0] + np.sum(c[:, :, 1:] * get("reflection")[:, None, :], axis=2)
    else:
        derivatives = get("uv_derivatives") * meta[:, [1, 2, 1, 2]].astype(np.float32)
        derivatives = np.clip(derivatives, -DERIVATIVE_CLIP, DERIVATIVE_CLIP) / np.float32(DERIVATIVE_CLIP)
        ndotv = np.clip(get("ndotv").reshape(-1, 1), 0, 1)
        roughness = get("roughness").reshape(-1, 1)
        if mode == "local":
            features = np.concatenate((values[:, :8], get("reflection"), roughness, ndotv, derivatives), axis=1)
        else:
            position = 2 * (get("position") - BBOX_MIN) / (BBOX_MAX - BBOX_MIN) - 1
            blocks = [position]
            for band in (1, 2):
                phase = position * np.float32(math.pi * band)
                blocks.extend((np.sin(phase), np.cos(phase)))
            blocks.extend((get("normal"), get("reflection"), roughness,
                           get("metallic").reshape(-1, 1), ndotv, derivatives))
            features = np.concatenate(blocks, axis=1)
        x = features
        offset = 0
        dims = (asset["decoder_input"], asset["hidden_width"], asset["hidden_width"], 3)
        for index, (nin, nout) in enumerate(zip(dims[:-1], dims[1:])):
            count = nin * nout
            weight = asset["decoder_weights"][offset:offset + count].reshape(nout, nin)
            offset += count
            bias = asset["decoder_weights"][offset:offset + nout]
            offset += nout
            x = x @ weight.T + bias
            if index < 2:
                x = np.maximum(x, 0)
        logits = x + values[:, 8:11] if mode == "local" else x
    result = np.maximum(logits, 0) + np.log1p(np.exp(-np.abs(logits)))
    result[~hit] = 0
    return result.astype(np.float32)


def decode_numpy(asset, samples, batch_size=8192):
    if batch_size <= 0:
        raise ValueError("batch_size must be positive")
    n = len(samples["hit"])
    result = np.empty((n, 3), dtype=np.float32)
    for start in range(0, n, batch_size):
        end = min(start + batch_size, n)
        result[start:end] = _numpy_batch(asset, {k: v[start:end] for k, v in samples.items()})
    return result


def glsl_source(mode):
    """GLSL450 helpers; caller supplies scalar macros and GPU buffer bindings.

    sfShade(chart,uv,P,N,R,roughness,metallic,ndotv,rawDerivatives,hit)->linearRGB.
    sfResolve(rgb[4],hit[4],centerIncident)->display associated RGBA.
    Buffer macros: SF_CHART(chartID) reads uvec4(offset,W,H,wrapflags);
    SF_ATLAS(scalarIndex) and SF_WEIGHT(scalarIndex) read FP32 payload scalars.
    Header validation belongs to the caller and must precede resource use.
    """
    mode = canonical_mode(mode)
    _, channels, stride, hidden, inputs, _ = MODES[mode]
    common = r'''
// Canonical NLSF0001 helper contract; generated by surface_model.py.
// Define SF_CHART(i), SF_ATLAS(i), SF_WEIGHT(i) before this helper text.
float sfSoftplus(float x) {
    float z=exp(-abs(x));
    float tail=(z<0.0001) ? z*(1.0-0.5*z) : log(1.0+z);
    return max(x,0.0)+tail;
}
vec3 sfSoftplus3(vec3 v) { return vec3(sfSoftplus(v.x),sfSoftplus(v.y),sfSoftplus(v.z)); }
int sfChartCoordinate(int x,int size,bool periodic) {
    return periodic ? ((x%size)+size)%size : clamp(x,0,size-1);
}
vec4 sfDerivativeFeatures(vec4 derivatives,uvec4 chart) {
    return clamp(derivatives*vec4(chart.y,chart.z,chart.y,chart.z),vec4(-8.0),vec4(8.0))/8.0;
}
void sfOrientNormals(inout vec3 N,inout vec3 Ng,vec3 V,out vec3 reflection,out float ndotv) {
    N=normalize(N); Ng=normalize(Ng); V=normalize(V);
    if(dot(Ng,V)<0.0) {N=-N;Ng=-Ng;}
    reflection=normalize(reflect(-V,N)); ndotv=clamp(dot(N,V),0.0,1.0);
}
vec4 sfResolve(vec3 rgb[4],bool hit[4],bool incident) {
    vec3 associated=vec3(0.0); float alpha=0.0;
    for(int i=0;i<4;i++) if(hit[i]) {associated+=rgb[i]*0.25;alpha+=0.25;}
    if(!incident || alpha==0.0) return vec4(0.0);
    vec3 straight=max(associated,vec3(0.0))/alpha;
    return vec4(alpha*pow(straight/(vec3(1.0)+straight),vec3(1.0/2.2)),alpha);
}
'''
    if mode != "global":
        common += f'''\nvoid sfAtlas(int chartID,vec2 uv,out float values[12]) {{
    uvec4 m=SF_CHART(chartID); vec2 xy=uv*vec2(m.y,m.z)-vec2(0.5);
    ivec2 ij=ivec2(floor(xy)); vec2 f=xy-vec2(ij);
    for(int c=0;c<12;c++) values[c]=0.0;
    for(int dy=0;dy<2;dy++) for(int dx=0;dx<2;dx++) {{
        int x=sfChartCoordinate(ij.x+dx,int(m.y),(m.w&1u)!=0u);
        int y=sfChartCoordinate(ij.y+dy,int(m.z),(m.w&2u)!=0u);
        float w=(dx==1?f.x:1.0-f.x)*(dy==1?f.y:1.0-f.y);
        int address=(int(m.x)+y*int(m.y)+x)*{stride};
        for(int c=0;c<{channels};c++) values[c]+=w*SF_ATLAS(address+c);
    }}
}}
'''
    common += '''\nvec3 sfShade(int chartID,vec2 uv,vec3 position,vec3 N,vec3 reflection,
             float roughness,float metallic,float ndotv,vec4 rawDerivatives,bool hit) {
    if(!hit) return vec3(0.0);
'''
    if mode != "global":
        common += "    float values[12]; sfAtlas(chartID,uv,values);\n"
    if mode == "texture":
        return common + "    return sfSoftplus3(vec3(values[0],values[1],values[2]));\n}\n"
    if mode == "directional":
        return common + '''    vec3 result;
    for(int c=0;c<3;c++) result[c]=values[c*4]+values[c*4+1]*reflection.x+
                                  values[c*4+2]*reflection.y+values[c*4+3]*reflection.z;
    return sfSoftplus3(result);
}
'''
    common += f"    float features[{inputs}];\n"
    common += "    vec4 d=sfDerivativeFeatures(rawDerivatives,SF_CHART(chartID));\n"
    if mode == "local":
        common += '''    for(int i=0;i<8;i++) features[i]=values[i];
    features[8]=reflection.x;features[9]=reflection.y;features[10]=reflection.z;
    features[11]=roughness;features[12]=clamp(ndotv,0.0,1.0);
    for(int i=0;i<4;i++) features[13+i]=d[i];
'''
    else:
        common += '''    vec3 p=2.0*(position-vec3(-174.0,-166.0,34.0))/vec3(286.0,332.0,268.0)-1.0;
    for(int i=0;i<3;i++) features[i]=p[i];
    for(int band=0;band<2;band++) {
        vec3 phase=p*(band==0?3.14159265358979323846:6.28318530717958647692);
        vec3 sn=sin(phase),cs=cos(phase);
        for(int i=0;i<3;i++) {features[3+band*6+i]=sn[i];features[6+band*6+i]=cs[i];}
    }
    for(int i=0;i<3;i++) {features[15+i]=N[i];features[18+i]=reflection[i];}
    features[21]=roughness;features[22]=metallic;features[23]=clamp(ndotv,0.0,1.0);
    for(int i=0;i<4;i++) features[24+i]=d[i];
'''
    b1 = hidden * inputs
    w2 = b1 + hidden
    b2 = w2 + hidden * hidden
    w3 = b2 + hidden
    b3 = w3 + 3 * hidden
    common += f'''    float a[{hidden}],b[{hidden}];
    for(int i=0;i<{hidden};i++) {{
        float sum=SF_WEIGHT({b1}+i);
        for(int j=0;j<{inputs};j++) sum+=SF_WEIGHT(i*{inputs}+j)*features[j];
        a[i]=max(sum,0.0);
    }}
    for(int i=0;i<{hidden};i++) {{
        float sum=SF_WEIGHT({b2}+i);
        for(int j=0;j<{hidden};j++) sum+=SF_WEIGHT({w2}+i*{hidden}+j)*a[j];
        b[i]=max(sum,0.0);
    }}
    vec3 result;
    for(int i=0;i<3;i++) {{
        float sum=SF_WEIGHT({b3}+i);
        for(int j=0;j<{hidden};j++) sum+=SF_WEIGHT({w3}+i*{hidden}+j)*b[j];
        result[i]=sum;
    }}
'''
    if mode == "local":
        common += "    result+=vec3(values[8],values[9],values[10]);\n"
    return common + "    return sfSoftplus3(result);\n}\n"
