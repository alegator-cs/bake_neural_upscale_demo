from pathlib import Path
import hashlib, struct, re
root=Path(__file__).resolve().parent
# Only the full build produces this receipt after compiling/validating shaders.
import json
receipt=json.loads((root/'shader_build_receipt.json').read_text(encoding='utf-8-sig'))
assert receipt['surface_flags_abi']==2
for category in ('source_sha256','spv_sha256'):
    for path,digest in receipt[category].items():
        assert hashlib.sha256((root/path).read_bytes()).hexdigest()==digest, ('Stale shader receipt',path)
assert len(receipt['spv_sha256'])==17
for file in ('resolve_directional.comp','resolve_small32.comp'):
    normalized=''.join((root/'surface'/file).read_text().split())
    assert '(frame.info.z&1u)!=0u' in normalized, 'Unsafe old diagnostics flag contract'
assert 'frame.info.z&2u' in ''.join((root/'surface/surface_geometry.glsl').read_text().split())

spv=root/'neural_mlp_slim.spv'; p=root/'quake3e/code/renderervk/shaders/spirv/shader_data.c';s=p.read_text()
b=spv.read_bytes();rows=['    '+','.join(map(str,b[i:i+16]))+',' for i in range(0,len(b),16)]
s,n=re.subn(r'const unsigned char neural_mlp_slim_comp_spv\[\] = \{.*?\};','const unsigned char neural_mlp_slim_comp_spv[] = {\n'+'\n'.join(rows)+'\n};',s,flags=re.S);assert n==1;p.write_text(s,newline='\n')
rows=['/* Exact canonical surface shaders plus immutable geometry BVH. */', '#define SURFACE_FLAGS_ABI 2', '#define SURFACE_FHD_ABI 1', f'#define SURFACE_SHADER_RECEIPT_SHA "{hashlib.sha256((root/"shader_build_receipt.json").read_bytes()).hexdigest()}"']
for file,name in [('visibility.vert.spv','visibility_vert'),('visibility.frag.spv','visibility_frag'),('resolve_directional.comp.spv','resolve_directional'),('resolve_small32.comp.spv','resolve_small32'),('visibility_firsthit.comp.spv','firsthit'),('resolve_small32_linear.comp.spv','resolve_small32_linear'),('upscale_light.comp.spv','upscale_light'),('upscale_light_fast.comp.spv','upscale_light_fast'),('upscale_bilinear_fast.comp.spv','upscale_bilinear_fast'),('visibility_firsthit_gated.comp.spv','firsthit_gated'),('upscale_pixel_residual.comp.spv','upscale_pixel_residual'),('upscale_pixel_bilinear.comp.spv','upscale_pixel_bilinear'),('upscale_cnn_display.comp.spv','upscale_cnn_display'),('upscale_cnn_conv0.comp.spv','upscale_cnn_conv0'),('upscale_cnn_conv1.comp.spv','upscale_cnn_conv1'),('upscale_cnn_head.comp.spv','upscale_cnn_head')]:
    b=(root/'surface'/file).read_bytes();v=struct.unpack('<'+'I'*(len(b)//4),b)
    rows += [f'static const uint32_t surface_{name}_spv[] = {{']
    rows += ['    '+','.join(str(x)+'U' for x in v[i:i+8])+',' for i in range(0,len(v),8)]
    rows += ['};']
b=(root.parent/'assets/visibility.bvh').read_bytes()
rows += [f'#define SURFACE_BVH_BYTES {len(b)}', f'#define SURFACE_BVH_SHA "{hashlib.sha256(b).hexdigest()}"']
(root/'quake3e/code/renderervk/surface_shader_data.h').write_text('\n'.join(rows)+'\n',newline='\n')
print('Embedded shaders; pinned BVH',len(b),'bytes')
