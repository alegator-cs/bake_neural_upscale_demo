"""Source-only 408-view mode9 capture packet builder. Launches nothing.

Checks the packaged camera table and mixed-resolution source path before writing
52 small eight-view configs and a plan. Execution is a separate explicit action.
"""
from pathlib import Path
import argparse,hashlib,json,re,struct

ROOT=Path(__file__).resolve().parents[1]
TIERS={'A480':((480,270),(1920,1080)),'B240':((240,135),(960,540)),'C128':((128,72),(512,288))}
COUNTS={'A480':{'train':96,'selection':12},'B240':{'train':160,'selection':6},'C128':{'train':128,'selection':6}}
SOURCE_NAMES=('vk_surface_resources.inc','vk_surface_adapter.inc','vk_surface_multires_capture.inc','vk_surface_cnn_capture.inc','overnight_camera_table.h','surface_batch_cameras.h','surface_batch_identity.h')
def digest(raw):return hashlib.sha256(raw).hexdigest()
def load(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def source_check(source,rows):
    folder=source/'code/renderervk';text={n:(folder/n).read_text()for n in SOURCE_NAMES}
    tables={}
    for ident,words in re.findall(r'\{(\d+),\s*\{([^}]+)\}\}',text['overnight_camera_table.h']):
        parsed=[int(x,16)for x in re.findall(r'0x([0-9a-fA-F]{8})u',words)]
        if len(parsed)==16:tables[int(ident)]=struct.pack('<16I',*parsed).hex()
    for row in rows:
        if tables.get(row['view_id'])!=row['raw_first64_hex']:raise ValueError('Missing/different actual camera bits '+str(row['view_id']))
    resources=text['vk_surface_resources.inc'];adapter=text['vk_surface_adapter.inc'];multires=text['vk_surface_multires_capture.inc']
    for marker in ['width==1920u && height==1080u','width==960u && height==540u','width==512u && height==288u',
        'r_surfaceUpscale->integer==9','s->lrWidth=s->width/4u;s->lrHeight=s->height/4u']:
        if marker not in resources:raise ValueError('Packaged source lacks exact mixedtier path: '+marker)
    for marker in ['r_surfaceUpscale->integer==9) {surface_multires_finish_capture();return;}',
        'r_surfaceUpscale->integer>=10) {surface_cnn_finish_capture();return;}']:
        if marker not in adapter:raise ValueError('Packaged capture routing differs')
    for marker in ['s->fhdLastMode!=9','NL4X_MULTIRES_RUNTIME_CAPTURE_1','.lr.ids.u32','.lr.bary.f32','.lr.center.u32',
        'lr.info[0]=s->lrWidth;lr.info[1]=s->lrHeight']:
        if marker not in multires:raise ValueError('Packaged native mode9 export differs')
    count=re.search(r'#define\s+SURFACE_BATCH_CAMERA_COUNT\s+(\d+)u?',text['surface_batch_identity.h'])
    if count and int(count.group(1))!=len(tables):raise ValueError('Compiled table count differs')
    return dict(camera_rows=len(tables),all408_camera_words_match=True,
        source_files=[dict(path='runtime/quake3e/code/renderervk/'+n,bytes=(folder/n).stat().st_size,sha256=digest((folder/n).read_bytes()))for n in SOURCE_NAMES])

def make(out,phase,source):
    if out.exists():raise FileExistsError('Use fresh packet output')
    cohort=ROOT/'cameras/COHORT.json';rows=load(cohort)['rows']
    if len(rows)!=408 or len({r['view_id']for r in rows})!=408:raise ValueError('Exact384TRAIN+24selection required')
    counts={tier:{split:sum(r['tier']==tier and r['split']==split for r in rows)for split in('train','selection')}for tier in TIERS}
    if counts!=COUNTS:raise ValueError('Exact precommitted tier counts required')
    checked=source_check(source,rows);out.mkdir(parents=True);batches=[]
    for tier,(lr,hr)in TIERS.items():
        chosen=sorted((r for r in rows if r['tier']==tier),key=lambda r:(r['split']!='train',r['view_id']))
        for start in range(0,len(chosen),8):
            members=chosen[start:start+8];index=len(batches);name=f'{phase}_{tier.lower()}_{start//8:02d}'
            lines=['set r_surfaceBenchmark 0','set r_surfacePose 0','set r_surfaceUpscale 9','set r_surfaceVisibility 1',
                'set r_surfaceMode 2','set r_transportLockView 0','set r_surfaceBase surface/matched_A.nlsf',
                'set r_surfaceCorrection surface/matched_A.nlsr','set com_maxfps 0','set r_swapInterval 0','wait 24']
            views=[]
            for row in members:
                stem=f'captures/{phase}_{row["view_id"]}_{tier}_m9'
                if len(stem)>40:raise ValueError('Engine MAX_QPATH stem exceeded')
                lines+=['set r_surfaceBatchViewID '+str(row['view_id']),'wait 8','surfaceCapture '+stem,'wait 12']
                views.append(dict(view_id=row['view_id'],split=row['split'],tier=tier,stem=stem,raw_first64_hex=row['raw_first64_hex']))
            raw=('\n'.join(lines+['wait 8','quit'])+'\n').encode('ascii');filename=f'config_{index:02d}.cfg';(out/filename).write_bytes(raw)
            # Original normal capture startup flags, with only caller paths/config replaced.
            settings=[('fs_basepath','<OpenArenaRoot>'),('fs_homepath','<FreshHome>'),('fs_basegame','baseoa'),('logfile','2'),
                ('developer','0'),('con_notifytime','0'),('r_fullscreen','0'),('r_noborder','1'),('r_mode','-1'),
                ('r_customWidth',str(hr[0])),('r_customHeight',str(hr[1])),('s_initsound','0'),('com_introplayed','1'),('bot_enable','0'),
                ('r_fbo','1'),('r_overBrightBits','0'),('r_gamma','1'),('r_neuralFastBuffers','1'),('r_neuralFastScale','1'),
                ('r_neuralTransport','1'),('r_surfaceMode','2'),('r_surfaceVisibility','1'),('r_surfaceUpscale','9'),
                ('r_neuralModel','maps/neural_transport.bin'),('r_transportPlate','textures/neural_transport/reference_480.tga'),
                ('r_neuralView','7'),('in_mouse','1'),('cl_freelook','1'),('in_nograb','1')]
            arrays=16*hr[0]*hr[1]+68*lr[0]*lr[1]+208
            batches.append(dict(index=index,name=name,tier=tier,native_output=list(hr),native_LR=list(lr),
                quarter_guide_extent=[2*lr[0],2*lr[1]],config=dict(path=filename,bytes=len(raw),sha256=digest(raw)),
                views=views,startup_settings=[list(x)for x in settings],startup_tail=['+devmap','neural_test','+exec',filename],
                startup_command_count=len(settings)+2,raw_capture_bytes=arrays*len(members),
                suggested_owner_cap_seconds=60,cap_enforced_by_packet_builder=False))
    plan=dict(schema='C16_RELEASE_MODE9_CAPTURE_PACKETS_1',status='source_only_no_execution',phase=phase,
        counts=counts,views=408,batches=batches,cohort_sha256=digest(cohort.read_bytes()),source_check=checked,
        GPU_jobs=0,engine_processes_started=0,images_generated=0,raw_capture_bytes=sum(b['raw_capture_bytes']for b in batches),
        game_and_surface_assets_must_be_prepared_in_each_home=True,
        fresh_runtime_asset_admission_required_before_new_A_recapture=True)
    (out/'PLAN.json').write_text(json.dumps(plan,indent=2)+'\n');return plan
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
    p.add_argument('--phase',choices=('pre_a','new_a'),required=True)
    p.add_argument('--runtime-source',type=Path,default=ROOT.parent/'runtime/quake3e');a=p.parse_args()
    result=make(a.out,a.phase,a.runtime_source)
    print(json.dumps({k:result[k]for k in('status','phase','counts','views','raw_capture_bytes')}|{'batches':len(result['batches'])}))
if __name__=='__main__':main()
