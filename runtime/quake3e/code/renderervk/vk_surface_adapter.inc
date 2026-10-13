/* Adapter flag2 requires the atomically regenerated repair shader bundle. */
#if !defined(SURFACE_FLAGS_ABI) || SURFACE_FLAGS_ABI != 2
#error "Rebuild all repair shaders and embed them before compiling this adapter"
#endif
/* Isolated fixed-scene surface mode. All camera vectors are REAL/unmirrored. */
static qboolean surface_hash_matches(const void *data,size_t bytes,const char *sha) {byte d[32];return surface_asset_sha256(data,bytes,d) && surface_digest_is(d,sha);}
#include "vk_surface_cnn_loader.inc"
static int surface_prior_mode=-999,surface_prior_view=-999,surface_prior_valid=-999,surface_prior_visibility=-999;
static int surface_prior_upscale=-999;
static qboolean surface_attempted;
/* CPU asset acceptance is independent of retained GPU resource lifetime. */
static qboolean surface_assets_accepted;
/* GPU contents follow asset identities, not merely dimensions/path names. */
static char surface_gpu_base_sha[65],surface_gpu_correction_sha[65],surface_gpu_cnn_sha[65];
static uint32_t surface_attempt_width, surface_attempt_height;
static surfacePush_t surface_last_push;
static qboolean surface_last_valid;
static int surface_last_batch_view_id=-1;
static char surface_pending_capture[MAX_QPATH];
static uint32_t surface_capture_active_centers;
static const char surface_upscale_sha[]="85b9b351198386fb15f677a11e1e2725876c4d4ff01db70e5e6b2d1026d00459";
static const byte surface_pixel_legacy_placeholder[8876]={0};
static uint32_t surface_pixel_width,surface_pixel_parameters,surface_pixel_bytes;
static qboolean surface_direct_native_extent(uint32_t w,uint32_t h) {
    return (w==512 && h==288) || (w==960 && h==540) || (w==1920 && h==1080);
}

/* Independent NLUR intake; neither its header nor its binding meanings are
 * borrowed from NLUP. Only the exact frozen candidate and codec are accepted. */
static qboolean surface_load_pixel(void **model) {
    const byte *a;
    uint32_t width,parameters,i;
    byte digest[32];
    int bytes;
    *model=NULL;surface_pixel_width=surface_pixel_parameters=surface_pixel_bytes=0;
    if(strlen(SURFACE_PIXEL_ASSET_SHA)!=64 || !r_surfacePixelModel || !r_surfacePixelModel->string[0])goto rejected;
    bytes=ri.FS_ReadFile(r_surfacePixelModel->string,NULL);
    if((bytes!=520 && bytes!=868 && bytes!=1468) || ri.FS_ReadFile(r_surfacePixelModel->string,model)!=bytes || !*model)goto rejected;
    a=(const byte *)*model;width=surface_asset_word(a,28);parameters=surface_asset_word(a,36);
    if(memcmp(a,"NLUR0001",8) || !surface_hash_matches(a,bytes,SURFACE_PIXEL_ASSET_SHA) ||
            surface_asset_word(a,8)!=256 || surface_asset_word(a,12)!=1 || surface_asset_word(a,16)!=1 ||
            surface_asset_word(a,20)!=1 || surface_asset_word(a,24)!=21 || surface_asset_word(a,32)!=3 ||
            !((width==0 && parameters==66) || (width==6 && parameters==153) || (width==12 && parameters==303)) ||
            bytes!=256u+4u*parameters || surface_asset_word(a,40)!=1 || surface_asset_word(a,44)!=1 ||
            surface_asset_word(a,48) || surface_asset_word(a,52) ||
            !surface_digest_is(a+64,surface_correction_sha) || !surface_digest_is(a+96,SURFACE_PIXEL_CODE_SHA) ||
            surface_asset_quad(a,192)!=4u*parameters || !surface_asset_finite(a,200) || surface_asset_real(a,200)!=.25f ||
            !surface_asset_sha256(a+256,4u*parameters,digest) || memcmp(digest,a+160,32))goto rejected;
    for(i=56;i<64;i++)if(a[i])goto rejected;
    for(i=128;i<160;i++)if(a[i])goto rejected;
    for(i=204;i<256;i++)if(a[i])goto rejected;
    for(i=256;i<(uint32_t)bytes;i+=4)if(!surface_asset_finite(a,i))goto rejected;
    surface_pixel_width=width;surface_pixel_parameters=parameters;surface_pixel_bytes=(uint32_t)bytes;
    ri.Printf(PRINT_ALL,"Pixel NLUR accepted: %dB width%u params%u SHA%s diagnostic%u; no HR inputs\n",bytes,width,parameters,SURFACE_PIXEL_ASSET_SHA,SURFACE_PIXEL_DIAGNOSTIC);
    return qtrue;
rejected:
    if(*model)ri.FS_FreeFile(*model);*model=NULL;
    ri.Printf(PRINT_WARNING,"Pixel NLUR rejected: frozen SHA/codec/baseline/header/payload/finite checks; initialized checker, no valid arrays\n");
    return qfalse;
}

/* Accept exactly the visual-approved light model, including its header and
 * bound baseline/scene/geometry. Payload-only checks cannot protect scale. */
