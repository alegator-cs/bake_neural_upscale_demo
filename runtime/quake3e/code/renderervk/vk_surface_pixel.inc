/* Separate pixel/subpixel-only output path. The LR renderer remains upstream;
 * no HR visibility, physical-center gate, guide reconstruction or matching is
 * dispatched here. Retained legacy allocations are reported as inactive. */
static uint64_t surface_pixel_memory(qboolean allocated, qboolean includePixel, qboolean includeFallback) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    const surfaceBuffer_t *buffers[8]={&s->triangles,&s->materials,&s->appearance,&s->correction,&s->bvh,&s->diagnostic,&s->upscale,&s->readback};
    const surfaceImage_t *images[10]={&s->id,&s->barycentric,&s->depth,&s->output,&s->lrID,&s->lrBarycentric,&s->lrLinear,&s->lrCenterGate,&s->hrCenterGate,&s->geometryLinear};
    uint64_t total=0;uint32_t i;
    for(i=0;i<8;i++)total+=allocated?buffers[i]->allocated:buffers[i]->requested;
    for(i=0;i<10;i++)total+=allocated?images[i]->allocated:images[i]->requested;
    if(includePixel){
        total+=allocated?s->pixelModel.allocated:s->pixelModel.requested;
        total+=allocated?(s->cnnModel.allocated+s->cnnDisplay.allocated+s->cnnConv0.allocated+s->cnnConv1.allocated):
            (s->cnnModel.requested+s->cnnDisplay.requested+s->cnnConv0.requested+s->cnnConv1.requested);
    }
    if(includeFallback)total+=allocated?vk_surface_fallback.allocated:vk_surface_fallback.requested;
    return total;
}
static qboolean surface_create_pixel(const void *model, VkDeviceSize bytes,
        uint32_t width, uint32_t parameters, VkShaderModule residual, VkShaderModule bilinear) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    VkDescriptorSetLayoutBinding bindings[3];
    VkDescriptorSetLayoutCreateInfo layout;
    VkDescriptorPoolSize sizes[3];
    VkDescriptorPoolCreateInfo pool;
    VkDescriptorSetAllocateInfo allocate;
    VkPipelineLayoutCreateInfo pipeline;
    VkPushConstantRange push;
    VkDescriptorBufferInfo buffer;
    VkDescriptorImageInfo images[2];
    VkWriteDescriptorSet writes[3];
    VkComputePipelineCreateInfo compute;
    uint32_t i;
    if(!s->fhdReady || !residual || !bilinear ||
            (model && !((width==0 && parameters==66) || (width==6 && parameters==153) || (width==12 && parameters==303))) ||
            (model && bytes!=256u+4u*parameters) || (!model && bytes))return qfalse;
    if(model)surface_upload_buffer(&s->pixelModel,model,bytes);
    Com_Memset(bindings,0,sizeof(bindings));
    bindings[0].binding=2;bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].binding=5;bindings[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].binding=11;bindings[2].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    for(i=0;i<3;i++) {bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
    Com_Memset(&layout,0,sizeof(layout));layout.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount=3;layout.pBindings=bindings;
    VK_CHECK(qvkCreateDescriptorSetLayout(vk.device,&layout,NULL,&s->pixelSetLayout));
    Com_Memset(&push,0,sizeof(push));push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;push.size=4*sizeof(uint32_t);
    Com_Memset(&pipeline,0,sizeof(pipeline));pipeline.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline.setLayoutCount=1;pipeline.pSetLayouts=&s->pixelSetLayout;pipeline.pushConstantRangeCount=1;pipeline.pPushConstantRanges=&push;
    VK_CHECK(qvkCreatePipelineLayout(vk.device,&pipeline,NULL,&s->pixelPipelineLayout));
    for(i=0;i<3;i++) {sizes[i].type=bindings[i].descriptorType;sizes[i].descriptorCount=1;}
    Com_Memset(&pool,0,sizeof(pool));pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets=1;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
    VK_CHECK(qvkCreateDescriptorPool(vk.device,&pool,NULL,&s->pixelPool));
    Com_Memset(&allocate,0,sizeof(allocate));allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool=s->pixelPool;allocate.descriptorSetCount=1;allocate.pSetLayouts=&s->pixelSetLayout;
    VK_CHECK(qvkAllocateDescriptorSets(vk.device,&allocate,&s->pixelDescriptor));
    Com_Memset(&buffer,0,sizeof(buffer));buffer.buffer=model?s->pixelModel.buffer:s->diagnostic.buffer;buffer.range=VK_WHOLE_SIZE;
    Com_Memset(images,0,sizeof(images));images[0].imageView=s->output.view;images[0].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    images[1].imageView=s->lrLinear.view;images[1].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;images[1].sampler=s->nearestSampler;
    Com_Memset(writes,0,sizeof(writes));
    for(i=0;i<3;i++) {
        writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=s->pixelDescriptor;
        writes[i].dstBinding=bindings[i].binding;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;
        if(!i)writes[i].pBufferInfo=&buffer;else writes[i].pImageInfo=&images[i-1];
    }
    qvkUpdateDescriptorSets(vk.device,3,writes,0,NULL);
    Com_Memset(&compute,0,sizeof(compute));compute.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;compute.layout=s->pixelPipelineLayout;
    set_shader_stage_desc(&compute.stage,VK_SHADER_STAGE_COMPUTE_BIT,residual,"main");
    VK_CHECK(qvkCreateComputePipelines(vk.device,vk.pipelineCache,1,&compute,NULL,&s->pixelResidualPipeline));
    set_shader_stage_desc(&compute.stage,VK_SHADER_STAGE_COMPUTE_BIT,bilinear,"main");
    VK_CHECK(qvkCreateComputePipelines(vk.device,vk.pipelineCache,1,&compute,NULL,&s->pixelBilinearPipeline));
    s->pixelModelReady=model!=NULL;s->pixelReady=qtrue;
    s->pixelWidth=model?width:0;s->pixelParameters=model?parameters:0;
    s->pixelMACs=model?(width?24u*width:63u):0;
    ri.Printf(PRINT_ALL,"Pixel-only resources: model %llu/%lluB requested/allocated; width%u params%u; no new images; retained inactive legacy buffer%llu/%lluB contents%s and HR guides%llu/%lluB\n",
        (unsigned long long)s->pixelModel.requested,(unsigned long long)s->pixelModel.allocated,s->pixelWidth,s->pixelParameters,
        (unsigned long long)s->upscale.requested,(unsigned long long)s->upscale.allocated,
        s->legacyModelReady?"verified_NLUP0001":"initialized_zero_placeholder",
        (unsigned long long)(s->id.requested+s->barycentric.requested+s->depth.requested+s->hrCenterGate.requested+s->geometryLinear.requested),
        (unsigned long long)(s->id.allocated+s->barycentric.allocated+s->depth.allocated+s->hrCenterGate.allocated+s->geometryLinear.allocated));
    ri.Printf(PRINT_ALL,"Pixel allocation ledger foundation %llu/%lluB; full surface+pixel+fallback %llu/%lluB requested/allocated (readback included; engine+driver objects excluded) GPU vendor%u device%u driver%u API%u maxImage2D%u presentation%s\n",
        (unsigned long long)surface_pixel_memory(qfalse,qfalse,qfalse),(unsigned long long)surface_pixel_memory(qtrue,qfalse,qfalse),
        (unsigned long long)surface_pixel_memory(qfalse,qtrue,qtrue),(unsigned long long)surface_pixel_memory(qtrue,qtrue,qtrue),
        s->fhdVendorID,s->fhdDeviceID,s->fhdDriverVersion,s->fhdAPIVersion,s->fhdMaxImageDimension2D,pmode_to_str(vk_surface_present_mode));
    return qtrue;
}

static void surface_pixel_dispatch(VkCommandBuffer cmd, uint32_t mode) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    uint32_t extent[4]={s->width,s->height,s->lrWidth,s->lrHeight};
    surface_image_barrier(cmd,s->output.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    qvkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,mode==8?s->pixelResidualPipeline:s->pixelBilinearPipeline);
    qvkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pixelPipelineLayout,0,1,&s->pixelDescriptor,0,NULL);
    qvkCmdPushConstants(cmd,s->pixelPipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(extent),extent);
    qvkCmdDispatch(cmd,(s->width+7)/8,(s->height+7)/8,1);
}
