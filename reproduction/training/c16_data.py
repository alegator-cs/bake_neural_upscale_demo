"""Arm A accepted-pair loader and target-only patch sampler; no device APIs.

Imported after explicit worker GO. Selection/holdout payloads are never opened.
Native ground truth only defines loss/sampling regions, never model features.
"""
from pathlib import Path
from collections import OrderedDict
import hashlib
import json
import time
import numpy as np



SHAPES = {"A480": ((270,480,4),(1080,1920,4)), "B240": ((135,240,4),(540,960,4)),
          "C128": ((72,128,4),(288,512,4))}
TIER_INDEX={"A480":0,"B240":1,"C128":2}


def digest(path):
    h=hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda:stream.read(1048576),b""):h.update(chunk)
    return h.hexdigest()


def descriptor(path):
    path=Path(path).resolve()
    return dict(path=str(path),bytes=path.stat().st_size,sha256=digest(path))


def auth(item):
    path=Path(item["path"]).resolve()
    if path.stat().st_size!=item["bytes"] or digest(path)!=item["sha256"]:
        raise ValueError("Input path/bytes/hash differs: "+str(path))
    return path


def json_file(item):
    return json.loads(auth(item).read_text(encoding="utf-8"))


def open_payload(item):
    if item.get('schema')=='NL_FULL384_A2_BASELINE_RECIPE_1':return DerivedBaseline(item)
    path=Path(item["path"])
    if item["container"]=="RAW":
        result=np.memmap(path,mode="r",dtype="<f4",shape=tuple(item["shape"]))
    elif item["container"]=="NPY":result=np.load(path,mmap_mode="r",allow_pickle=False)
    else:raise ValueError("Only authenticated native RAW/NPY")
    if result.dtype.str!="<f4" or list(result.shape)!=item["shape"]:raise ValueError("Payload header differs")
    return result