static qboolean surface_load_upscale(void **model) {
    const byte *a;
    byte digest[32];
    uint32_t i;
    int bytes;
    *model=NULL;
    if(!r_surfaceUpscaleModel || !r_surfaceUpscaleModel->string[0]) return qfalse;
    bytes=ri.FS_ReadFile(r_surfaceUpscaleModel->string,NULL);
    if(bytes!=8876 || ri.FS_ReadFile(r_surfaceUpscaleModel->string,model)!=8876 || !*model) goto rejected;
    a=(const byte *)*model;
    if(memcmp(a,"NLUP0001",8) || !surface_hash_matches(a,8876,surface_upscale_sha) ||
            surface_asset_word(a,8)!=256 || surface_asset_word(a,12)!=1 ||
            surface_asset_word(a,16)!=1 || surface_asset_word(a,20)!=1 ||
            surface_asset_word(a,24)!=44 || surface_asset_word(a,28)!=24 ||
            surface_asset_word(a,32)!=88 || surface_asset_word(a,36)!=2 ||
            surface_asset_word(a,40)!=1803 || surface_asset_word(a,44)!=1979 ||
            surface_asset_word(a,48)!=1 || surface_asset_word(a,52)!=1 ||
            !surface_digest_is(a+64,surface_correction_sha) ||
            !surface_digest_is(a+96,surface_scene_sha) || !surface_digest_is(a+128,surface_geometry_sha) ||
            surface_asset_quad(a,192)!=8620 || !surface_asset_finite(a,200) || surface_asset_real(a,200)!=1.0f ||
            !surface_asset_sha256(a+256,8620,digest) || memcmp(digest,a+160,32)) goto rejected;
    for(i=56;i<64;i++)if(a[i])goto rejected;
    for(i=204;i<256;i++)if(a[i])goto rejected;
    for(i=1;i<88;i++)if(surface_asset_quad(a,256+8*i)<=surface_asset_quad(a,256+8*(i-1)))goto rejected;
    for(i=960;i<8876;i+=4)if(!surface_asset_finite(a,i))goto rejected;
    ri.Printf(PRINT_ALL,"FHD frozen light accepted:8876B,1979 parameters,whole SHA256%s; unchanged header+IDs+FP32 payload\n",surface_upscale_sha);
    return qtrue;
rejected:
    if(*model)ri.FS_FreeFile(*model);*model=NULL;
    ri.Printf(PRINT_WARNING,"FHD model rejected: exact8876-byte frozen NLUP0001 identity,shape,finite payload or bound assets; diagnostic checker\n");
    return qfalse;
}

static VkShaderModule surface_module(const uint32_t *data, size_t bytes) {
    VkShaderModuleCreateInfo ci;
    VkShaderModule module;
    Com_Memset(&ci,0,sizeof(ci)); ci.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize=bytes; ci.pCode=data;
    VK_CHECK(qvkCreateShaderModule(vk.device,&ci,NULL,&module)); return module;
}
static void vk_surface_reset_adapter(void) {
    surface_benchmark_finish("renderer_reset");
    surface_attempted=qfalse; surface_assets_accepted=qfalse; surface_last_valid=qfalse;
    surface_attempt_width=surface_attempt_height=0;
    surface_gpu_base_sha[0]=surface_gpu_correction_sha[0]=surface_gpu_cnn_sha[0]=0;
    surface_prior_mode=surface_prior_view=surface_prior_valid=surface_prior_visibility=-999;
    surface_prior_upscale=-999;
    surface_pending_capture[0]=0; Com_Memset(&surface_last_push,0,sizeof(surface_last_push));
}
/* Call from vk_begin_frame after its slot-fence wait and acquisition/retry block,
 * BEFORE qvkBeginCommandBuffer. Asset changes and resource recreation cannot occur in
 * the current frame's recorded command stream. Shutdown keeps its idle wait. */
