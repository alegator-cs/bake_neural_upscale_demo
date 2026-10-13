"""Portable native Cycles teacher adapter; no resize or display screenshots.

This adapts the accepted physical worker's camera, transport and export math.
The private owner/job protocol is not reproduced. Run only for an explicitly
chosen view, extent and output directory. Blender 3.6.0 is required.
"""
from pathlib import Path
import argparse, hashlib, json, math, struct, sys, time
import bpy
import numpy as np
from mathutils import Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from camera_contract import incident_mask

SCENE_SHA='c844dfc2401c5fb3b8e30d9f4f375dbeca4fbb8d5aeb21ff66e6876bc2237f93'
ACCEPTED_FILE_SHAS={SCENE_SHA,"8e98ac44c4dab9e52821b3bfff9a510b418f460fb99a9c3223e7829a1296c45c"}
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def display_rgba(linear):
    alpha=np.clip(linear[...,3:4],0,1)
    straight=np.maximum(linear[...,:3],0)/np.maximum(alpha,np.float32(1e-8))
    return np.concatenate((alpha*np.power(straight/(1+straight),np.float32(1/2.2)),alpha),-1).astype('<f4')

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--scene',type=Path,default=Path(__file__).with_name('indirect_alcove.blend'))
    p.add_argument('--cohort',type=Path,default=Path(__file__).parents[1]/'cameras/COHORT.json')
    p.add_argument('--view-id',type=int,required=True)
    p.add_argument('--frame',type=Path,help='Actual native96B runtime frame; preferred camera authority')
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--device',choices=('CPU','CUDA','OPTIX'),default='CPU')
    p.add_argument('--samples',type=int,default=512);p.add_argument('--seed',type=int,default=11)
    a=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--'in sys.argv else [])
    if a.out.exists():raise FileExistsError('Preserve old output; choose a fresh directory')
    if a.samples!=512:raise ValueError('Release recipe retains uniform512spp')
    if bpy.app.version_string!='3.6.0':raise RuntimeError('Use Blender3.6.0 for this frozen scene/normal contract')
    if digest(a.scene)not in ACCEPTED_FILE_SHAS:raise ValueError('Use the preserved exact scene for the released recipe')
    rows=json.loads(a.cohort.read_text())['rows'];row=next(x for x in rows if x['view_id']==a.view_id)
    camera=row['camera'];width,height=row['native_resolution'];camera_bytes=bytes.fromhex(row['raw_first64_hex'])
    if a.frame:
        raw=a.frame.read_bytes()
        if len(raw)!=96 or raw[:64]!=camera_bytes:raise ValueError('Frame camera must match frozen raw first64bytes')
        info=struct.unpack('<8I',raw[64:]);width,height=info[:2]
        if [width,height]!=row['native_resolution']:raise ValueError('Render native declared teacher extent')
    floats=struct.unpack('<16f',camera_bytes)
    for key,offset in [('origin',0),('forward',4),('right',8),('up',12)]:
        if list(floats[offset:offset+3])!=camera[key]:raise ValueError('Raw camera/vector disagreement')
    tx,ty=floats[7],floats[11]
    start=time.perf_counter();a.out.mkdir(parents=True)
    bpy.ops.wm.open_mainfile(filepath=str(a.scene.resolve()));scene=bpy.context.scene
    settings=dict(max_bounces=6,diffuse_bounces=4,glossy_bounces=4,transmission_bounces=0,
        volume_bounces=0,transparent_max_bounces=0,caustics_reflective=False,caustics_refractive=False,
        pixel_filter_type='BOX',sampling_pattern='TABULATED_SOBOL',use_light_tree=True)
    for key,expected in settings.items():
        if getattr(scene.cycles,key)!=expected:raise ValueError('Frozen transport differs: '+key)
    if scene.render.engine!='CYCLES' or not scene.render.film_transparent or scene.use_nodes:
        raise ValueError('Transparent native Cycles without compositor required')
    scene.render.resolution_x,scene.render.resolution_y=width,height;scene.render.resolution_percentage=100
    ratio=ty/(tx*height/width);scene.render.pixel_aspect_x=max(1.,1./ratio);scene.render.pixel_aspect_y=max(1.,ratio)
    scene.cycles.samples=a.samples;scene.cycles.seed=a.seed;scene.cycles.time_limit=0
    scene.cycles.use_adaptive_sampling=scene.cycles.use_denoising=False
    reflect=np.array([-1.,1.,1.]);origin=np.asarray(camera['origin'])*reflect+[988.,0.,0.]
    basis=np.column_stack([-np.asarray(camera['right'])*reflect,np.asarray(camera['up'])*reflect,-np.asarray(camera['forward'])*reflect])
    if abs(np.linalg.det(basis)-1)>2e-5:raise ValueError('Reflected basis is not proper')
    matrix=Matrix.Identity(4)
    for r in range(3):
        for c in range(3):matrix[r][c]=float(basis[r,c])
        matrix[r][3]=float(origin[r]/128.)
    scene.camera.matrix_world=matrix;scene.camera.data.lens=18./tx;scene.camera.data.clip_start=floats[15]/128.
    bpy.context.view_layer.update()
    if a.device=='CPU':
        scene.cycles.device='CPU';scene.render.threads_mode='FIXED';scene.render.threads=1
    else:
        preferences=bpy.context.preferences.addons['cycles'].preferences;preferences.compute_device_type=a.device
        preferences.get_devices();selected=[]
        for d in preferences.devices:
            d.use=d.type==a.device
            if d.use:selected.append(d.name)
        if len(selected)!=1:raise ValueError('Require one explicitly selected Cycles GPU')
        scene.cycles.device='GPU'
    events=dict(complete=False,cancel=False)
    def completed(_):events['complete']=True
    def cancelled(_):events['cancel']=True
    bpy.app.handlers.render_complete.append(completed);bpy.app.handlers.render_cancel.append(cancelled)
    try:operator=bpy.ops.render.render(write_still=False)
    finally:
        bpy.app.handlers.render_complete.remove(completed);bpy.app.handlers.render_cancel.remove(cancelled)
    if operator!={'FINISHED'} or not events['complete'] or events['cancel']:raise RuntimeError('Uniform teacher render incomplete')
    exr=a.out/'native_blender_orientation.exr';bpy.data.images['Render Result'].save_render(str(exr),scene=scene)
    image=bpy.data.images.load(str(exr),check_existing=False)
    if list(image.size)!=[width,height] or image.alpha_mode!='PREMUL':raise ValueError('Native premultiplied float EXR required')
    buffer=np.empty(width*height*4,'f4');image.pixels.foreach_get(buffer);bpy.data.images.remove(image)
    linear=buffer.reshape(height,width,4)[::-1,::-1].copy();del buffer
    mask=np.empty((height,width),'bool')
    for y0 in range(0,height,120):
        y1=min(y0+120,height)
        px,py=np.meshgrid((np.arange(width,dtype='f4')+.5)/width*2-1,1-(np.arange(y0,y1,dtype='f4')+.5)/height*2)
        f,r,u=(np.asarray(camera[k],'f4')for k in('forward','right','up'))
        rays=(f+r*tx*px[...,None]+u*ty*py[...,None]).reshape(-1,3);rays/=np.linalg.norm(rays,axis=1,keepdims=True)
        mask[y0:y1]=incident_mask(np.asarray(camera['origin'],'f4'),rays).reshape(y1-y0,width)
    linear*=mask[...,None];display=display_rgba(linear)
    if not np.isfinite(linear).all() or not np.isfinite(display).all():raise ValueError('Nonfinite teacher')
    for name,value in [('target.linear_rgba.npy',linear),('target.display_rgba.npy',display),('incident_mask.npy',mask)]:np.save(a.out/name,value,allow_pickle=False)
    receipt=dict(schema='C16_RELEASE_NATIVE_TEACHER_1',view_id=a.view_id,native_resolution=[width,height],samples=a.samples,
        seed=a.seed,adaptive_sampling=False,denoising=False,resized=False,alpha_mode='associated',display_extra_gamma=False,
        AA='uniform512 TABULATED_SOBOL BOX effective1pixel',receiver_gate='center20boxes/mirror494 applied once',
        reference_is_finite_Monte_Carlo=True,scene_sha256=SCENE_SHA,scene_file_sha256=digest(a.scene),raw_first64_hex=camera_bytes.hex(),
        blender_version=bpy.app.version_string,blender_build_hash=bpy.app.build_hash.decode(),elapsed_seconds=time.perf_counter()-start,
        files={f.name:dict(bytes=f.stat().st_size,sha256=digest(f))for f in a.out.iterdir()if f.is_file()})
    (a.out/'TEACHER.json').write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':main()
