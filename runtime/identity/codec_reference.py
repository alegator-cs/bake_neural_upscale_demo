"""Exact original byte validators; code checks only, not owner admission.

The original receipt-aware generators and their dependency graph are described
in docs/IDENTITY.md. This extracted module has no process, build or GPU calls.
"""
from __future__ import annotations
import hashlib
import re
import struct
from pathlib import Path
from types import SimpleNamespace

SCENE = "c844dfc2401c5fb3b8e30d9f4f375dbeca4fbb8d5aeb21ff66e6876bc2237f93"
GEOMETRY = "657e3d200dcf36412bec2e4b3b3730737a5de07a8e8a49df6e2ba80bb9b1c9ce"
MAX_TEXELS = 262144
MAX_BASE_BYTES = 16 * 1024**2
MAX_CORRECTION_BYTES = 8 * 1024**2

def sha(raw): return hashlib.sha256(raw).hexdigest()

def bounded(path, cap):
    path = Path(path)
    if path.stat().st_size > cap: raise ValueError(f"Oversized input: {path}")
    raw = path.read_bytes()
    if len(raw) > cap: raise ValueError("Input grew beyond cap")
    return raw

def pinned(path, row, cap):
    raw = bounded(path, cap)
    if len(raw) != row["bytes"] or sha(raw) != row["sha256"]:
        raise ValueError(f"File identity mismatch: {path}")
    return raw

def finite_words(raw, offset):
    if (len(raw) - offset) % 4: raise ValueError("Unaligned FP32 payload")
    if any((word[0] & 0x7f800000) == 0x7f800000 for word in struct.iter_unpack("<I", raw[offset:])):
        raise ValueError("Nonfinite atlas/decoder")

def charts(raw, count, texels):
    if count != 88 or len(raw) < 256 + 16 * count: raise ValueError("88 charts required")
    rows = list(struct.iter_unpack("<4I", raw[256:256+16*count]))
    intervals = []
    for start, width, height, flags in rows:
        end = start + width * height
        if not width or not height or flags > 3 or end > texels: raise ValueError("Invalid chart bounds/flags")
        intervals.append((start, end))
    intervals.sort()
    if any(b[0] < a[1] for a,b in zip(intervals, intervals[1:])): raise ValueError("Overlapping charts")
    return rows

