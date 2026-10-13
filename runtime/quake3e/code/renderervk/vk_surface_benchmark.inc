/* Bounded noncapture collector. No query WAIT, image readback or queue changes.
 * A CSV batch is flushed once; mode, pose and camera belong to the submitted
 * command slot, never to the cvars at query-read time. */
#define SURFACE_BENCH_MAX 512u
#define SURFACE_BENCH_WARM 8u
#define SURFACE_BENCH_BYTES (1024u*1024u)
static uint64_t surface_pixel_memory(qboolean allocated,qboolean includePixel,qboolean includeFallback);
static double surface_fhd_timestamp_ms(uint32_t begin, uint32_t end);
static struct {
    qboolean active, cpuActive;
    uint32_t epoch, target, warm, count, attempts, mode;
    int pose;
    uint64_t lastSerial;
    size_t used;
    char path[MAX_QPATH], text[SURFACE_BENCH_BYTES];
    double frameStart, lastPresent, queueIdle;
} surface_benchmark;

static double surface_cpu_clock_ms(void) {
#if defined(_WIN32)
    LARGE_INTEGER now;
    static LARGE_INTEGER frequency;
    if(!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    if(frequency.QuadPart) return 1000.0*(double)now.QuadPart/(double)frequency.QuadPart;
#endif
    return (double)ri.Milliseconds();
}

static void surface_benchmark_finish(const char *status) {
    char end[512]; size_t length;
    if(!surface_benchmark.active)return;
    Com_sprintf(end,sizeof(end),"# status=%s samples=%u requested=%u warm=%u attempts=%u; CPU wall includes pacing/acquire/fence/queue-idle/present; GPU queries exclude those host waits and scanout\n",
        status,surface_benchmark.count,surface_benchmark.target,surface_benchmark.warm,surface_benchmark.attempts);
    length=strlen(end);
    if(surface_benchmark.used+length<sizeof(surface_benchmark.text)) {
        Com_Memcpy(surface_benchmark.text+surface_benchmark.used,end,length);
        surface_benchmark.used+=length;
    }
    ri.FS_WriteFile(surface_benchmark.path,surface_benchmark.text,(int)surface_benchmark.used);
    ri.Printf(PRINT_ALL,"Surface benchmark %s: %u/%u samples, one CSV %s (%u bytes); CPU contention/pacing included\n",
        status,surface_benchmark.count,surface_benchmark.target,surface_benchmark.path,(unsigned)surface_benchmark.used);
    surface_benchmark.active=qfalse;
    ri.Cvar_Set("r_surfaceBenchmark","0");
    if(r_surfaceBenchmark)r_surfaceBenchmark->modified=qfalse;
}

static void surface_benchmark_poll(qboolean allowArm) {
    const char *path;
    int requested;
    qboolean restartRequested;
    if(!r_surfaceBenchmark)return;
    requested=r_surfaceBenchmark->integer;
    restartRequested=r_surfaceBenchmark->modified || (r_surfaceBenchmarkFile && r_surfaceBenchmarkFile->modified);
    if(surface_benchmark.active && requested==0) {surface_benchmark_finish("cancelled");return;}
    if(surface_benchmark.active && (!r_surfaceUpscale || (uint32_t)r_surfaceUpscale->integer!=surface_benchmark.mode ||
            (r_surfacePose?r_surfacePose->integer:0)!=surface_benchmark.pose) && !restartRequested) {
        surface_benchmark_finish("mode_or_pose_changed");return;
    }
    if(surface_benchmark.active && !restartRequested && surface_benchmark.mode==10 && r_surfaceCnnModel->modified) {
        surface_benchmark_finish("model_changed");return;
    }
    if(!restartRequested)return;
    /* A startup batch must wait for actual timestamp properties/resources.
     * Keep the request modified until the first initialized prepare point. */
    /* The early phase cancels stale batches but cannot snapshot allocation
     * totals. Arm only after successful current-model resource preparation. */
    if(requested>0 && (!allowArm || !vk_surface_renderer.fhdReady))return;
    r_surfaceBenchmark->modified=qfalse;
    if(r_surfaceBenchmarkFile)r_surfaceBenchmarkFile->modified=qfalse;
    if(surface_benchmark.active)surface_benchmark_finish("superseded");
    if(!requested)return;
    if(requested<1 || requested>(int)SURFACE_BENCH_MAX || !r_surfaceUpscale ||
            r_surfaceUpscale->integer<1 || r_surfaceUpscale->integer>11) {
        ri.Printf(PRINT_WARNING,"Surface benchmark rejected: N1..512 and FHD mode1..11 required\n");
        ri.Cvar_Set("r_surfaceBenchmark","0");r_surfaceBenchmark->modified=qfalse;return;
    }
    path=r_surfaceBenchmarkFile?r_surfaceBenchmarkFile->string:"benchmarks/surface-timing.csv";
    if(!path[0] || strlen(path)>=MAX_QPATH || strstr(path,"..") || strchr(path,':') || path[0]=='/' || path[0]=='\\') {
        ri.Printf(PRINT_WARNING,"Surface benchmark rejected: bounded game-relative CSV path required\n");
        ri.Cvar_Set("r_surfaceBenchmark","0");r_surfaceBenchmark->modified=qfalse;return;
    }
    surface_benchmark.active=qtrue;
    if(!++surface_benchmark.epoch)++surface_benchmark.epoch;
    surface_benchmark.target=(uint32_t)requested;
    surface_benchmark.warm=surface_benchmark.count=surface_benchmark.attempts=0;
    surface_benchmark.mode=(uint32_t)r_surfaceUpscale->integer;
    surface_benchmark.pose=r_surfacePose?r_surfacePose->integer:0;
    surface_benchmark.lastSerial=0;
    Q_strncpyz(surface_benchmark.path,path,sizeof(surface_benchmark.path));
    Com_sprintf(surface_benchmark.text,sizeof(surface_benchmark.text),
        "# schema=NLR4_NONCAPTURE_TIMING_1 warm_required=8 max_samples=512 max_attempts=2048 timestamp_period_ns=%.9g timestamp_valid_bits=%u shader_receipt=%s\n"
        "# gpu_vendor_id=%u gpu_device_id=%u driver_version=%u api_version=%u max_image_dim2D=%u presentation_mode=%s foundation_requested_bytes=%llu foundation_allocated_bytes=%llu total_surface_with_pixel_fallback_requested_bytes=%llu total_surface_with_pixel_fallback_allocated_bytes=%llu (engine_driver_objects_excluded)\n"
        "# Extra-query chronology=new modes10/11:0,1,2,3,4,8,9,10,11,5,6,7; q4-to-q5 contains CNN substages, equal boundary ticks allowed; old modes extra queries unavailable.\n"
        "# CPU clock=QPC_on_Windows; wall=inter_present (first frame begin_to_present); GPU=bottom_of_pipe query intervals, not display FPS; no image readbacks\n"
        "sample,serial,slot,mode,pose,cpu_wall_ms,cpu_queue_idle_ms,whole_gpu_frame_ms,two_pass_ms,lr_visibility_ms,lr_appearance_ms,hr_visibility_ms,hr_final_ms,composition_ms,presentation_draws_ms,q0,q1,q2,q3,q4,q5,q6,q7,origin_x,origin_y,origin_z,origin_w,forward_x,forward_y,forward_z,tan_x,right_x,right_y,right_z,tan_y,up_x,up_y,up_z,near,info_w,info_h,info_flags,triangles,reserved0,reserved1,reserved2,reserved3,hr_quarter_ray_upper_bound,lr_quarter_rays,hr_center_gate_upper_bound,model_format,model_sha256,model_hidden_width,model_parameters,model_asset_bytes,decoder_macs_per_pixel,decoder_macs_upper_bound,pixel_model_diagnostic,q8,q9,q10,q11,cnn_queries_available,cnn_substages_dispatched,extra_query_semantics,cnn_display_ms,cnn_conv0_ms,cnn_conv1_ms,cnn_head_ms\n",
        vk_surface_renderer.timestampPeriod,vk_surface_renderer.timestampValidBits,SURFACE_SHADER_RECEIPT_SHA,
        vk_surface_renderer.fhdVendorID,vk_surface_renderer.fhdDeviceID,vk_surface_renderer.fhdDriverVersion,vk_surface_renderer.fhdAPIVersion,
        vk_surface_renderer.fhdMaxImageDimension2D,pmode_to_str(vk_surface_present_mode),
        (unsigned long long)surface_pixel_memory(qfalse,qfalse,qfalse),(unsigned long long)surface_pixel_memory(qtrue,qfalse,qfalse),
        (unsigned long long)surface_pixel_memory(qfalse,qtrue,qtrue),(unsigned long long)surface_pixel_memory(qtrue,qtrue,qtrue));
    surface_benchmark.used=strlen(surface_benchmark.text);
    ri.Cvar_Set("r_surfaceBenchmark",va("%d",requested));r_surfaceBenchmark->modified=qfalse;
    ri.Printf(PRINT_ALL,"Surface benchmark armed: mode%u pose%d, 8 warm + %u samples; %s\n",
        surface_benchmark.mode,surface_benchmark.pose,surface_benchmark.target,surface_benchmark.path);
}

static void surface_benchmark_collect(void) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    surfacePush_t *p;
    char row[2048];size_t length;
    uint32_t slot=(uint32_t)vk.cmd_index;
    if(!surface_benchmark.active)return;
    /* A prior batch must not finish and clear the positive request which the
     * late preparation phase will arm after current-model validation. */
    if(r_surfaceBenchmark && r_surfaceBenchmark->integer>0 &&
            (r_surfaceBenchmark->modified || (r_surfaceBenchmarkFile && r_surfaceBenchmarkFile->modified)))return;
    if(++surface_benchmark.attempts>2048u) {surface_benchmark_finish("attempt_limit_or_queries_unavailable");return;}
    if(!s->timingReady || !s->querySlotSubmitted[slot] || !s->querySlotCPUReady[slot] ||
            s->querySlotCaptured[slot] || s->querySlotBenchmarkEpoch[slot]!=surface_benchmark.epoch ||
            s->timingSerial<=surface_benchmark.lastSerial || s->timingMode!=surface_benchmark.mode ||
            s->querySlotPose[slot]!=surface_benchmark.pose)return;
    surface_benchmark.lastSerial=s->timingSerial;
    if(surface_benchmark.warm<SURFACE_BENCH_WARM) {++surface_benchmark.warm;return;}
    p=&s->querySlotPush[slot];
    Com_sprintf(row,sizeof(row),"%u,%llu,%u,%u,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%s,%s,%u,%u,%u,%u,%llu,%u,%llu,%llu,%llu,%llu,%u,%u,%s,%.9g,%.9g,%.9g,%.9g\n",
        surface_benchmark.count+1,(unsigned long long)s->timingSerial,slot,s->timingMode,s->querySlotPose[slot],
        s->querySlotCPUWall[slot],s->querySlotCPUQueueIdle[slot],surface_fhd_timestamp_ms(0,7),surface_fhd_timestamp_ms(1,5),
        surface_fhd_timestamp_ms(1,2),surface_fhd_timestamp_ms(2,3),surface_fhd_timestamp_ms(3,4),surface_fhd_timestamp_ms(4,5),
        surface_fhd_timestamp_ms(5,6),surface_fhd_timestamp_ms(6,7),
        (unsigned long long)s->timestampValues[0],(unsigned long long)s->timestampValues[1],(unsigned long long)s->timestampValues[2],(unsigned long long)s->timestampValues[3],
        (unsigned long long)s->timestampValues[4],(unsigned long long)s->timestampValues[5],(unsigned long long)s->timestampValues[6],(unsigned long long)s->timestampValues[7],
        p->origin[0],p->origin[1],p->origin[2],p->origin[3],p->forwardTanX[0],p->forwardTanX[1],p->forwardTanX[2],p->forwardTanX[3],
        p->rightTanY[0],p->rightTanY[1],p->rightTanY[2],p->rightTanY[3],p->upNear[0],p->upNear[1],p->upNear[2],p->upNear[3],
        p->info[0],p->info[1],p->info[2],p->info[3],p->reserved[0],p->reserved[1],p->reserved[2],p->reserved[3],
        s->timingMode==2 || s->timingMode==6 || s->timingMode>=8?0u:8294400u,s->timingMode==4?0u:518400u,
        s->timingMode>=8?0u:s->timingMode==7?4147200u:2073600u,
        s->timingMode==10?"NLR40001":s->timingMode==8?"NLUR0001":"none",
        s->timingMode==10?s->querySlotCnnSHA[slot]:s->timingMode==8?SURFACE_PIXEL_ASSET_SHA:"none",
        s->querySlotPixelWidth[slot],s->querySlotPixelParameters[slot],s->querySlotPixelBytes[slot],s->querySlotPixelMACs[slot],
        (unsigned long long)s->querySlotPixelMACs[slot]*p->info[0]*p->info[1],s->querySlotPixelDiagnostic[slot],
        (unsigned long long)s->timestampValues[8],(unsigned long long)s->timestampValues[9],
        (unsigned long long)s->timestampValues[10],(unsigned long long)s->timestampValues[11],
        s->timingMode>=10?1u:0u,s->timingMode==10?1u:0u,
        s->timingMode==10?"CNN_four_dispatch_boundaries":s->timingMode==11?"bilinear_boundary_markers_no_CNN_dispatch":"unavailable",
        s->timingMode==10?surface_fhd_timestamp_ms(4,8):-1.,s->timingMode==10?surface_fhd_timestamp_ms(8,9):-1.,
        s->timingMode==10?surface_fhd_timestamp_ms(9,10):-1.,s->timingMode==10?surface_fhd_timestamp_ms(10,11):-1.);
    length=strlen(row);
    if(surface_benchmark.used+length+512>=sizeof(surface_benchmark.text)) {surface_benchmark_finish("buffer_limit");return;}
    Com_Memcpy(surface_benchmark.text+surface_benchmark.used,row,length);surface_benchmark.used+=length;
    if(++surface_benchmark.count>=surface_benchmark.target)surface_benchmark_finish("complete");
}

