/* Four pixel-only dispatches: display conversion, two shared LR convolutions,
 * HR phase head. No camera/geometry bindings or HR visibility work. */
static void surface_cnn_buffer_barrier(VkCommandBuffer cmd,surfaceBuffer_t *buffer,VkAccessFlags source,VkAccessFlags target) {
    VkBufferMemoryBarrier barrier;
    Com_Memset(&barrier,0,sizeof(barrier));barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask=source;barrier.dstAccessMask=target;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer=buffer->buffer;barrier.size=VK_WHOLE_SIZE;
    qvkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,1,&barrier,0,NULL);
}

static qboolean surface_create_cnn(const void *model,VkDeviceSize bytes,uint32_t width,uint32_t parameters,
        VkShaderModule display,VkShaderModule conv0,VkShaderModule conv1,VkShaderModule head) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    VkDescriptorSetLayoutBinding bindings[6];VkDescriptorSetLayoutCreateInfo layout;
    VkDescriptorPoolSize sizes[3];VkDescriptorPoolCreateInfo pool;VkDescriptorSetAllocateInfo allocate;
    VkPipelineLayoutCreateInfo pipeline;VkPushConstantRange push;VkDescriptorBufferInfo buffers[4];
    VkDescriptorImageInfo images[2];VkWriteDescriptorSet writes[6];VkComputePipelineCreateInfo compute;
    VkSpecializationMapEntry entry;VkSpecializationInfo specialization;
    VkShaderModule modules[4]={display,conv0,conv1,head};
    VkPipeline *pipelines[4]={&s->cnnDisplayPipeline,&s->cnnConv0Pipeline,&s->cnnConv1Pipeline,&s->cnnHeadPipeline};
    VkDeviceSize latentBytes;uint32_t i;
    if(!s->fhdReady || !model || (width!=8 && width!=16 && width!=32) ||
            parameters!=9u*width*width+102u*width+64u || bytes!=256u+4u*parameters ||
            !display || !conv0 || !conv1 || !head)return qfalse;
    latentBytes=(VkDeviceSize)SURFACE_LR_WIDTH*SURFACE_LR_HEIGHT*width*sizeof(float);
    surface_upload_buffer(&s->cnnModel,model,bytes);
    surface_allocate_buffer(&s->cnnDisplay,(VkDeviceSize)SURFACE_LR_WIDTH*SURFACE_LR_HEIGHT*16u,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    surface_allocate_buffer(&s->cnnConv0,latentBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    surface_allocate_buffer(&s->cnnConv1,latentBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Com_Memset(bindings,0,sizeof(bindings));
    bindings[0].binding=2;bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].binding=5;bindings[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].binding=11;bindings[2].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    for(i=3;i<6;i++){bindings[i].binding=12+i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;}
    for(i=0;i<6;i++){bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
    Com_Memset(&layout,0,sizeof(layout));layout.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount=6;layout.pBindings=bindings;
    VK_CHECK(qvkCreateDescriptorSetLayout(vk.device,&layout,NULL,&s->cnnSetLayout));
    Com_Memset(&push,0,sizeof(push));push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;push.size=16;
    Com_Memset(&pipeline,0,sizeof(pipeline));pipeline.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline.setLayoutCount=1;pipeline.pSetLayouts=&s->cnnSetLayout;pipeline.pushConstantRangeCount=1;pipeline.pPushConstantRanges=&push;
    VK_CHECK(qvkCreatePipelineLayout(vk.device,&pipeline,NULL,&s->cnnPipelineLayout));
    sizes[0].type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;sizes[0].descriptorCount=4;
    sizes[1].type=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;sizes[1].descriptorCount=1;
    sizes[2].type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;sizes[2].descriptorCount=1;
    Com_Memset(&pool,0,sizeof(pool));pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets=1;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
    VK_CHECK(qvkCreateDescriptorPool(vk.device,&pool,NULL,&s->cnnPool));
    Com_Memset(&allocate,0,sizeof(allocate));allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool=s->cnnPool;allocate.descriptorSetCount=1;allocate.pSetLayouts=&s->cnnSetLayout;
    VK_CHECK(qvkAllocateDescriptorSets(vk.device,&allocate,&s->cnnDescriptor));
    Com_Memset(buffers,0,sizeof(buffers));buffers[0].buffer=s->cnnModel.buffer;buffers[1].buffer=s->cnnDisplay.buffer;
    buffers[2].buffer=s->cnnConv0.buffer;buffers[3].buffer=s->cnnConv1.buffer;
    for(i=0;i<4;i++)buffers[i].range=VK_WHOLE_SIZE;
    Com_Memset(images,0,sizeof(images));images[0].imageView=s->output.view;images[0].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    images[1].imageView=s->lrLinear.view;images[1].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;images[1].sampler=s->nearestSampler;
    Com_Memset(writes,0,sizeof(writes));
    for(i=0;i<6;i++){
        writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=s->cnnDescriptor;
        writes[i].dstBinding=bindings[i].binding;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;
        if(i==0)writes[i].pBufferInfo=&buffers[0];else if(i==1 || i==2)writes[i].pImageInfo=&images[i-1];
        else writes[i].pBufferInfo=&buffers[i-2];
    }
    qvkUpdateDescriptorSets(vk.device,6,writes,0,NULL);
    Com_Memset(&entry,0,sizeof(entry));entry.constantID=0;entry.size=sizeof(width);
    Com_Memset(&specialization,0,sizeof(specialization));specialization.mapEntryCount=1;specialization.pMapEntries=&entry;
    specialization.dataSize=sizeof(width);specialization.pData=&width;
    Com_Memset(&compute,0,sizeof(compute));compute.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;compute.layout=s->cnnPipelineLayout;
    for(i=0;i<4;i++){
        set_shader_stage_desc(&compute.stage,VK_SHADER_STAGE_COMPUTE_BIT,modules[i],"main");
        compute.stage.pSpecializationInfo=i?&specialization:NULL;
        VK_CHECK(qvkCreateComputePipelines(vk.device,vk.pipelineCache,1,&compute,NULL,pipelines[i]));
    }
    s->cnnWidth=width;s->cnnParameters=parameters;s->cnnMACsPerLR=9u*width*width+100u*width;s->cnnReady=qtrue;
    ri.Printf(PRINT_ALL,"CNN4pass ready C%u parameters%u model%llu/%lluB display%llu/%lluB conv0%llu/%lluB conv1%llu/%lluB requested/allocated; MACs/frame%llu; HR rays/gates/guides0\n",width,parameters,
        (unsigned long long)s->cnnModel.requested,(unsigned long long)s->cnnModel.allocated,
        (unsigned long long)s->cnnDisplay.requested,(unsigned long long)s->cnnDisplay.allocated,
        (unsigned long long)s->cnnConv0.requested,(unsigned long long)s->cnnConv0.allocated,
        (unsigned long long)s->cnnConv1.requested,(unsigned long long)s->cnnConv1.allocated,
        (unsigned long long)s->cnnMACsPerLR*SURFACE_LR_WIDTH*SURFACE_LR_HEIGHT);
    return qtrue;
}

static void surface_cnn_dispatch(VkCommandBuffer cmd) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    uint32_t extent[4]={SURFACE_FHD_WIDTH,SURFACE_FHD_HEIGHT,SURFACE_LR_WIDTH,SURFACE_LR_HEIGHT};
    surface_image_barrier(cmd,s->output.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    surface_cnn_buffer_barrier(cmd,&s->cnnDisplay,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    surface_cnn_buffer_barrier(cmd,&s->cnnConv0,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    surface_cnn_buffer_barrier(cmd,&s->cnnConv1,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    qvkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->cnnPipelineLayout,0,1,&s->cnnDescriptor,0,NULL);
    qvkCmdPushConstants(cmd,s->cnnPipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(extent),extent);
    qvkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->cnnDisplayPipeline);
    qvkCmdDispatch(cmd,(SURFACE_LR_WIDTH+7)/8,(SURFACE_LR_HEIGHT+7)/8,1);
    surface_cnn_buffer_barrier(cmd,&s->cnnDisplay,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    surface_fhd_timestamp(cmd,8);
    qvkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->cnnConv0Pipeline);
    qvkCmdDispatch(cmd,(SURFACE_LR_WIDTH+7)/8,(SURFACE_LR_HEIGHT+7)/8,1);
    surface_cnn_buffer_barrier(cmd,&s->cnnConv0,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    surface_fhd_timestamp(cmd,9);
    qvkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->cnnConv1Pipeline);
    qvkCmdDispatch(cmd,(SURFACE_LR_WIDTH+7)/8,(SURFACE_LR_HEIGHT+7)/8,1);
    surface_cnn_buffer_barrier(cmd,&s->cnnConv1,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    surface_fhd_timestamp(cmd,10);
    qvkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->cnnHeadPipeline);
    qvkCmdDispatch(cmd,(SURFACE_FHD_WIDTH+7)/8,(SURFACE_FHD_HEIGHT+7)/8,1);
    surface_fhd_timestamp(cmd,11);
}
