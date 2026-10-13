"""Portable, read-only dataset contracts. No device API or numerical model import."""
from pathlib import Path
import hashlib,json,struct,time
import numpy as np

ROOT=Path(__file__).parents[1]
SCENE='c844dfc2401c5fb3b8e30d9f4f375dbeca4fbb8d5aeb21ff66e6876bc2237f93'
GEOMETRY='657e3d200dcf36412bec2e4b3b3730737a5de07a8e8a49df6e2ba80bb9b1c9ce'
TIERS={'A480':([480,270],[1920,1080]),'B240':([240,135],[960,540]),'C128':([128,72],[512,288])}

def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb')as f:
        for data in iter(lambda:f.read(1048576),b''):h.update(data)
    return h.hexdigest()
def descriptor(path,container=None,dtype=None,shape=None):
    path=Path(path).resolve();row=dict(path=str(path),bytes=path.stat().st_size,sha256=digest(path))
    if container:row.update(container=container,dtype=dtype,shape=list(shape))
    return row
def binding(row,base,verify=False):
    path=Path(row['path']);path=path if path.is_absolute()else Path(base)/path;path=path.resolve()
    if not path.is_file()or path.stat().st_size!=row['bytes']:raise ValueError('Missing/wrong-sized input '+str(path))
    if verify and digest(path)!=row['sha256']:raise ValueError('SHA differs '+str(path))
    return path
def payload(row):
    path=Path(row['path']);shape=tuple(row['shape']);dtype=np.dtype(row['dtype'])
    if row['container']=='NPY':value=np.load(path,mmap_mode='r',allow_pickle=False)
    elif row['container']=='RAW':value=np.memmap(path,mode='r',dtype=dtype,shape=shape)
    else:raise ValueError('RAW/NPY only')
    if value.shape!=shape or value.dtype!=dtype:raise ValueError('Array header differs '+str(path))
    return value

def load_manifest(path,verify=False,purpose='a'):
    path=Path(path).resolve();manifest=read(path)
    if manifest.get('schema')!='C16_RELEASE_DATASET_1' or manifest.get('scene_sha256')!=SCENE:
        raise ValueError('Required release dataset schema/scene identity')
    if manifest.get('target_semantics')!='finite_native_Cycles512_associated_linear_and_display':
        raise ValueError('Native512spp teacher contract required; resized labels forbidden')
    admitted={r['view_id']:r for r in read(ROOT/'cameras/COHORT.json')['rows']}
    rows=manifest['views']
    if len(rows)!=408 or {r['view_id']for r in rows}!=set(admitted):raise ValueError('Require exact384TRAIN+24selection')
    for row in rows:
        known=admitted[row['view_id']]
        if row['role']!=known['split'] or row['resolution_tag']!=known['tier']:raise ValueError('Protected split/tier mismatch')
        if row.get('raw_first64_hex')!=known['raw_first64_hex']:raise ValueError('Actual camera bits mismatch')
        lw,lh=TIERS[row['resolution_tag']][0];hw,hh=TIERS[row['resolution_tag']][1]
        specs={'lr_linear':([lh,lw,4],'<f4'),'target_rgba':([hh,hw,4],'<f4')}
        if purpose=='a':specs.update(target_linear=([hh,hw,4],'<f4'),lr_ids=([lh*2,lw*2],'<i4'),
            lr_barycentric=([lh*2,lw*2,2],'<f4'),lr_gate=([lh,lw],'<u4'))
        for key,(shape,dtype)in specs.items():
            value=row[key]
            if value['shape']!=shape or value['dtype']!=dtype:raise ValueError('Native payload contract differs '+key)
            value['path']=str(binding(value,path.parent,verify))
        row['camera']=known['camera']
        if purpose=='c16':
            row['baseline_recipe']=dict(schema='NL_FULL384_A2_BASELINE_RECIPE_1',input=row['lr_linear'],shape=[hh,hw,4],dtype='<f4',
                CPU_reconstructed_display_sha256=row['CPU_reconstructed_display_sha256'])
    return manifest

class AData:
    def __init__(self,manifest,mesh):
        self.rows=sorted((r for r in manifest['views']if r['role']=='train'),key=lambda r:(r['view_id'],r['resolution_tag']))
        self.mesh=dict(np.load(mesh,allow_pickle=False));self.cache={}
    def sample(self,row,pixels):
        from a_numerics import reconstruct,reduce_linear_pixels,display_from_linear
        if self.cache.get('id')!=row['view_id']:
            for old in self.cache.get('arrays',{}).values():
                if isinstance(old,np.memmap):old._mmap.close()
            self.cache=dict(id=row['view_id'],arrays={k:payload(row[k])for k in ('lr_ids','lr_barycentric','lr_gate','target_linear')})
        arrays=self.cache['arrays'];w,h=TIERS[row['resolution_tag']][0];x,y=pixels%w,pixels//w
        xx=x[:,None]*2+np.arange(4)[None,:]%2;yy=y[:,None]*2+np.arange(4)[None,:]//2
        ids,bary=arrays['lr_ids'][yy,xx],arrays['lr_barycentric'][yy,xx]
        samples,bad=reconstruct(self.mesh,row['camera'],pixels,ids,bary,w,h)
        gate=np.asarray(arrays['lr_gate'][y,x],'bool');linear=reduce_linear_pixels(arrays['target_linear'],pixels,w,h)
        return samples,gate,display_from_linear(linear),bad