class DerivedBaseline:
    """RGBA baseline from actual LR only; no camera, geometry or HR rays.

    This is an explicit numerical recipe, never a pretend retained RAW file.
    Its exact all-native GPU-vs-CPU provenance is admitted before training.
    """
    def __init__(self,recipe):
        self.recipe=recipe;self.linear=open_payload(recipe['input']);self.shape=tuple(recipe['shape'])
        self.dtype=np.dtype('<f4')
        if self.linear.shape!=(self.shape[0]//4,self.shape[1]//4,4):raise ValueError('True4x actual-LR baseline extent')

    def __getitem__(self,key):
        if not isinstance(key,tuple)or len(key)!=2 or any(not isinstance(k,slice)or k.step not in(None,1)for k in key):raise ValueError('Contiguous native baseline rectangle only')
        ys,xs=key;h,w,_=self.shape;lh,lw,_=self.linear.shape
        y0,y1=0 if ys.start is None else ys.start,h if ys.stop is None else ys.stop
        x0,x1=0 if xs.start is None else xs.start,w if xs.stop is None else xs.stop
        if not 0<=x0<=x1<=w or not 0<=y0<=y1<=h:raise ValueError('Native baseline rectangle extent')
        x=(np.arange(x0,x1,dtype='f4')+.5)*np.float32(lw/w)-.5;cx=np.floor(x).astype('i4');fx=x-np.floor(x)
        yy=(np.arange(y0,y1,dtype='f4')+.5)*np.float32(lh/h)-.5;cy=np.floor(yy).astype('i4');fy=yy-np.floor(yy)
        bilinear=np.zeros((y1-y0,x1-x0,4),'f4')
        for phase in range(4):
            ox,oy=phase%2,phase//2;ix=np.clip(cx+ox,0,lw-1);iy=np.clip(cy+oy,0,lh-1)
            weight=((1-fx)if ox==0 else fx)[None,:]*((1-fy)if oy==0 else fy)[:,None]
            bilinear+=self.linear[iy[:,None],ix[None,:]]*weight[...,None]
        bilinear[...,:3]=np.maximum(bilinear[...,:3],0);bilinear[...,3]=np.clip(bilinear[...,3],0,1);bilinear[bilinear[...,3]<=1e-8,:3]=0
        return display_from_linear(bilinear)

    def native(self,deadline):
        h,w,_=self.shape;result=np.empty(self.shape,'<f4');digest_value=hashlib.sha256()
        for y in range(0,h,24):
            if time.monotonic()>=deadline:raise TimeoutError('CPU native baseline reconstruction counted in chunk')
            strip=self[y:min(y+24,h),0:w];result[y:y+len(strip)]=strip;digest_value.update(strip.astype('<f4',copy=False).tobytes())
        if digest_value.hexdigest()!=self.recipe['CPU_reconstructed_display_sha256']:raise ValueError('Actual retained baseline reconstruction SHA differs')
        return result


def display_from_linear(rgba):
    value=np.asarray(rgba,dtype="f4")
    alpha=np.clip(value[...,3:4],0,1)
    straight=np.maximum(value[...,:3],0)/np.maximum(alpha,np.float32(1e-8))
    rgb=alpha*np.power(straight/(1+straight),np.float32(1/2.2))
    return np.concatenate((rgb,alpha),axis=-1).astype("f4")


def check_rgba(value):
    if not np.isfinite(value).all() or np.any(value<0) or np.any(value>1) or np.any(value[...,:3]>value[...,3:4]+2e-6):
        raise ValueError("Display-associated RGBA bounds differ")


def boundary(mask):
    inner=mask.copy()
    inner[1:] &= mask[:-1];inner[:-1] &= mask[1:]
    inner[:,1:] &= mask[:,:-1];inner[:,:-1] &= mask[:,1:]
    return mask & ~inner


class TrainingPairs:
    def __init__(self,manifest,protocol,deadline,cohort):
        self.manifest,self.protocol,self.deadline=manifest,protocol,deadline
        self.upstream,self.upstream_sha=cohort['upstream'],cohort['upstream_sha256']
        self.rows=sorted(({**r,'base_rgba':r['baseline_recipe']}for r in manifest['views']if r['role']=='train'),key=lambda r:(r['view_id'],r['resolution_tag']))
        self.selection_rows=sorted((r for r in manifest['views']if r['role']=='selection'),key=lambda r:(r['view_id'],r['resolution_tag']))
        self.pools={};self.cache=OrderedDict();self.cache_limit=1
        for index,row in enumerate(self.rows):
            self.check_time()
            for key in('lr_linear','target_rgba'):auth(row[key]);self.check_time()
            self.pools[index]=self.make_pools(row);self.check_time()

    def selection_display(self,row):
        self.check_time()
        for key in('lr_linear','target_rgba'):auth(row[key]);self.check_time()
        linear=open_payload(row['lr_linear'])
        if not np.isfinite(linear).all()or np.any(linear[...,:3]<0)or np.any(linear[...,3]<0)or np.any(linear[...,3]>1):raise ValueError('Actual new-A LR linear domain differs')
        result=display_from_linear(linear);check_rgba(result);return result

    def selection_baseline(self,row):
        self.check_time();result=DerivedBaseline(row['baseline_recipe']).native(self.deadline);check_rgba(result);return result

    def check_time(self):
        if time.monotonic()>=self.deadline:raise TimeoutError("Arm A worker deadline")

    def make_pools(self,row):
        # One target at a time; compact LR masks retained, full teacher unmapped.
        value=open_payload(row["target_rgba"]);check_rgba(value)
        h,w=value.shape[:2];foreground=value[...,3]>1e-6
        luma=value[...,:3]@np.asarray((.2126,.7152,.0722),dtype="f4")
        bright=foreground & (luma>=.8)
        contour=boundary(value[...,3]>=.5)|boundary(bright)
        dx=np.mean((value[:,1:,:3]-value[:,:-1,:3])**2,axis=2)
        dy=np.mean((value[1:,:,:3]-value[:-1,:,:3])**2,axis=2)
        contour[:,:-1]|=dx>=.04**2;contour[:-1]|=dy>=.04**2
        pools={}
        for name,mask in (("foreground",foreground),("bright",bright),("contour",contour)):
            mask_lr=mask.reshape(h//4,4,w//4,4).any(axis=(1,3))
            indices=np.flatnonzero(mask_lr).astype("u4")
            if len(indices)>8192:
                # Deterministic bounded pool avoids384-view coordinate storage
                # exhausting the2GiB host cap; never affects inference inputs.
                seed=self.protocol["mask_pool_seed"] ^ row["view_id"] ^ (TIER_INDEX[row["resolution_tag"]]<<20) ^ (("foreground","bright","contour").index(name)<<24)
                choice=np.random.default_rng(seed).choice(len(indices),8192,replace=False)
                indices=indices[np.sort(choice)]
            pools[name]=indices
        return pools

    def lr_display(self,index):
        if index in self.cache:
            self.cache.move_to_end(index);return self.cache[index]
        linear=open_payload(self.rows[index]["lr_linear"])
        if not np.isfinite(linear).all() or np.any(linear[...,:3]<0) or np.any(linear[...,3]<0) or np.any(linear[...,3]>1):
            raise ValueError("Corrected native LR linear bounds differ")
        display=display_from_linear(linear);check_rgba(display)
        self.cache[index]=display
        while len(self.cache)>self.cache_limit:self.cache.popitem(last=False)
        return display

    def draw(self,rng,core=32,batch=8):
        # One camera/resolution perbatch; both candidates reset the same RNG.
        index=int(rng.integers(0,len(self.rows)));row=self.rows[index]
        display=self.lr_display(index);height,width=display.shape[:2]
        base=open_payload(row["base_rgba"]);target=open_payload(row["target_rgba"])
        tiles,bases,targets,origins,schedule=[],[],[],[],[]
        for _ in range(batch):
            branch=int(rng.integers(0,4));requested=("uniform","foreground","bright","contour")[branch]
            pool=self.pools[index].get(requested)
            effective=requested
            if requested!="uniform" and len(pool)==0:
                pool=self.pools[index]["foreground"];effective="foreground_fallback"
            if requested!="uniform" and len(pool):
                cy,cx=divmod(int(pool[int(rng.integers(0,len(pool)))]),width)
                x=int(np.clip(cx-int(rng.integers(0,core)),0,width-core))
                y=int(np.clip(cy-int(rng.integers(0,core)),0,height-core))
            else:
                effective="uniform";x=int(rng.integers(0,width-core+1));y=int(rng.integers(0,height-core+1))
                if rng.random()<.2:
                    edge=int(rng.integers(0,4))
                    if edge<2:x=0 if edge==0 else width-core
                    else:y=0 if edge==2 else height-core
            ix=np.clip(np.arange(x-4,x+core+4),0,width-1);iy=np.clip(np.arange(y-4,y+core+4),0,height-1)
            tiles.append(display[iy[:,None],ix[None,:]])
            bases.append(np.array(base[4*y:4*(y+core),4*x:4*(x+core)],copy=True))
            targets.append(np.array(target[4*y:4*(y+core),4*x:4*(x+core)],copy=True))
            origins.append((x,y));schedule.append((row["view_id"],TIER_INDEX[row["resolution_tag"]],x,y,branch,
                {"uniform":0,"foreground":1,"bright":2,"contour":3,"foreground_fallback":4}[effective]))
        pack=lambda rows:np.stack(rows).transpose(0,3,1,2).copy()
        result=(pack(tiles),pack(bases),pack(targets))
        for array in result:
            if not np.isfinite(array).all():raise ValueError("Nonfinite training patch")
        return (*result,origins,(height,width),schedule)