static void vk_surface_prepare_frame(void) {
    surfaceValidatedAssets_t a;
    VkShaderModule modules[16];
    void *bvh=NULL,*upscale=NULL,*pixel=NULL,*cnn=NULL; int bytes;
    qboolean changed, created;
    static int rejectedMode=-999;
    int i;
    surface_last_valid=qfalse;
    surface_benchmark_poll(qfalse);
    surface_fhd_timing_read();
    surface_benchmark_collect();
    if(!vk.neuralLight.active || vk.neural_mlp_descriptor==VK_NULL_HANDLE) return;
    /* Binding 6 is statically declared even with surface mode disabled. */
    if(!vk_surface_ensure_fallback()) {
        ri.Error(ERR_FATAL,"Surface compositor requires a valid rgba32f fallback");
        return;
    }
    if(!r_surfaceMode || !r_surfaceMode->integer) return;
    if(r_surfaceMode->integer<1 || r_surfaceMode->integer>2) {
        surface_assets_accepted=qfalse; surface_attempted=qfalse;
        if(rejectedMode!=r_surfaceMode->integer) {
            ri.Printf(PRINT_WARNING,"Surface rejected: unsupported selectable mode %d; diagnostic checker\n",r_surfaceMode->integer);
            rejectedMode=r_surfaceMode->integer;
        }
        return;
    }
    rejectedMode=-999;
    if(!r_surfaceUpscale || r_surfaceUpscale->integer<0 || r_surfaceUpscale->integer>11 ||
            (!r_surfaceUpscale->integer && (VkDeviceSize)vk.neuralLight.width*vk.neuralLight.height>131072) ||
            (r_surfaceUpscale->integer && (r_surfaceMode->integer!=2 || r_surfaceVisibility->integer!=1 ||
            !((r_surfaceUpscale->integer==4 && surface_direct_native_extent(vk.neuralLight.width,vk.neuralLight.height)) ||
              surface_capture4x_extent(vk.neuralLight.width,vk.neuralLight.height) ||
              (vk.neuralLight.width==1920 && vk.neuralLight.height==1080)) ||
            glConfig.vidWidth!=(int)vk.neuralLight.width || glConfig.vidHeight!=(int)vk.neuralLight.height))) {
        surface_assets_accepted=qfalse;surface_attempted=qfalse;
        ri.Printf(PRINT_WARNING,"Surface extent rejected: modes1..11 require actualFHD except modes4/9 native512x288/960x540/1920x1080; surfaceMode2/canonical visibility1/native=framebuffer required (got%ux%u/%dx%d); no resolution fallback\n",vk.neuralLight.width,vk.neuralLight.height,glConfig.vidWidth,glConfig.vidHeight);
        return;
    }
    changed=r_surfaceCorrection->modified || r_surfaceBase->modified || r_surfaceTriangles->modified || r_surfaceMaterials->modified;
    changed=changed || r_surfaceUpscale->modified || r_surfaceUpscaleModel->modified;
    changed=changed || r_surfacePixelModel->modified || r_surfaceCnnModel->modified;
    changed=changed || !surface_attempted || surface_attempt_width!=vk.neuralLight.width || surface_attempt_height!=vk.neuralLight.height;
    if(!changed) {if(surface_assets_accepted)surface_benchmark_poll(qtrue);return;}
    surface_attempted=qtrue; surface_assets_accepted=qfalse;
    surface_attempt_width=vk.neuralLight.width; surface_attempt_height=vk.neuralLight.height;
    r_surfaceCorrection->modified=r_surfaceBase->modified=r_surfaceTriangles->modified=r_surfaceMaterials->modified=qfalse;
    r_surfaceUpscale->modified=r_surfaceUpscaleModel->modified=qfalse;
    r_surfacePixelModel->modified=r_surfaceCnnModel->modified=qfalse;
    Com_Memset(&a,0,sizeof(a));
    /* Rejection leaves the existing renderer and fallback alive. Their output
     * cannot be dispatched, captured or presented as valid through this gate. */
    if(!surface_load_assets(&a)) return;
    bytes=ri.FS_ReadFile("surface/visibility.bvh",NULL);
    if(bytes!=SURFACE_BVH_BYTES || bytes<=0 || bytes>16*1024*1024 || ri.FS_ReadFile("surface/visibility.bvh",&bvh)!=bytes || !bvh || !surface_hash_matches(bvh,bytes,SURFACE_BVH_SHA)) {
        ri.Printf(PRINT_WARNING,"Surface rejected: canonical visibility BVH size/hash; checker required\n");
        if(bvh)ri.FS_FreeFile(bvh); surface_free_assets(&a); return;
    }
    a.bvh=bvh; a.bvhBytes=bytes;
    if(r_surfaceUpscale->integer>=1 && r_surfaceUpscale->integer<=7 && r_surfaceUpscale->integer!=4 && !surface_load_upscale(&upscale)) {
        ri.FS_FreeFile(bvh);a.bvh=NULL;surface_free_assets(&a);return;
    }
    if(r_surfaceUpscale->integer==8 && !surface_load_pixel(&pixel)) {
        ri.FS_FreeFile(bvh);a.bvh=NULL;surface_free_assets(&a);return;
    }
    if(r_surfaceUpscale->integer==10 && !surface_load_cnn(&cnn)) {
        ri.FS_FreeFile(bvh);a.bvh=NULL;surface_free_assets(&a);return;
    }
    /* The paired allowlist permits explicitly configured identities.
     * Resource reuse requires exact accepted base/correction hashes.
     * The loader pins every accepted byte stream to its configured SHA.
     * Equal byte lengths do not imply equal contents; any admitted asset
     * digest change requires new uploads/resources, including a same-C16 refit. */
    if(vk_surface_renderer.ready && !strcmp(surface_gpu_base_sha,surface_base_sha) &&
            !strcmp(surface_gpu_correction_sha,surface_correction_sha) && vk_surface_renderer.width==vk.neuralLight.width &&
            vk_surface_renderer.height==vk.neuralLight.height &&
            vk_surface_renderer.triangleCount==a.triangleCount && vk_surface_renderer.hasCorrection &&
            (!r_surfaceUpscale->integer || (r_surfaceUpscale->integer==4 && vk.neuralLight.width!=1920) || (vk_surface_renderer.fhdReady &&
            (r_surfaceUpscale->integer<=7?(r_surfaceUpscale->integer==4 || vk_surface_renderer.legacyModelReady):
            (vk_surface_renderer.pixelReady && (r_surfaceUpscale->integer==9 || r_surfaceUpscale->integer==11 ||
            (r_surfaceUpscale->integer==8 && vk_surface_renderer.pixelModelReady) ||
            (r_surfaceUpscale->integer==10 && vk_surface_renderer.cnnReady && vk_surface_renderer.cnnWidth==surface_cnn_width && !strcmp(surface_gpu_cnn_sha,surface_cnn_sha_for_width(surface_cnn_width))))))))) {
        surface_assets_accepted=qtrue;
        ri.Printf(PRINT_ALL,"Surface canonical assets accepted: reused native %ux%u GPU resources\n",vk.neuralLight.width,vk.neuralLight.height);
        if(upscale)ri.FS_FreeFile(upscale);
        if(pixel)ri.FS_FreeFile(pixel);
        if(cnn)ri.FS_FreeFile(cnn);
        ri.FS_FreeFile(bvh); a.bvh=NULL; surface_free_assets(&a);surface_benchmark_poll(qtrue);return;
    }
    modules[0]=surface_module(surface_visibility_vert_spv,sizeof(surface_visibility_vert_spv));
    modules[1]=surface_module(surface_visibility_frag_spv,sizeof(surface_visibility_frag_spv));
    modules[2]=surface_module(surface_resolve_directional_spv,sizeof(surface_resolve_directional_spv));
    modules[3]=surface_module(surface_resolve_small32_spv,sizeof(surface_resolve_small32_spv));
    modules[4]=surface_module(surface_firsthit_spv,sizeof(surface_firsthit_spv));
    created=vk_surface_create(vk.neuralLight.width,vk.neuralLight.height,&a,modules[0],modules[1],modules[2],modules[3],modules[4]);
    if(created && r_surfaceUpscale->integer && (r_surfaceUpscale->integer!=4 || vk.neuralLight.width==1920)) {
#if defined(SURFACE_FHD_ABI) && SURFACE_FHD_ABI == 1
        modules[5]=surface_module(surface_resolve_small32_linear_spv,sizeof(surface_resolve_small32_linear_spv));
        modules[6]=surface_module(surface_upscale_light_spv,sizeof(surface_upscale_light_spv));
        modules[7]=surface_module(surface_upscale_light_fast_spv,sizeof(surface_upscale_light_fast_spv));
        modules[8]=surface_module(surface_upscale_bilinear_fast_spv,sizeof(surface_upscale_bilinear_fast_spv));
        modules[9]=surface_module(surface_firsthit_gated_spv,sizeof(surface_firsthit_gated_spv));
        created=surface_create_fhd((r_surfaceUpscale->integer>=8 || r_surfaceUpscale->integer==4)?surface_pixel_legacy_placeholder:upscale,
            8876,modules[5],modules[6],modules[7],modules[8],modules[9]);
        for(i=5;i<10;i++)qvkDestroyShaderModule(vk.device,modules[i],NULL);
        vk_surface_renderer.legacyModelReady=created && r_surfaceUpscale->integer<=7 && r_surfaceUpscale->integer!=4;
        if(created && r_surfaceUpscale->integer>=8) {
            modules[10]=surface_module(surface_upscale_pixel_residual_spv,sizeof(surface_upscale_pixel_residual_spv));
            modules[11]=surface_module(surface_upscale_pixel_bilinear_spv,sizeof(surface_upscale_pixel_bilinear_spv));
            created=surface_create_pixel(pixel,r_surfaceUpscale->integer==8?surface_pixel_bytes:0,
                surface_pixel_width,surface_pixel_parameters,modules[10],modules[11]);
            qvkDestroyShaderModule(vk.device,modules[10],NULL);qvkDestroyShaderModule(vk.device,modules[11],NULL);
            if(created && r_surfaceUpscale->integer==10) {
                modules[12]=surface_module(surface_upscale_cnn_display_spv,sizeof(surface_upscale_cnn_display_spv));
                modules[13]=surface_module(surface_upscale_cnn_conv0_spv,sizeof(surface_upscale_cnn_conv0_spv));
                modules[14]=surface_module(surface_upscale_cnn_conv1_spv,sizeof(surface_upscale_cnn_conv1_spv));
                modules[15]=surface_module(surface_upscale_cnn_head_spv,sizeof(surface_upscale_cnn_head_spv));
                created=surface_create_cnn(cnn,surface_cnn_bytes,surface_cnn_width,surface_cnn_parameters,modules[12],modules[13],modules[14],modules[15]);
                for(i=12;i<16;i++)qvkDestroyShaderModule(vk.device,modules[i],NULL);
            }
        }
#else
        created=qfalse;
        ri.Printf(PRINT_WARNING,"FHD rejected: source shader bundle requires SURFACE_FHD_ABI1 and all separately embedded FHD compute modules\n");
#endif
    }
    for(i=0;i<5;i++)qvkDestroyShaderModule(vk.device,modules[i],NULL);
    ri.FS_FreeFile(bvh); a.bvh=NULL; surface_free_assets(&a);
    if(upscale)ri.FS_FreeFile(upscale);
    if(pixel)ri.FS_FreeFile(pixel);
    if(cnn)ri.FS_FreeFile(cnn);
    surface_assets_accepted=created;
    if(created) {Q_strncpyz(surface_gpu_base_sha,surface_base_sha,sizeof(surface_gpu_base_sha));
        Q_strncpyz(surface_gpu_correction_sha,surface_correction_sha,sizeof(surface_gpu_correction_sha));
        Q_strncpyz(surface_gpu_cnn_sha,r_surfaceUpscale->integer==10?surface_cnn_sha_for_width(surface_cnn_width):"",sizeof(surface_gpu_cnn_sha));}
    if(created)surface_benchmark_poll(qtrue);
    if(!created) ri.Printf(PRINT_WARNING,"Surface renderer creation rejected: native budget/formats or validated resource contract; diagnostic checker\n");
}
static void vk_surface_dispatch(void) {
    surfacePush_t p;
    surface_last_batch_view_id=-1;
    if(!r_surfaceMode || r_surfaceMode->integer<1 || r_surfaceMode->integer>2 ||
            !surface_assets_accepted || !vk_surface_renderer.ready ||
            vk_surface_renderer.width!=vk.neuralLight.width || vk_surface_renderer.height!=vk.neuralLight.height) {
        surface_last_valid=qfalse; vk_surface_renderer.outputWritten=qfalse; return;
    }
    Com_Memset(&p,0,sizeof(p));
    VectorCopy(backEnd.viewParms.or.origin,p.origin);
    VectorCopy(backEnd.viewParms.or.axis[0],p.forwardTanX);
    VectorNegate(backEnd.viewParms.or.axis[1],p.rightTanY);
    VectorCopy(backEnd.viewParms.or.axis[2],p.upNear);
    p.forwardTanX[3]=tanf(DEG2RAD(backEnd.refdef.fov_x)*.5f);
    p.rightTanY[3]=tanf(DEG2RAD(backEnd.refdef.fov_y)*.5f); p.upNear[3]=.128f;
    if(r_surfaceBatchViewID && r_surfaceBatchViewID->integer>=0) {
        float camera[16];
        if((r_surfaceUpscale->integer!=9 && r_surfaceUpscale->integer!=4 && r_surfaceUpscale->integer!=10 && r_surfaceUpscale->integer!=11) || !R_SurfaceBatchFrame(camera)) {
            surface_last_valid=qfalse;vk_surface_renderer.outputWritten=qfalse;
            ri.Printf(PRINT_WARNING,"Surface batch camera rejected: frozen viewID%d/mode4_or9_or10_or11 required\n",r_surfaceBatchViewID->integer);
            return;
        }
        /* Preserve every basis/tangent/near bit, avoiding degree round trips. */
        Com_Memcpy(&p,camera,16*sizeof(float));
        surface_last_batch_view_id=r_surfaceBatchViewID->integer;
    }
    p.info[0]=vk.neuralLight.width; p.info[1]=vk.neuralLight.height; p.info[3]=vk_surface_renderer.triangleCount;
    /* info.z bit 0 is historical full diagnostics (not allocated here); bit 1
     * is canonical first-hit math. Never set bit 0 against the 4-byte dummy. */
    p.info[2]=r_surfaceVisibility->integer==1 ? 2u : 0u;
    if(r_surfaceUpscale->integer==4 && p.info[0]!=1920) {
        surface_last_valid=vk_surface_draw(&p,qtrue,qtrue);
        vk_surface_renderer.fhdLastMode=4;vk_surface_renderer.fhdLRCurrent=qfalse;
        vk_surface_renderer.fhdHRGuidesCurrent=surface_last_valid;vk_surface_renderer.hrDiagnosticWritten=qfalse;
        vk_surface_renderer.timingReady=qfalse;
    } else surface_last_valid=r_surfaceUpscale->integer?surface_fhd_draw(&p,(uint32_t)r_surfaceUpscale->integer):
        vk_surface_draw(&p,r_surfaceMode->integer==2,r_surfaceVisibility->integer==1);
    surface_last_push=p;
}
static void vk_surface_set_meta(neuralMlpGpuData_t *dst) {
    int mode=r_surfaceMode ? r_surfaceMode->integer : 0;
    if(!mode)return;
    dst->reflectionMeta[0]=surface_assets_accepted && surface_last_valid ? 1 : 0;
    dst->reflectionMeta[1]=mode; dst->reflectionMeta[2]=32032;
    /* Offline plate eligibility always requires an explicit lock, even if the
     * separate historical transport mode is disabled. */
    dst->reflectionParams[2]=r_transportLockView && r_transportLockView->integer ? 1.0f : 0.0f;
    if(mode!=surface_prior_mode || dst->viewMode!=surface_prior_view || dst->reflectionMeta[0]!=surface_prior_valid || r_surfaceVisibility->integer!=surface_prior_visibility || r_surfaceUpscale->integer!=surface_prior_upscale) {
        const char *title=dst->viewMode==3 ? "Neural Surface | OFFLINE CYCLES PLATE | EXACT CAMERA LOCK REQUIRED" :
            (!dst->reflectionMeta[0] ? "Neural Surface | INVALID ASSET | DIAGNOSTIC CHECKER" :
            (r_surfaceUpscale->integer==1 ? "Neural Surface | FHD1920x1080 FROZEN LIGHT1979 | LR480x270+HR GUIDES" :
            (r_surfaceUpscale->integer==2 ? "Neural Surface | FHD1920x1080 BILINEAR CONTROL | LR480x270" :
            (r_surfaceUpscale->integer==3 ? "Neural Surface | FHD1920x1080 GEOMETRY CONTROL | LR480x270+HR GUIDES" :
            (r_surfaceUpscale->integer==4 ? "Neural Surface | FHD1920x1080 DIRECT SMALL32 COMPARISON" :
            (r_surfaceVisibility->integer==1 ? (mode==2 ? "Neural Surface | SMALL32 CORRECTION | LIVE BVH FIRST HIT" : "Neural Surface | FROZEN DIRECTIONAL | LIVE BVH FIRST HIT") :
            "Neural Surface | ISOLATED RASTER EXPERIMENT | BOUNDARY PARITY FAILS"))))));
        ri.Printf(PRINT_ALL,"%s\n",title);
#if defined(_WIN32)
        if(g_wv.hWnd)SetWindowTextA(g_wv.hWnd,title);
#endif
        surface_prior_mode=mode;surface_prior_view=dst->viewMode;surface_prior_valid=dst->reflectionMeta[0];surface_prior_visibility=r_surfaceVisibility->integer;
        surface_prior_upscale=r_surfaceUpscale->integer;
    }
}
void vk_surface_capture_f(void) {
    const char *stem; size_t i;
    if(ri.Cmd_Argc()!=2) {ri.Printf(PRINT_ALL,"usage: surfaceCapture <game-relative stem>\n");return;}
    stem=ri.Cmd_Argv(1);
    if(!*stem || strlen(stem)>MAX_QPATH-24 || strstr(stem,".."))return;
    for(i=0;stem[i];i++)if(!((stem[i]>='a'&&stem[i]<='z')||(stem[i]>='A'&&stem[i]<='Z')||(stem[i]>='0'&&stem[i]<='9')||stem[i]=='_'||stem[i]=='-'||stem[i]=='/'))return;
    Q_strncpyz(surface_pending_capture,stem,sizeof(surface_pending_capture));
    /* Invalidate old completion markers immediately. A rejected/partial new
     * capture can never be mistaken for a successful earlier same-stem frame. */
    {char path[MAX_QPATH];const char pending[]="{\"complete\":false,\"status\":\"pending_current_frame\"}\n";
        Com_sprintf(path,sizeof(path),"%s.json",stem);ri.FS_WriteFile(path,pending,sizeof(pending)-1);}
}

