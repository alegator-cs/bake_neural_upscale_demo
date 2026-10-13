/* New CNN family capture: actual final+LR input only; no HR guide/debug readback. */
static void surface_cnn_finish_capture(void) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    surfacePush_t lr=surface_last_push;
    uint32_t extent[4]={1920,1080,480,270};
    char arrays[8192],extra[4096],meta[24576],path[MAX_QPATH];
    size_t used=0;
    qboolean learned=s->fhdLastMode==10,ok;
    if(!surface_assets_accepted || !surface_last_valid || !s->pixelReady || !s->outputWritten || !s->fhdLRCurrent ||
            s->fhdLastMode!=(uint32_t)r_surfaceUpscale->integer || (s->fhdLastMode!=10 && s->fhdLastMode!=11) ||
            (learned && !s->cnnReady) || s->fhdHRGuidesCurrent || s->hrDiagnosticWritten ||
            surface_last_push.info[0]!=1920 || surface_last_push.info[1]!=1080)goto rejected;
    arrays[0]=0;lr.info[0]=480;lr.info[1]=270;
    ok=surface_fhd_write_image(&s->output,".rgba32f","hr_rgba","<f4",1920,1080,4,VK_IMAGE_LAYOUT_GENERAL,arrays,sizeof(arrays),&used) &&
        surface_fhd_write_image(&s->lrLinear,".lr.rgba32f","lr_rgba","<f4",480,270,4,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
        surface_fhd_write_image(&s->lrID,".lr.ids.u32","lr_ids","<u4",960,540,1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
        surface_fhd_write_image(&s->lrBarycentric,".lr.bary.f32","lr_bary","<f4",960,540,2,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,arrays,sizeof(arrays),&used) &&
        surface_fhd_write_image(&s->lrCenterGate,".lr.center.u32","lr_center_ids","<u4",480,270,1,VK_IMAGE_LAYOUT_GENERAL,arrays,sizeof(arrays),&used) &&
        surface_fhd_write_array(".frame.hr.bin","hr_frame","|u1",96,0,1,&surface_last_push,sizeof(surface_last_push),arrays,sizeof(arrays),&used) &&
        surface_fhd_write_array(".frame.lr.bin","lr_frame","|u1",96,0,1,&lr,sizeof(lr),arrays,sizeof(arrays),&used) &&
        surface_fhd_write_array(".extent.bin","upscale_extent","|u1",16,0,1,extent,sizeof(extent),arrays,sizeof(arrays),&used);
    if(!ok)goto rejected;
    surface_fhd_timing_read();

    if(s->timingReady && s->timingMode!=s->fhdLastMode)s->timingReady=qfalse;
    Com_sprintf(extra,sizeof(extra),
        "\"cnn_protocol_sha256\":\"%s\",\"base_sha256\":\"%s\",\"appearance_identity\":\"%s\",\"atlas_texels\":%u,\"material_table_sha256\":\"c5ff9715587f4ae1bc0d31c9c3bc98cdaa802a53e1a91e9950f0df468b049f16\",\"upscale_dispatches\":%u,\"cnn_dispatches\":%u,\"cnn_macs_per_LR_pixel\":%u,\"cnn_active_buffers\":%s,\"cnn_buffers\":{\"model\":{\"requested\":%llu,\"allocated\":%llu},\"LR_display_RGBA\":{\"requested\":%llu,\"allocated\":%llu},\"conv0\":{\"requested\":%llu,\"allocated\":%llu},\"conv1\":{\"requested\":%llu,\"allocated\":%llu}},\"extra_query_semantics\":\"%s\",\"query_chronological_order\":[0,1,2,3,4,8,9,10,11,5,6,7],\"query_equal_boundary_ticks_allowed\":true,\"cnn_timings_ms\":{\"LR_display\":%.9g,\"conv0\":%.9g,\"conv1\":%.9g,\"phase_head\":%.9g}",
        learned?SURFACE_CNN_PROTOCOL_SHA:"none",surface_base_sha,surface_selected_identity?surface_selected_identity->name:"none",surface_selected_identity?surface_selected_identity->texels:0u,learned?4u:1u,learned?4u:0u,learned?s->cnnMACsPerLR:0u,learned?"true":"false",
        (unsigned long long)s->cnnModel.requested,(unsigned long long)s->cnnModel.allocated,
        (unsigned long long)s->cnnDisplay.requested,(unsigned long long)s->cnnDisplay.allocated,
        (unsigned long long)s->cnnConv0.requested,(unsigned long long)s->cnnConv0.allocated,
        (unsigned long long)s->cnnConv1.requested,(unsigned long long)s->cnnConv1.allocated,
        learned?"CNN_four_dispatch_boundaries":"bilinear_boundary_markers_no_CNN_dispatch",
        learned?surface_fhd_timestamp_ms(4,8):-1.,learned?surface_fhd_timestamp_ms(8,9):-1.,
        learned?surface_fhd_timestamp_ms(9,10):-1.,learned?surface_fhd_timestamp_ms(10,11):-1.);
    Com_sprintf(meta,sizeof(meta),"{\"schema\":\"NLR4_FHD_CAPTURE_1\",\"complete\":true,\"mode\":\"%s\",\"mode_index\":%u,\"optimization\":\"LR_shared_CNN_pixels_and_subpixel_only\",\"production_choice\":\"%s\",\"output_width\":1920,\"output_height\":1080,\"lr_width\":480,\"lr_height\":270,\"framebuffer_width\":%d,\"framebuffer_height\":%d,\"window_width\":%d,\"window_height\":%d,\"pixel_origin\":\"top_left\",\"quarter_order\":\"row_major_00_10_01_11\",\"lr_transfer\":\"linear_associated\",\"lr_center_semantics\":\"incident_bool01\",\"output_semantics\":\"display_associated\",\"hr_guides_valid\":false,\"hr_guide_semantics\":\"unavailable_not_generated\",\"lr_valid\":true,\"upscale_push_semantics\":\"extent_only_16B_no_camera\",\"model_format\":\"%s\",\"upscaler_sha256\":\"%s\",\"model_sha256\":\"%s\",\"model_hidden_width\":%u,\"model_parameters\":%u,\"model_asset_bytes\":%u,\"model_payload_bytes\":%u,\"model_gpu_requested_bytes\":%llu,\"model_gpu_allocated_bytes\":%llu,\"model_diagnostic\":%s,\"model_quality_status\":\"%s\",\"model_code_sha256\":\"%s\",\"baseline_sha256\":\"%s\",\"geometry_sha256\":\"%s\",\"scene_sha256\":\"%s\",\"correction_sha256\":\"%s\",\"source_shader_receipt_sha256\":\"%s\",\"new_texture_bytes\":0,\"hr_bvh_quarter_rays\":0,\"hr_bvh_quarter_ray_upper_bound\":0,\"hr_quarter_invocations\":0,\"hr_analytic_center_gates\":0,\"lr_bvh_quarter_rays\":518400,\"lr_analytic_center_gates\":129600,\"decoder_macs_per_pixel\":%u,\"dense_decoder_macs\":%llu,\"dense_decoder_macs_semantics\":\"shared_LR_encoders_plus_phase_head_full_frame_upper_bound\",\"retained_inactive_pixel_model_requested_bytes\":%llu,\"retained_inactive_pixel_model_allocated_bytes\":%llu,\"retained_inactive_legacy_buffer_requested_bytes\":%llu,\"retained_inactive_legacy_buffer_allocated_bytes\":%llu,\"retained_inactive_legacy_buffer_contents\":\"%s\",\"retained_inactive_hr_attachment_requested_bytes\":%llu,\"retained_inactive_hr_attachment_allocated_bytes\":%llu,\"surface_foundation_requested_bytes\":%llu,\"surface_foundation_allocated_bytes\":%llu,\"surface_total_with_pixel_and_fallback_requested_bytes\":%llu,\"surface_total_with_pixel_and_fallback_allocated_bytes\":%llu,\"allocation_scope\":\"surface_with_CNN_retained_models_readback_and_fallback_excludes_engine_driver_objects\",\"gpu_vendor_id\":%u,\"gpu_device_id\":%u,\"gpu_driver_version\":%u,\"gpu_api_version\":%u,\"gpu_max_image_dimension_2d\":%u,\"presentation_mode\":\"%s\",\"timestamp_valid_bits\":%u,\"timestamp_period_ns\":%.9g,\"timing_current_frame\":%s,\"frame_slot\":%d,\"timings_ms\":{\"whole_gpu_frame\":%.9g,\"two_pass_total\":%.9g,\"lr_visibility\":%.9g,\"lr_appearance\":%.9g,\"hr_visibility\":%.9g,\"hr_final\":%.9g,\"engine_composition\":%.9g,\"presentation_draws\":%.9g},%s,\"arrays\":{%s}}\n",
        learned?"cnn_residual":"bilinear",s->fhdLastMode,SURFACE_CNN_PRODUCTION_CHOICE,glConfig.vidWidth,glConfig.vidHeight,gls.windowWidth,gls.windowHeight,
        learned?"NLR40001":"none",learned?surface_cnn_sha_for_width(s->cnnWidth):"none",learned?surface_cnn_sha_for_width(s->cnnWidth):"none",
        learned?s->cnnWidth:0u,learned?s->cnnParameters:0u,learned?(uint32_t)s->cnnModel.requested:0u,learned?4u*s->cnnParameters:0u,
        learned?(unsigned long long)s->cnnModel.requested:0ULL,learned?(unsigned long long)s->cnnModel.allocated:0ULL,
        learned && SURFACE_CNN_DIAGNOSTIC?"true":"false",learned?SURFACE_CNN_QUALITY_STATUS:"canonical_bilinear_comparison",
        learned?SURFACE_CNN_CODE_SHA:"none",surface_correction_sha,surface_geometry_sha,surface_scene_sha,surface_correction_sha,SURFACE_SHADER_RECEIPT_SHA,
        learned?s->cnnMACsPerLR/16u:0u,learned?(unsigned long long)s->cnnMACsPerLR*480u*270u:0ULL,
        (unsigned long long)s->pixelModel.requested,(unsigned long long)s->pixelModel.allocated,
        (unsigned long long)s->upscale.requested,(unsigned long long)s->upscale.allocated,s->legacyModelReady?"verified_NLUP0001":"initialized_zero_placeholder",
        (unsigned long long)(s->id.requested+s->barycentric.requested+s->depth.requested+s->hrCenterGate.requested+s->geometryLinear.requested),
        (unsigned long long)(s->id.allocated+s->barycentric.allocated+s->depth.allocated+s->hrCenterGate.allocated+s->geometryLinear.allocated),
        (unsigned long long)surface_pixel_memory(qfalse,qfalse,qfalse),(unsigned long long)surface_pixel_memory(qtrue,qfalse,qfalse),
        (unsigned long long)surface_pixel_memory(qfalse,qtrue,qtrue),(unsigned long long)surface_pixel_memory(qtrue,qtrue,qtrue),
        s->fhdVendorID,s->fhdDeviceID,s->fhdDriverVersion,s->fhdAPIVersion,s->fhdMaxImageDimension2D,pmode_to_str(vk_surface_present_mode),s->timestampValidBits,s->timestampPeriod,
        s->timingReady?"true":"false",vk.cmd_index,
        surface_fhd_timestamp_ms(0,7),surface_fhd_timestamp_ms(1,5),surface_fhd_timestamp_ms(1,2),surface_fhd_timestamp_ms(2,3),surface_fhd_timestamp_ms(3,4),surface_fhd_timestamp_ms(4,5),surface_fhd_timestamp_ms(5,6),surface_fhd_timestamp_ms(6,7),extra,arrays);
    Com_sprintf(path,sizeof(path),"%s.json",surface_pending_capture);ri.FS_WriteFile(path,meta,(int)strlen(meta));
    ri.Printf(PRINT_ALL,"CNN current-frame capture complete: %s mode%u model%s diagnostic%u; HR rays/gates/guides0\n",surface_pending_capture,s->fhdLastMode,learned?surface_cnn_sha_for_width(s->cnnWidth):"none",learned?SURFACE_CNN_DIAGNOSTIC:0u);
    surface_pending_capture[0]=0;return;
rejected:
    {const char failed[]="{\"schema\":\"NLR4_FHD_CAPTURE_1\",\"complete\":false,\"status\":\"rejected_or_partial_current_frame\"}\n";
        Com_sprintf(path,sizeof(path),"%s.json",surface_pending_capture);ri.FS_WriteFile(path,failed,sizeof(failed)-1);}
    ri.Printf(PRINT_WARNING,"CNN capture rejected/partial; no valid current arrays completion\n");surface_pending_capture[0]=0;
}