static void surface_benchmark_cpu_begin(void) {
    double now;
    surface_benchmark.cpuActive=surface_benchmark.active || (r_surfaceBenchmark && r_surfaceBenchmark->integer>0);
    if(!surface_benchmark.cpuActive)return;
    now=surface_cpu_clock_ms();
    surface_benchmark.frameStart=surface_benchmark.lastPresent>0?surface_benchmark.lastPresent:now;
    surface_benchmark.queueIdle=0;
}

static void surface_benchmark_cpu_present(void) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    double now;
    if(!surface_benchmark.cpuActive)return;
    now=surface_cpu_clock_ms();surface_benchmark.lastPresent=now;
    if(s->querySlotSubmitted[vk.cmd_index]) {
        s->querySlotCPUWall[vk.cmd_index]=now-surface_benchmark.frameStart;
        s->querySlotCPUQueueIdle[vk.cmd_index]=surface_benchmark.queueIdle;
        s->querySlotCPUReady[vk.cmd_index]=qtrue;
    }
    surface_benchmark.cpuActive=qfalse;
}

static void surface_benchmark_present_interrupted(void) {
    surface_benchmark_finish("presentation_interrupted");
    surface_benchmark.lastPresent=0;
    surface_benchmark.cpuActive=qfalse;
}