static qboolean surface_fhd_write_array(const char *suffix, const char *key,
        const char *dtype, uint32_t width, uint32_t height, uint32_t channels,
        const void *data, size_t bytes, char *arrays, size_t capacity, size_t *used) {
    byte digest[32], savedDigest[32];
    static const char digits[]="0123456789abcdef";
    char path[MAX_QPATH],hex[65],shape[80];
    void *saved=NULL;
    uint32_t i;
    int savedBytes;
    size_t n;
    if(bytes>0x7fffffffU || !surface_asset_sha256(data,bytes,digest))return qfalse;
    Com_sprintf(path,sizeof(path),"%s%s",surface_pending_capture,suffix);
    ri.FS_WriteFile(path,data,(int)bytes);
    savedBytes=ri.FS_ReadFile(path,&saved);
    if(savedBytes!=(int)bytes || !saved || !surface_asset_sha256(saved,bytes,savedDigest) || memcmp(savedDigest,digest,32)) {
        if(saved)ri.FS_FreeFile(saved);return qfalse;
    }
    ri.FS_FreeFile(saved);
    for(i=0;i<32;i++){hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15];}hex[64]=0;
    if(!height)Com_sprintf(shape,sizeof(shape),"[%u]",width);
    else if(channels==1)Com_sprintf(shape,sizeof(shape),"[%u,%u]",height,width);
    else Com_sprintf(shape,sizeof(shape),"[%u,%u,%u]",height,width,channels);
    if(*used+strlen(path)+strlen(key)+strlen(dtype)+strlen(shape)+strlen(hex)+128>=capacity)return qfalse;
    Com_sprintf(arrays+*used,(int)(capacity-*used),"%s\"%s\":{\"path\":\"%s\",\"dtype\":\"%s\",\"shape\":%s,\"bytes\":%llu,\"sha256\":\"%s\"}",*used?",":"",key,path,dtype,shape,(unsigned long long)bytes,hex);
    n=strlen(arrays+*used);*used+=n;
    return qtrue;
}

