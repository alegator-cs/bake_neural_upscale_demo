/* Mode9 acquisition, always genuinely rendered LR at native HR/4. No HR
 * visibility/center/geometry attachments are generated or read for this path. */
static void surface_multires_finish_capture(void) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    surfacePush_t lr=surface_last_push;
    uint32_t extent[4]={s->width,s->height,s->lrWidth,s->lrHeight};
    char arrays[8192],meta[16384],path[MAX_QPATH];
    size_t used=0;
    uint64_t lrPixels=(uint64_t)s->lrWidth*s->lrHeight;
    qboolean ok;
    if(!surface_assets_accepted || !surface_last_valid || !s->pixelReady || !s->outputWritten || !s->fhdLRCurrent ||
       s->fhdLastMode!=9 || r_surfaceUpscale->integer!=9 || s->fhdHRGuidesCurrent || s->hrDiagnosticWritten ||
       !surface_capture4x_extent(s->width,s->height) || s->width!=4*s->lrWidth || s->height!=4*s->lrHeight ||
       surface_last_push.info[0]!=s->width || surface_last_push.info[1]!=s->height ||
       glConfig.vidWidth!=(int)s->width || glConfig.vidHeight!=(int)s->height ||
       !r_surfaceBatchViewID || surface_last_batch_view_id<0 || surface_last_batch_view_id!=r_surfaceBatchViewID->integer ||
       SURFACE_BATCH_CAMERA_COUNT==0u)goto rejected;
    arrays[0]=0;lr.info[0]=s->lrWidth;lr.info[1]=s->lrHeight;
    ok=surface_fhd_write_image(&s->output,".rgba32f","hr_rgba","<f4",s->width,s->height,4,VK_IMAGE_LAYOUT_GENERAL,arrays,sizeof(arrays),&used) &&
       surface_fhd_write_image(&s->lrLinear,".lr.rgba32f","lr_rgba","<f4",s->lrWidth,s->lrHeight,4,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
       surface_fhd_write_image(&s->lrID,".lr.ids.u32","lr_ids","<u4",2*s->lrWidth,2*s->lrHeight,1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
       surface_fhd_write_image(&s->lrBarycentric,".lr.bary.f32","lr_bary","<f4",2*s->lrWidth,2*s->lrHeight,2,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
       surface_fhd_write_image(&s->lrCenterGate,".lr.center.u32","lr_center_ids","<u4",s->lrWidth,s->lrHeight,1,VK_IMAGE_LAYOUT_GENERAL,arrays,sizeof(arrays),&used) &&
       surface_fhd_write_array(".frame.hr.bin","hr_frame","|u1",96,0,1,&surface_last_push,sizeof(surface_last_push),arrays,sizeof(arrays),&used) &&
       surface_fhd_write_array(".frame.lr.bin","lr_frame","|u1",96,0,1,&lr,sizeof(lr),arrays,sizeof(arrays),&used) &&
       surface_fhd_write_array(".extent.bin","upscale_extent","|u1",16,0,1,extent,sizeof(extent),arrays,sizeof(arrays),&used);
    if(!ok)goto rejected;
    surface_fhd_timing_read();
    Com_sprintf(meta,sizeof(meta),"{\"schema\":\"NL4X_MULTIRES_RUNTIME_CAPTURE_1\",\"complete\":true,\"mode\":\"bilinear\",\"mode_index\":9,\"pair_name\":\"%s\",\"view_id\":%d,\"camera_manifest_sha256\":\"%s\",\"camera_table_sha256\":\"%s\",\"camera_first64_semantics\":\"frozen_float32_words_no_degree_roundtrip\",\"output_width\":%u,\"output_height\":%u,\"lr_width\":%u,\"lr_height\":%u,\"framebuffer_width\":%d,\"framebuffer_height\":%d,\"pixel_origin\":\"top_left\",\"quarter_order\":\"row_major_00_10_01_11\",\"lr_transfer\":\"linear_associated\",\"lr_center_semantics\":\"incident_bool01\",\"output_semantics\":\"display_associated\",\"hr_guides_valid\":false,\"hr_guide_semantics\":\"unavailable_not_generated\",\"lr_valid\":true,\"upscale_push_semantics\":\"extent_only_16B_no_camera\",\"model_format\":\"none\",\"model_asset_bytes\":0,\"model_parameters\":0,\"upscaler_sha256\":\"none\",\"geometry_sha256\":\"%s\",\"scene_sha256\":\"%s\",\"material_table_sha256\":\"%s\",\"baseline_sha256\":\"%s\",\"correction_sha256\":\"%s\",\"directional_base_sha256\":\"%s\",\"source_shader_receipt_sha256\":\"%s\",\"hr_bvh_quarter_rays\":0,\"hr_analytic_center_gates\":0,\"lr_bvh_quarter_rays\":%llu,\"lr_analytic_center_gates\":%llu,\"decoder_macs\":0,\"raw_array_bytes\":%llu,\"retained_inactive_hr_attachment_requested_bytes\":%llu,\"retained_inactive_hr_attachment_allocated_bytes\":%llu,\"surface_total_with_pixel_and_fallback_requested_bytes\":%llu,\"surface_total_with_pixel_and_fallback_allocated_bytes\":%llu,\"allocation_scope\":\"surface_with_readback_and_fallback_excludes_engine_driver_objects\",\"timing_current_frame\":%s,\"timings_ms\":{\"whole_gpu_frame\":%.9g,\"two_pass_total\":%.9g,\"lr_visibility\":%.9g,\"lr_appearance\":%.9g,\"hr_final\":%.9g},\"arrays\":{%s}}\n",
        s->lrWidth==480u?"A480":s->lrWidth==240u?"B240":"C128",surface_last_batch_view_id,SURFACE_BATCH_CAMERA_MANIFEST_SHA,SURFACE_BATCH_CAMERA_TABLE_SHA,
        s->width,s->height,s->lrWidth,s->lrHeight,glConfig.vidWidth,glConfig.vidHeight,
        surface_geometry_sha,surface_scene_sha,surface_material_sha,surface_base_sha,surface_correction_sha,surface_base_sha,SURFACE_SHADER_RECEIPT_SHA,
        (unsigned long long)(4*lrPixels),(unsigned long long)lrPixels,
        (unsigned long long)(16*(uint64_t)s->width*s->height+68*lrPixels+208),
        (unsigned long long)(s->id.requested+s->barycentric.requested+s->depth.requested+s->hrCenterGate.requested+s->geometryLinear.requested),
        (unsigned long long)(s->id.allocated+s->barycentric.allocated+s->depth.allocated+s->hrCenterGate.allocated+s->geometryLinear.allocated),
        (unsigned long long)surface_pixel_memory(qfalse,qtrue,qtrue),(unsigned long long)surface_pixel_memory(qtrue,qtrue,qtrue),
        s->timingReady?"true":"false",surface_fhd_timestamp_ms(0,7),surface_fhd_timestamp_ms(1,5),
        surface_fhd_timestamp_ms(1,2),surface_fhd_timestamp_ms(2,3),surface_fhd_timestamp_ms(4,5),arrays);
    Com_sprintf(path,sizeof(path),"%s.json",surface_pending_capture);ri.FS_WriteFile(path,meta,(int)strlen(meta));
    ri.Printf(PRINT_ALL,"Multi-resolution capture complete:%s view%d actual%ux%u/LR%ux%u; HRrays/gates/guides0\n",surface_pending_capture,surface_last_batch_view_id,s->width,s->height,s->lrWidth,s->lrHeight);
    surface_pending_capture[0]=0;return;
rejected:
    {const char failed[]="{\"schema\":\"NL4X_MULTIRES_RUNTIME_CAPTURE_1\",\"complete\":false,\"status\":\"rejected_or_partial_current_frame\"}\n";
     Com_sprintf(path,sizeof(path),"%s.json",surface_pending_capture);ri.FS_WriteFile(path,failed,sizeof(failed)-1);}
    surface_pending_capture[0]=0;
}