def validate_pair(base, correction, original_rows=None, genuine=False):
    """Full byte/header/payload validator, independent of torch/Vulkan."""
    if len(base) < 256 or base[:8] != b"NLSF0001": raise ValueError("Base magic/header")
    h = struct.unpack_from("<13I", base, 8)
    texels = h[2]
    if h[:2] != (256,2) or not 0 < texels <= MAX_TEXELS or h[3:8] != (12,12,0,0,0):
        raise ValueError("Directional decoder shape/texel cap")
    if h[8] != 88 or h[9:13] != (1,1,1,1): raise ValueError("Chart count/resolve/feature flags")
    if any(base[208:256]) or struct.unpack_from("<I",base,188)[0]: raise ValueError("Reserved base fields")
    if struct.unpack_from("<7f",base,160) != (-174.,-166.,34.,112.,166.,302.,8.): raise ValueError("Normalization")
    expected_payload = 88*16+texels*12*4
    if struct.unpack_from("<QQ",base,192) != (expected_payload,texels*12) or len(base) != 256+expected_payload:
        raise ValueError("Base exact payload/file size")
    checksum = 2166136261
    for i,b in enumerate(base): checksum = ((checksum ^ (0 if 60<=i<64 else b))*16777619)&0xffffffff
    if checksum != struct.unpack_from("<I",base,60)[0]: raise ValueError("Base FNV")
    if base[64:96].hex()!=GEOMETRY or base[96:128].hex()!=SCENE: raise ValueError("Base physical scene identity")
    if sha(base[256:256+88*16]) != base[128:160].hex(): raise ValueError("Base chart digest")
    rows=charts(base,88,texels)
    finite_words(base,256+88*16)
    if genuine:
        if texels < 65536 or not original_rows: raise ValueError("B requires >=4x pooled texels")
        for i,(new,old) in enumerate(zip(rows,original_rows)):
            if new[1]<2*old[1] or new[2]<2*old[2] or new[3]!=old[3]:
                raise ValueError(f"B chart {i} must retain wrap flags and at least double each axis")
        if sum(r[1]*r[2] for r in rows)<4*sum(r[1]*r[2] for r in original_rows):
            raise ValueError("B requires >=4x USED chart texels, not merely pooled padding")
    if len(correction)<256 or correction[:8]!=b"NLSR0001": raise ValueError("Correction magic/header")
    hc=struct.unpack_from("<11I",correction,8)
    if hc != (256,1,texels,4,34,32,2275,88,1,1,1): raise ValueError("Native small32 correction shape")
    if any(correction[52:64]) or any(correction[208:256]): raise ValueError("Reserved correction fields")
    learned=texels*4+2275
    expected=88*16+4*learned
    if struct.unpack_from("<QQ",correction,192)!=(expected,learned) or len(correction)!=256+expected:
        raise ValueError("Correction exact payload/file size")
    if correction[64:96].hex()!=sha(base) or correction[96:128].hex()!=GEOMETRY or correction[128:160].hex()!=SCENE:
        raise ValueError("Correction base/scene/geometry binding")
    if sha(correction[256:])!=correction[160:192].hex(): raise ValueError("Correction payload digest")
    if charts(correction,88,texels)!=rows: raise ValueError("Base/correction chart mismatch")
    finite_words(correction,256+88*16)
    if len(base)+len(correction)>64*1024**2: raise ValueError("Combined asset memory cap")
    return dict(texels=texels,charts=rows,base_bytes=len(base),correction_bytes=len(correction),
                base_sha256=sha(base),correction_sha256=sha(correction),genuine=genuine)

v3 = SimpleNamespace(sha=sha, validate_pair=validate_pair, finite_words=finite_words)

def token(name):
    if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', name):
        raise ValueError('Identity name must be an ASCII token of1..64 characters')
    return name

def validate_coarse(base, correction, original_rows):
    row = v3.validate_pair(base, correction)
    if row['texels'] != 16384 or row['charts'] != original_rows:
        raise ValueError('Refitted A must retain all original16384 texels/chart records exactly')
    return row

def validate_cnn(raw, baseline_sha, code_sha, protocol_sha, pilot=False):
    if len(raw) < 256 or raw[:8] != b'NLR40001':
        raise ValueError('CNN magic/header')
    h = struct.unpack_from('<14I', raw, 8)
    width, parameters = h[3], h[8]
    if width not in ({8,16,32} if pilot else {16}):
        raise ValueError('New CNN must use the existing C16 architecture')
    if h != (256,1,1,width,4,64,3,4,9*width*width+102*width+64,1,1,1,1,1):
        raise ValueError('CNN decoder/feature/header shape')
    if len(raw) != 256 + 4*parameters or struct.unpack_from('<Q', raw,192)[0] != 4*parameters:
        raise ValueError('CNN exact file/payload bytes')
    if raw[64:96].hex() != baseline_sha or raw[96:128].hex() != code_sha or raw[128:160].hex() != protocol_sha:
        raise ValueError('CNN exact correction/source/protocol binding')
    if v3.sha(raw[256:]) != raw[160:192].hex() or struct.unpack_from('<2f',raw,200) != (1.,1.) or any(raw[208:256]):
        raise ValueError('CNN payload/scales/reserved fields')
    v3.finite_words(raw,256)
    return dict(width=width, parameters=parameters, bytes=len(raw), sha256=v3.sha(raw),
                baseline_sha256=baseline_sha, code_sha256=code_sha, protocol_sha256=protocol_sha)