static qboolean surface_fhd_write_image(surfaceImage_t *image, const char *suffix,
        const char *key, const char *dtype, uint32_t width, uint32_t height,
        uint32_t channels, VkImageLayout layout, char *arrays, size_t capacity, size_t *used) {
    void *data;
    qboolean ok;
    uint32_t i;
    if(!image->requested || image->requested>0x7fffffffU)return qfalse;
    data=ri.Malloc((int)image->requested);
    ok=surface_fhd_capture_image(image,width,height,layout,data,image->requested) &&
        surface_fhd_write_array(suffix,key,dtype,width,height,channels,data,(size_t)image->requested,arrays,capacity,used);
    if(ok && !strcmp(key,"hr_center_ids")) {
        surface_capture_active_centers=0;
        for(i=0;i<width*height;i++) {
            uint32_t gate=((const uint32_t *)data)[i];
            if(gate>1u) {ok=qfalse;break;}
            surface_capture_active_centers+=gate;
        }
    }
    ri.Free(data);return ok;
}

#include "vk_surface_pixel_capture.inc"
#include "vk_surface_cnn_capture.inc"
#include "vk_surface_multires_capture.inc"

static void surface_fhd_finish_capture(void) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    surfacePush_t lr=surface_last_push;
    char arrays[8192],meta[16384],path[MAX_QPATH];
    size_t used=0;
    const char *mode=(s->fhdLastMode==1 || s->fhdLastMode==5 || s->fhdLastMode==7)?"light":
        (s->fhdLastMode==2 || s->fhdLastMode==6)?"bilinear":s->fhdLastMode==3?"geometry":"direct_small32";
    const char *optimization=s->fhdLastMode==5?"shared_LR_guide":s->fhdLastMode==6?"fast_bilinear":
        s->fhdLastMode==7?"shared_LR_guide_and_center_gate":"baseline";
    qboolean ok;
    if(!surface_assets_accepted || !surface_last_valid || !s->fhdReady || !s->outputWritten ||
            s->fhdLastMode!=(uint32_t)r_surfaceUpscale->integer || surface_last_push.info[0]!=1920 || surface_last_push.info[1]!=1080)goto rejected;
    arrays[0]=0;surface_capture_active_centers=0;lr.info[0]=480;lr.info[1]=270;
    ok=surface_fhd_write_image(&s->output,".rgba32f","hr_rgba","<f4",1920,1080,4,VK_IMAGE_LAYOUT_GENERAL,arrays,sizeof(arrays),&used);
    if(ok && s->fhdHRGuidesCurrent) {
        ok=surface_fhd_write_image(&s->id,".ids.u32","hr_ids","<u4",3840,2160,1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
            surface_fhd_write_image(&s->barycentric,".bary.f32","hr_bary","<f4",3840,2160,2,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used);
    }
    if(ok && s->fhdLRCurrent) {
        ok=surface_fhd_write_image(&s->lrLinear,".lr.rgba32f","lr_rgba","<f4",480,270,4,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
            surface_fhd_write_image(&s->lrID,".lr.ids.u32","lr_ids","<u4",960,540,1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
            surface_fhd_write_image(&s->lrBarycentric,".lr.bary.f32","lr_bary","<f4",960,540,2,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
            surface_fhd_write_image(&s->lrCenterGate,".lr.center.u32","lr_center_ids","<u4",480,270,1,VK_IMAGE_LAYOUT_GENERAL,arrays,sizeof(arrays),&used);
    }
    if(ok && s->hrDiagnosticWritten) {
        ok=surface_fhd_write_image(&s->hrCenterGate,".hr.center.u32","hr_center_ids","<u4",1920,1080,1,VK_IMAGE_LAYOUT_GENERAL,arrays,sizeof(arrays),&used) &&
            surface_fhd_write_image(&s->geometryLinear,".geometry.rgba32f","geometry_linear","<f4",1920,1080,4,VK_IMAGE_LAYOUT_GENERAL,arrays,sizeof(arrays),&used);
    }
    if(ok)ok=surface_fhd_write_array(".frame.hr.bin","hr_frame","|u1",96,0,1,&surface_last_push,sizeof(surface_last_push),arrays,sizeof(arrays),&used) &&
        surface_fhd_write_array(".frame.lr.bin","lr_frame","|u1",96,0,1,&lr,sizeof(lr),arrays,sizeof(arrays),&used);
    if(!ok)goto rejected;
    /* All image readbacks submit behind the exact captured frame and wait, so
     * these are its current slot's query results, not earlier-frame timings. */
    surface_fhd_timing_read();
    Com_sprintf(meta,sizeof(meta),"{\"schema\":\"NLUP_FHD_CAPTURE_1\",\"complete\":true,\"mode\":\"%s\",\"mode_index\":%u,\"optimization\":\"%s\",\"hr_guide_semantics\":\"%s\",\"output_width\":1920,\"output_height\":1080,\"lr_width\":480,\"lr_height\":270,\"framebuffer_width\":%d,\"framebuffer_height\":%d,\"window_width\":%d,\"window_height\":%d,\"pixel_origin\":\"top_left\",\"quarter_order\":\"row_major_00_10_01_11\",\"lr_transfer\":\"linear_associated\",\"center_semantics\":\"incident_bool01\",\"output_semantics\":\"display_associated\",\"geometry_linear_semantics\":\"%s\",\"hr_guides_valid\":%s,\"lr_valid\":%s,\"upscaler_sha256\":\"%s\",\"geometry_sha256\":\"%s\",\"scene_sha256\":\"%s\",\"correction_sha256\":\"%s\",\"source_shader_receipt_sha256\":\"%s\",\"model_asset_bytes\":8876,\"model_parameters\":1979,\"new_texture_bytes\":0,\"hr_bvh_quarter_rays\":%u,\"hr_bvh_quarter_ray_upper_bound\":%u,\"hr_quarter_invocations\":%u,\"hr_suppressed_quarter_rays\":%u,\"lr_guide_assemblies_upper_bound\":%u,\"lr_bvh_quarter_rays\":%u,\"hr_analytic_center_gates\":%u,\"lr_analytic_center_gates\":%u,\"dense_decoder_macs\":%llu,\"dense_decoder_macs_semantics\":\"full_coverage_upper_bound\",\"timing_current_frame\":%s,\"frame_slot\":%d,\"timings_ms\":{\"whole_gpu_frame\":%.9g,\"two_pass_total\":%.9g,\"lr_visibility\":%.9g,\"lr_appearance\":%.9g,\"hr_visibility\":%.9g,\"hr_final\":%.9g,\"engine_composition\":%.9g,\"presentation_draws\":%.9g},\"arrays\":{%s}}\n",
        mode,s->fhdLastMode,optimization,s->fhdLastMode==7?"canonical_inside_physical_center_gate":
        s->fhdHRGuidesCurrent?"canonical_all_quarters":"not_generated_this_frame",glConfig.vidWidth,glConfig.vidHeight,gls.windowWidth,gls.windowHeight,
        (s->fhdLastMode==2 || s->fhdLastMode==6)?"bilinear_base_linear_associated":"geometry_base_linear_associated",
        s->fhdHRGuidesCurrent?"true":"false",s->fhdLRCurrent?"true":"false",surface_upscale_sha,surface_geometry_sha,surface_scene_sha,surface_correction_sha,SURFACE_SHADER_RECEIPT_SHA,
        s->fhdHRGuidesCurrent?(s->fhdLastMode==7?4u*surface_capture_active_centers:8294400u):0u,
        s->fhdHRGuidesCurrent?8294400u:0u,s->fhdHRGuidesCurrent?8294400u:0u,
        s->fhdLastMode==7?8294400u-4u*surface_capture_active_centers:0u,
        (s->fhdLastMode==5 || s->fhdLastMode==7)?2073600u:(s->fhdLastMode==1 || s->fhdLastMode==3)?33177600u:0u,
        s->fhdLRCurrent?518400u:0u,s->fhdLastMode==7?4147200u:2073600u,s->fhdLRCurrent?129600u:0u,
        (s->fhdLastMode==1 || s->fhdLastMode==5 || s->fhdLastMode==7)?(unsigned long long)3632947200ULL:0ULL,s->timingReady?"true":"false",vk.cmd_index,
        surface_fhd_timestamp_ms(0,7),surface_fhd_timestamp_ms(1,5),surface_fhd_timestamp_ms(1,2),surface_fhd_timestamp_ms(2,3),surface_fhd_timestamp_ms(3,4),surface_fhd_timestamp_ms(4,5),surface_fhd_timestamp_ms(5,6),surface_fhd_timestamp_ms(6,7),arrays);
    Com_sprintf(path,sizeof(path),"%s.json",surface_pending_capture);ri.FS_WriteFile(path,meta,(int)strlen(meta));
    ri.Printf(PRINT_ALL,"FHD current-frame capture complete:%s actual1920x1080 mode%s totalGPU%.3fms two-pass%.3fms; setup/readback/hash/presentation waits excluded\n",surface_pending_capture,mode,surface_fhd_timestamp_ms(0,7),surface_fhd_timestamp_ms(1,5));
    surface_pending_capture[0]=0;return;
rejected:
    {const char failed[]="{\"schema\":\"NLUP_FHD_CAPTURE_1\",\"complete\":false,\"status\":\"rejected_or_partial_current_frame\"}\n";
        Com_sprintf(path,sizeof(path),"%s.json",surface_pending_capture);ri.FS_WriteFile(path,failed,sizeof(failed)-1);}
    ri.Printf(PRINT_WARNING,"FHD capture rejected/partial; no valid completion marker for current frame\n");surface_pending_capture[0]=0;
}
/* Matched B/coarse-control direct FHD output. No fabricated LR/NLUP metadata. */
static void surface_direct_fhd_finish_capture(void) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    char arrays[8192],meta[16384],path[MAX_QPATH];size_t used=0;qboolean ok;
    const surfaceExperimentAssetIdentity_t *identity=surface_selected_identity;
    uint32_t w=surface_last_push.info[0],h=surface_last_push.info[1];
    if(!identity || !surface_assets_accepted || !surface_last_valid || !s->ready || !s->outputWritten ||
       s->fhdLastMode!=4 || r_surfaceUpscale->integer!=4 || !s->fhdHRGuidesCurrent || s->fhdLRCurrent ||
       !surface_direct_native_extent(w,h) || glConfig.vidWidth!=(int)w || glConfig.vidHeight!=(int)h ||
       !r_surfaceBatchViewID || surface_last_batch_view_id<0 || surface_last_batch_view_id!=r_surfaceBatchViewID->integer ||
       SURFACE_BATCH_CAMERA_COUNT==0u)goto rejected;
    arrays[0]=0;
    ok=surface_fhd_write_image(&s->output,".rgba32f","hr_rgba","<f4",w,h,4,VK_IMAGE_LAYOUT_GENERAL,arrays,sizeof(arrays),&used) &&
       surface_fhd_write_image(&s->id,".ids.u32","hr_ids","<u4",2*w,2*h,1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
       surface_fhd_write_image(&s->barycentric,".bary.f32","hr_bary","<f4",2*w,2*h,2,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
       surface_fhd_write_array(".frame.hr.bin","hr_frame","|u1",96,0,1,&surface_last_push,sizeof(surface_last_push),arrays,sizeof(arrays),&used);
    if(!ok)goto rejected;
    if(s->fhdReady)surface_fhd_timing_read();else s->timingReady=qfalse;
    if(s->timingReady && s->timingMode!=4)s->timingReady=qfalse;
    Com_sprintf(meta,sizeof(meta),"{\"schema\":\"NLSB_NATIVE_CAPTURE_1\",\"complete\":true,\"mode_index\":4,\"view_id\":%d,\"camera_manifest_sha256\":\"%s\",\"camera_table_sha256\":\"%s\",\"camera_first64_semantics\":\"frozen_float32_words_no_degree_roundtrip\",\"material_table_sha256\":\"%s\",\"experiment_identity\":\"%s\",\"arm\":\"%s\",\"genuine_higher_detail_asset\":%s,\"output_width\":%u,\"output_height\":%u,\"supervision_width\":%u,\"supervision_height\":%u,\"framebuffer_width\":%d,\"framebuffer_height\":%d,\"window_width\":%d,\"window_height\":%d,\"pixel_origin\":\"top_left\",\"quarter_order\":\"row_major_00_10_01_11\",\"output_semantics\":\"display_associated\",\"fixed_alpha_semantics\":\"incident_center_gate_times_valid_quarter_count_div4\",\"coverage_learned\":false,\"alpha_AA_floor_requires_physical_teacher_measurement\":true,\"lr_valid\":false,\"hr_guides_valid\":true,\"atlas_texels\":%u,\"atlas_chart_count\":88,\"base_channels\":12,\"correction_channels\":4,\"base_sha256\":\"%s\",\"correction_sha256\":\"%s\",\"geometry_sha256\":\"%s\",\"scene_sha256\":\"%s\",\"source_shader_receipt_sha256\":\"%s\",\"receiver_gate_source_sha256\":\"%s\",\"base_asset_bytes\":%llu,\"correction_asset_bytes\":%llu,\"correction_decoder_parameters\":2275,\"hr_bvh_quarter_rays\":%u,\"hr_analytic_center_gates\":%u,\"lr_bvh_quarter_rays\":0,\"lr_analytic_center_gates\":0,\"small32_full_coverage_MAC_upper_bound\":%llu,\"timing_current_frame\":%s,\"timings_ms\":{\"whole_gpu_frame\":%.9g,\"direct_surface_total\":%.9g,\"hr_visibility\":%.9g,\"hr_appearance\":%.9g,\"engine_composition\":%.9g,\"presentation_draws\":%.9g},\"arrays\":{%s}}\n",
        surface_last_batch_view_id,SURFACE_BATCH_CAMERA_MANIFEST_SHA,SURFACE_BATCH_CAMERA_TABLE_SHA,surface_material_sha,
        identity->name,identity->genuineFHD?"B_higher_detail_FHD":"C_coarse_atlas_FHD_control",identity->genuineFHD?"true":"false",
        w,h,identity->genuineFHD?1920u:0u,identity->genuineFHD?1080u:0u,
        glConfig.vidWidth,glConfig.vidHeight,gls.windowWidth,gls.windowHeight,identity->texels,
        surface_base_sha,surface_correction_sha,surface_geometry_sha,surface_scene_sha,SURFACE_SHADER_RECEIPT_SHA,SURFACE_EXPERIMENT_GATE_SHA,
        (unsigned long long)identity->baseBytes,(unsigned long long)identity->correctionBytes,4u*w*h,w*h,(unsigned long long)w*h*4ULL*2208ULL,s->timingReady?"true":"false",
        surface_fhd_timestamp_ms(0,7),surface_fhd_timestamp_ms(1,5),surface_fhd_timestamp_ms(3,4),
        surface_fhd_timestamp_ms(4,5),surface_fhd_timestamp_ms(5,6),surface_fhd_timestamp_ms(6,7),arrays);
    Com_sprintf(path,sizeof(path),"%s.json",surface_pending_capture);ri.FS_WriteFile(path,meta,(int)strlen(meta));
    surface_pending_capture[0]=0;return;
rejected:
    Com_sprintf(path,sizeof(path),"%s.json",surface_pending_capture);
    {const char failed[]="{\"schema\":\"NLSB_NATIVE_CAPTURE_1\",\"complete\":false,\"status\":\"rejected_or_partial_current_frame\"}\n";
     ri.FS_WriteFile(path,failed,sizeof(failed)-1);}
    surface_pending_capture[0]=0;
}

static void vk_surface_finish_capture(void) {
    float *pixels; VkDeviceSize bytes; char meta[2048],path[MAX_QPATH];
    uint32_t *ids; float *barycentrics;
    VkDeviceSize idBytes, barycentricBytes;
    surfacePush_t *p=&surface_last_push;
    if(!surface_pending_capture[0])return;
    if(r_surfaceUpscale && r_surfaceUpscale->integer==9) {surface_multires_finish_capture();return;}
    if(r_surfaceUpscale && r_surfaceUpscale->integer==4) {surface_direct_fhd_finish_capture();return;}
    if(r_surfaceUpscale && r_surfaceUpscale->integer>=10) {surface_cnn_finish_capture();return;}
    if(r_surfaceUpscale && r_surfaceUpscale->integer>=8) {surface_pixel_finish_capture();return;}
    if(r_surfaceUpscale && r_surfaceUpscale->integer) {surface_fhd_finish_capture();return;}
    if(!surface_assets_accepted || !surface_last_valid || r_surfaceMode->integer<1 ||
            r_surfaceMode->integer>2 || !vk_surface_renderer.ready) {
        ri.Printf(PRINT_WARNING,"Surface capture rejected: no valid current surface output\n");surface_pending_capture[0]=0;return;
    }
    bytes=(VkDeviceSize)p->info[0]*p->info[1]*16; pixels=ri.Malloc((int)bytes);
    if(vk_surface_capture_rgba(pixels,bytes)) {
        Com_sprintf(path,sizeof(path),"%s.rgba32f",surface_pending_capture);ri.FS_WriteFile(path,pixels,(int)bytes);
        idBytes=(VkDeviceSize)p->info[0]*p->info[1]*16;
        barycentricBytes=(VkDeviceSize)p->info[0]*p->info[1]*32;
        ids=ri.Malloc((int)idBytes); barycentrics=ri.Malloc((int)barycentricBytes);
        if(vk_surface_capture_quadrants(ids,idBytes,barycentrics,barycentricBytes)) {
            Com_sprintf(path,sizeof(path),"%s.ids.u32",surface_pending_capture);ri.FS_WriteFile(path,ids,(int)idBytes);
            Com_sprintf(path,sizeof(path),"%s.bary.f32",surface_pending_capture);ri.FS_WriteFile(path,barycentrics,(int)barycentricBytes);
            ri.Printf(PRINT_ALL,"Surface current-frame visibility capture: %ux%u IDs+barycentric image rows\n",2*p->info[0],2*p->info[1]);
        } else ri.Printf(PRINT_WARNING,"Surface visibility readback rejected; RGBA capture exists but IDs/barycentric QA is incomplete\n");
        ri.Free(barycentrics); ri.Free(ids);
        Com_sprintf(meta,sizeof(meta),"{\"schema\":\"neural_surface_engine_v1\",\"width\":%u,\"height\":%u,\"mode\":%d,\"visibility\":%d,\"origin\":[%.9g,%.9g,%.9g],\"forward\":[%.9g,%.9g,%.9g],\"right\":[%.9g,%.9g,%.9g],\"up\":[%.9g,%.9g,%.9g],\"tanx\":%.9g,\"tany\":%.9g,\"near\":%.9g,\"geometry_sha256\":\"657e3d200dcf36412bec2e4b3b3730737a5de07a8e8a49df6e2ba80bb9b1c9ce\",\"correction_sha256\":\"4b3a42f65b1598f0236ac427ac6083685f120d2774ee8f631fd1ab5d34803bf1\"}\n",p->info[0],p->info[1],r_surfaceMode->integer,r_surfaceVisibility->integer,p->origin[0],p->origin[1],p->origin[2],p->forwardTanX[0],p->forwardTanX[1],p->forwardTanX[2],p->rightTanY[0],p->rightTanY[1],p->rightTanY[2],p->upNear[0],p->upNear[1],p->upNear[2],p->forwardTanX[3],p->rightTanY[3],p->upNear[3]);
        Com_sprintf(path,sizeof(path),"%s.json",surface_pending_capture);ri.FS_WriteFile(path,meta,(int)strlen(meta));
        ri.Printf(PRINT_ALL,"Surface current-frame float capture: %s (%llu bytes)\n",surface_pending_capture,(unsigned long long)bytes);
    }
    ri.Free(pixels); surface_pending_capture[0]=0;
}
