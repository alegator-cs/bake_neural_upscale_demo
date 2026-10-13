"""Exact upstream A numeric functions; no tracing or device initialization.
Extracted from accepted 10,000-update fit sources. Training adapters are separate.
"""
import numpy as np
WIDTH,HEIGHT=480,270
OFFSETS=[[.25,.25],[.75,.25],[.25,.75],[.75,.75]]
def dot(a, b):
    product = np.asarray(a, 'f4') * np.asarray(b, 'f4')
    return (product[..., 0] + product[..., 1]) + product[..., 2]


def cross(a, b):
    return np.stack((a[..., 1]*b[..., 2]-a[..., 2]*b[..., 1],
                     a[..., 2]*b[..., 0]-a[..., 0]*b[..., 2],
                     a[..., 0]*b[..., 1]-a[..., 1]*b[..., 0]), -1).astype('f4')


def normalize(value):
    length = np.sqrt(dot(value, value)).astype('f4')
    return value / np.maximum(length[..., None], np.float32(1e-30))


def reconstruct(mesh, camera, pixels, triangle_ids, barycentric, width=WIDTH, height=HEIGHT):
    """Canonical same-triangle features from actual guides; no visibility queries.

    Uses native output dimensions, exact float32 tangents and quarter centers.
    Missing samples have zero features; coverage is never inferred from teacher.
    """
    pixels = np.asarray(pixels, 'i8')
    ids = np.asarray(triangle_ids).reshape(-1, 4)
    bary = np.asarray(barycentric, 'f4').reshape(-1, 4, 2)
    count = len(mesh['positions'])
    hit = (ids >= 0) & (ids < count)
    if np.any((ids != -1) & (ids != np.uint32(0xffffffff)) & ~hit):
        raise ValueError('Native guide contains out-of-range triangle ID')
    safe = np.where(hit, ids, 0).astype('i8')
    p, n, uv = mesh['positions'][safe], mesh['normals'][safe], mesh['uv'][safe]
    metadata = mesh['metadata'][safe]
    b1, b2 = bary[..., 0], bary[..., 1]
    b0 = (np.float32(1) - b1) - b2
    x = (pixels % width).astype('f4')[:, None] + np.asarray(OFFSETS, 'f4')[None, :, 0]
    y = (pixels // width).astype('f4')[:, None] + np.asarray(OFFSETS, 'f4')[None, :, 1]
    px = (x / np.float32(width)) * np.float32(2) - np.float32(1)
    py = np.float32(1) - (y / np.float32(height)) * np.float32(2)
    mirror = np.array([-1, 1, 1], 'f4')
    forward, right, up = (np.asarray(camera[k], 'f4') * mirror for k in ('forward', 'right', 'up'))
    tx, ty = np.float32(camera['tan_half_fov_x']), np.float32(camera['tan_half_fov_y'])
    q = (forward + right * (px*tx)[..., None]) + up * (py*ty)[..., None]
    view = normalize(q) * np.float32(-1)
    e1, e2 = p[..., 1, :] - p[..., 0, :], p[..., 2, :] - p[..., 0, :]
    raw_ng = cross(e1, e2)
    ng = normalize(raw_ng)
    normal = normalize((n[..., 0, :]*b0[..., None] + n[..., 1, :]*b1[..., None]) + n[..., 2, :]*b2[..., None])
    sign = np.where(dot(ng, view) < 0, np.float32(-1), np.float32(1))[..., None]
    ng, normal = ng * sign, normal * sign
    ndotv = dot(normal, view)
    reflection = normalize(normal*(np.float32(2)*ndotv)[..., None] - view)
    interpolated_uv = (uv[..., 0, :]*b0[..., None] + uv[..., 1, :]*b1[..., None]) + uv[..., 2, :]*b2[..., None]
    origin = np.asarray(camera['origin'], 'f4').copy()
    origin[0] = np.float32(988) - origin[0]
    denominator, area = dot(ng, q), dot(raw_ng, raw_ng)
    valid = hit & np.isfinite(denominator) & (denominator != 0) & (area >= np.float32(1e-15))
    denominator = np.where(valid, denominator, np.float32(1))
    inverse_area = np.float32(1) / np.where(valid, area, np.float32(1))
    dual1, dual2 = cross(e2, raw_ng)*inverse_area[..., None], cross(raw_ng, e1)*inverse_area[..., None]
    grad_u = dual1*(uv[..., 1, 0]-uv[..., 0, 0])[..., None] + dual2*(uv[..., 2, 0]-uv[..., 0, 0])[..., None]
    grad_v = dual1*(uv[..., 1, 1]-uv[..., 0, 1])[..., None] + dual2*(uv[..., 2, 1]-uv[..., 0, 1])[..., None]
    qx = right*((np.float32(2)*tx)/np.float32(width))
    qy = up*((np.float32(-2)*ty)/np.float32(height))
    plane_t = dot(ng, p[..., 0, :]-origin) / denominator
    dpdx = (qx-q*(dot(ng, qx)/denominator)[..., None])*plane_t[..., None]
    dpdy = (qy-q*(dot(ng, qy)/denominator)[..., None])*plane_t[..., None]
    derivative = np.stack((dot(grad_u, dpdx), dot(grad_v, dpdx), dot(grad_u, dpdy), dot(grad_v, dpdy)), -1)
    valid &= np.isfinite(derivative).all(-1)
    derivative = np.where(valid[..., None], derivative, np.float32(0)).astype('f4')
    roughness = metadata[..., 3].copy().view('f4')
    result = {'hit': hit, 'chart_id': np.where(hit, metadata[..., 0], 0).astype('i8'),
              'uv': interpolated_uv, 'normal': normal, 'reflection': reflection,
              'roughness': roughness, 'ndotv': np.clip(ndotv, 0, 1), 'uv_derivatives': derivative}
    for key in ('uv', 'normal', 'reflection', 'roughness', 'ndotv', 'uv_derivatives'):
        value = result[key]
        result[key] = np.where(hit[(...,)+(None,)*(value.ndim-hit.ndim)], value, np.float32(0)).astype('f4')
        if not np.isfinite(result[key]).all():
            raise ValueError('Nonfinite reconstructed native feature: ' + key)
    return result, int((hit & ~valid).sum())


def display_from_linear(value):
    value = np.asarray(value, 'f4')
    alpha = np.clip(value[...,3:4], np.float32(0), np.float32(1))
    straight = np.maximum(value[...,:3], np.float32(0)) / np.maximum(alpha, np.float32(1e-8))
    exponent = np.asarray([0x3ee8ba2f], '<u4').view('<f4')[0]
    rgb = alpha * np.power(straight / (np.float32(1) + straight), exponent)
    return np.concatenate((rgb, alpha), -1).astype('f4')


def reduce_linear_pixels(source, pixels, width, height):
    """Mean 16 associated float32 samples; apply no transfer inside reduction.

    Explicit row-major float32 additions are the recorded deterministic policy.
    No unassociation, alpha weighting or additional physical gate is introduced.
    """
    if source.shape != (height*4, width*4, 4) or source.dtype != np.dtype('<f4'):
        raise ValueError('Exact native 4x associated linear teacher required')
    pixels = np.asarray(pixels, 'i8')
    if pixels.ndim != 1 or np.any(pixels<0) or np.any(pixels>=width*height):
        raise ValueError('LR pixel index out of extent')
    x,y = pixels%width, pixels//width
    result = np.zeros((len(pixels),4), 'f4')
    for dy in range(4):
        for dx in range(4):
            block = np.asarray(source[y*4+dy,x*4+dx], 'f4')
            if not np.isfinite(block).all() or np.any(block[:,:3]<-1e-6) or np.any(block[:,3]<-1e-6) or np.any(block[:,3]>1.000001):
                raise ValueError('Physical linear teacher domain differs')
            if np.any((block[:,3]<=0) & (np.max(np.abs(block[:,:3]),1)>1e-6)):
                raise ValueError('Nonzero RGB at zero alpha is not associated physical RGBA')
            result += block
    return result * np.float32(1/16)


def torch_neighbors(torch,atlas,meta,samples,device):
    """Batch-local GPU tensors and outputs; no copied original CPU-only helper."""
    hit=torch.as_tensor(samples['hit'],device=device,dtype=torch.bool)
    chart=torch.as_tensor(samples['chart_id'],device=device,dtype=torch.long)
    chart=torch.where(hit,chart,0)
    uv=torch.as_tensor(samples['uv'],device=device,dtype=torch.float32)
    m=meta[chart]
    xy=uv*m[...,1:3]-.5;ij=torch.floor(xy).long();fraction=xy-ij
    output=torch.zeros((*uv.shape[:-1],atlas.shape[-1]),device=device,dtype=atlas.dtype)
    for dy in (0,1):
        for dx in (0,1):
            x,y=ij[...,0]+dx,ij[...,1]+dy
            x=torch.where((m[...,3]&1)!=0,torch.remainder(x,m[...,1]),torch.minimum(torch.clamp_min(x,0),m[...,1]-1))
            y=torch.where((m[...,3]&2)!=0,torch.remainder(y,m[...,2]),torch.minimum(torch.clamp_min(y,0),m[...,2]-1))
            weight=(fraction[...,0] if dx else 1-fraction[...,0])*(fraction[...,1] if dy else 1-fraction[...,1])
            output=output+atlas[m[...,0]+y*m[...,1]+x]*weight[...,None]
    return output,hit,m


def predict(torch,surface,model,samples,gate,stage):
    device=model.atlas.device
    reflection=torch.as_tensor(samples['reflection'],device=device,dtype=torch.float32)
    if stage=='directional':
        values,hit,meta=torch_neighbors(torch,model.atlas,model.chart_meta,samples,device)
    else:
        values,hit,meta=torch_neighbors(torch,model.baseline_atlas,model.baseline_chart_meta,samples,device)
    coefficient=values.reshape(*values.shape[:-1],3,4)
    logits=coefficient[...,0]+(coefficient[...,1:]*reflection[...,None,:]).sum(-1)
    if stage=='correction':
        local,_,_=torch_neighbors(torch,model.atlas,model.chart_meta,samples,device)
        color=torch.nn.functional.softplus(logits)
        derivative=torch.as_tensor(samples['uv_derivatives'],device=device,dtype=torch.float32)
        derivative=(derivative*meta[...,[1,2,1,2]]).clamp(-8,8)/8
        roughness=torch.as_tensor(samples['roughness'],device=device,dtype=torch.float32)[...,None]
        ndotv=torch.as_tensor(samples['ndotv'],device=device,dtype=torch.float32).clamp(0,1)[...,None]
        blocks=[local,color/(1+color),reflection,roughness,ndotv,derivative]
        for band in (1,2,4):
            phase=reflection*np.float32(np.pi*band)
            blocks.extend((torch.sin(phase),torch.cos(phase)))
        logits=logits+model.layers(torch.cat(blocks,-1))
    rgb=torch.nn.functional.softplus(logits)*hit[...,None]
    return surface.resolve_samples(rgb,hit,torch.as_tensor(gate,device=device,dtype=torch.bool))

