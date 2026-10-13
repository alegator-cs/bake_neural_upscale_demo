#include "surface_pixel_identity.h"
#include "surface_cnn_identity.h"
#include "surface_batch_identity.h"
#include "surface_experiment_identity.h"
#define SURFACE_QUERY_STRIDE 12u
/* Private surface resources. Include after the ordinary Vulkan helpers.
 * Canonical contract: task-6 small_regularization/runtime/surface_renderer.cpp.
 * Asset identity/shape validation and the game adapter remain separate.
 * Queue operations here run on the renderer thread, outside an active physical
 * render pass; resize/destroy/capture also require the frame to be submitted.
 */

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize requested, allocated;
} surfaceBuffer_t;

typedef struct {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    VkDeviceSize requested, allocated;
} surfaceImage_t;

/* Exact shared gpu_renderer push contract: real, unmirrored camera basis. */
typedef struct {
    float origin[4];
    float forwardTanX[4];
    float rightTanY[4];
    float upNear[4]; /* real up.xyz, near=0.128 game units (.001 Blender m) */
    uint32_t info[4]; /* native W,H,flags (bit0 diagnostics; bit1 canonical),triangle count */
    uint32_t reserved[4];
} surfacePush_t;
typedef char surface_push_must_be_96_bytes[(sizeof(surfacePush_t) == 96) ? 1 : -1];

typedef struct {
    const void *triangles, *materials, *appearance, *correction, *bvh;
    VkDeviceSize triangleBytes, materialBytes, appearanceBytes, correctionBytes, bvhBytes;
    uint32_t triangleCount;
} surfaceValidatedAssets_t;

typedef struct {
    uint32_t width, height, triangleCount;
    uint32_t lrWidth, lrHeight; /* Exact4x capture mode9 only; other modes remain FHD. */
    surfaceBuffer_t triangles, materials, appearance, correction, bvh, diagnostic, readback;
    surfaceImage_t id, barycentric, depth, output;
    VkDescriptorPool pool;
    VkDescriptorSetLayout setLayout;
    VkDescriptorSet descriptor;
    VkPipelineLayout pipelineLayout;
    VkDescriptorSetLayout firstHitSetLayout;
    VkDescriptorSet firstHitDescriptor;
    VkPipelineLayout firstHitPipelineLayout;
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    VkSampler nearestSampler;
    VkPipeline visibilityPipeline, resolveDirectionalPipeline, resolveSmall32Pipeline;
    VkPipeline firstHitPipeline;
    PFN_vkCmdCopyImageToBuffer copyImageToBuffer;
    qboolean ready, hasCorrection, hasFirstHit, outputWritten, visibilityWritten;
    /* Separate FHD control: the approved NLUP file is uploaded without repacking. */
    surfaceBuffer_t upscale;
    surfaceImage_t lrID, lrBarycentric, lrLinear, lrCenterGate, hrCenterGate, geometryLinear;
    VkDescriptorPool fhdPool;
    VkDescriptorSetLayout fhdSetLayout;
    VkDescriptorSet fhdDescriptor, lrResolveDescriptor, lrFirstHitDescriptor;
    VkPipelineLayout fhdPipelineLayout;
    VkPipeline upscalePipeline, lrResolvePipeline;
    VkPipeline upscaleFastPipeline, bilinearFastPipeline, firstHitGatedPipeline;
    surfaceBuffer_t pixelModel;
    VkDescriptorPool pixelPool;
    VkDescriptorSetLayout pixelSetLayout;
    VkDescriptorSet pixelDescriptor;
    VkPipelineLayout pixelPipelineLayout;
    VkPipeline pixelResidualPipeline, pixelBilinearPipeline;
    qboolean pixelReady, pixelModelReady, legacyModelReady;
    uint32_t pixelWidth, pixelParameters, pixelMACs;
    surfaceBuffer_t cnnModel,cnnDisplay,cnnConv0,cnnConv1;
    VkDescriptorPool cnnPool;
    VkDescriptorSetLayout cnnSetLayout;
    VkDescriptorSet cnnDescriptor;
    VkPipelineLayout cnnPipelineLayout;
    VkPipeline cnnDisplayPipeline,cnnConv0Pipeline,cnnConv1Pipeline,cnnHeadPipeline;
    qboolean cnnReady;
    uint32_t cnnWidth,cnnParameters,cnnMACsPerLR;
    uint32_t fhdVendorID,fhdDeviceID,fhdDriverVersion,fhdAPIVersion,fhdMaxImageDimension2D;
    VkQueryPool fhdQueries;
    PFN_vkCreateQueryPool createQueryPool;
    PFN_vkDestroyQueryPool destroyQueryPool;
    PFN_vkCmdResetQueryPool resetQueryPool;
    PFN_vkCmdWriteTimestamp writeTimestamp;
    PFN_vkGetQueryPoolResults getQueryResults;
    float timestampPeriod;
    uint32_t timestampValidBits;
    uint64_t timestampValues[SURFACE_QUERY_STRIDE];
    qboolean querySlotSubmitted[NUM_COMMAND_BUFFERS];
    uint32_t querySlotMode[NUM_COMMAND_BUFFERS];
    uint32_t querySlotPixelWidth[NUM_COMMAND_BUFFERS],querySlotPixelParameters[NUM_COMMAND_BUFFERS];
    uint32_t querySlotPixelBytes[NUM_COMMAND_BUFFERS],querySlotPixelMACs[NUM_COMMAND_BUFFERS],querySlotPixelDiagnostic[NUM_COMMAND_BUFFERS];
    char querySlotCnnSHA[NUM_COMMAND_BUFFERS][65];
    int querySlotPose[NUM_COMMAND_BUFFERS];
    surfacePush_t querySlotPush[NUM_COMMAND_BUFFERS];
    uint32_t querySlotBenchmarkEpoch[NUM_COMMAND_BUFFERS];
    qboolean querySlotCaptured[NUM_COMMAND_BUFFERS], querySlotCPUReady[NUM_COMMAND_BUFFERS];
    double querySlotCPUWall[NUM_COMMAND_BUFFERS], querySlotCPUQueueIdle[NUM_COMMAND_BUFFERS];
    uint64_t querySlotSerial[NUM_COMMAND_BUFFERS], submittedSerial;
    uint32_t timingMode;
    uint64_t timingSerial;
    qboolean fhdReady, lrVisibilityWritten, lrWritten, hrDiagnosticWritten, timingReady;
    qboolean fhdLRCurrent, fhdHRGuidesCurrent;
    uint32_t fhdLastMode;
} surfaceRenderer_t;

static surfaceRenderer_t vk_surface_renderer;
static surfaceImage_t vk_surface_fallback;

/* May be prototyped before the engine's compositor descriptor setup. */
static VkImageView vk_surface_output_view(void) {
    return vk_surface_renderer.ready ? vk_surface_renderer.output.view : vk_surface_fallback.view;
}

static void surface_destroy_buffer(surfaceBuffer_t *b) {
    if (b->buffer) qvkDestroyBuffer(vk.device, b->buffer, NULL);
    if (b->memory) qvkFreeMemory(vk.device, b->memory, NULL);
    Com_Memset(b, 0, sizeof(*b));
}

static void surface_destroy_image(surfaceImage_t *i) {
    if (i->view) qvkDestroyImageView(vk.device, i->view, NULL);
    if (i->image) qvkDestroyImage(vk.device, i->image, NULL);
    if (i->memory) qvkFreeMemory(vk.device, i->memory, NULL);
    Com_Memset(i, 0, sizeof(*i));
}

/* Call before the Vulkan device/procedure table is destroyed, after submission.
 * The wait is required even when the engine has several frames in flight. */
static void surface_destroy_renderer(void) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    if (vk.device && (s->ready || s->triangles.buffer)) VK_CHECK(qvkQueueWaitIdle(vk.queue));
    if (s->fhdQueries && s->destroyQueryPool) s->destroyQueryPool(vk.device, s->fhdQueries, NULL);
    if(s->cnnDisplayPipeline)qvkDestroyPipeline(vk.device,s->cnnDisplayPipeline,NULL);
    if(s->cnnConv0Pipeline)qvkDestroyPipeline(vk.device,s->cnnConv0Pipeline,NULL);
    if(s->cnnConv1Pipeline)qvkDestroyPipeline(vk.device,s->cnnConv1Pipeline,NULL);
    if(s->cnnHeadPipeline)qvkDestroyPipeline(vk.device,s->cnnHeadPipeline,NULL);
    if(s->cnnPool)qvkDestroyDescriptorPool(vk.device,s->cnnPool,NULL);
    if(s->cnnPipelineLayout)qvkDestroyPipelineLayout(vk.device,s->cnnPipelineLayout,NULL);
    if(s->cnnSetLayout)qvkDestroyDescriptorSetLayout(vk.device,s->cnnSetLayout,NULL);
    surface_destroy_buffer(&s->cnnModel);surface_destroy_buffer(&s->cnnDisplay);
    surface_destroy_buffer(&s->cnnConv0);surface_destroy_buffer(&s->cnnConv1);
    if(s->pixelResidualPipeline)qvkDestroyPipeline(vk.device,s->pixelResidualPipeline,NULL);
    if(s->pixelBilinearPipeline)qvkDestroyPipeline(vk.device,s->pixelBilinearPipeline,NULL);
    if(s->pixelPool)qvkDestroyDescriptorPool(vk.device,s->pixelPool,NULL);
    if(s->pixelPipelineLayout)qvkDestroyPipelineLayout(vk.device,s->pixelPipelineLayout,NULL);
    if(s->pixelSetLayout)qvkDestroyDescriptorSetLayout(vk.device,s->pixelSetLayout,NULL);
    surface_destroy_buffer(&s->pixelModel);
    if (s->upscalePipeline) qvkDestroyPipeline(vk.device, s->upscalePipeline, NULL);
    if (s->upscaleFastPipeline) qvkDestroyPipeline(vk.device, s->upscaleFastPipeline, NULL);
    if (s->bilinearFastPipeline) qvkDestroyPipeline(vk.device, s->bilinearFastPipeline, NULL);
    if (s->firstHitGatedPipeline) qvkDestroyPipeline(vk.device, s->firstHitGatedPipeline, NULL);
    if (s->lrResolvePipeline) qvkDestroyPipeline(vk.device, s->lrResolvePipeline, NULL);
    if (s->fhdPool) qvkDestroyDescriptorPool(vk.device, s->fhdPool, NULL);
    if (s->fhdPipelineLayout) qvkDestroyPipelineLayout(vk.device, s->fhdPipelineLayout, NULL);
    if (s->fhdSetLayout) qvkDestroyDescriptorSetLayout(vk.device, s->fhdSetLayout, NULL);
    surface_destroy_image(&s->lrID); surface_destroy_image(&s->lrBarycentric);
    surface_destroy_image(&s->lrLinear); surface_destroy_image(&s->lrCenterGate);
    surface_destroy_image(&s->hrCenterGate); surface_destroy_image(&s->geometryLinear);
    surface_destroy_buffer(&s->upscale);
    if (s->visibilityPipeline) qvkDestroyPipeline(vk.device, s->visibilityPipeline, NULL);
    if (s->firstHitPipeline) qvkDestroyPipeline(vk.device, s->firstHitPipeline, NULL);
    if (s->resolveDirectionalPipeline) qvkDestroyPipeline(vk.device, s->resolveDirectionalPipeline, NULL);
    if (s->resolveSmall32Pipeline) qvkDestroyPipeline(vk.device, s->resolveSmall32Pipeline, NULL);
    if (s->framebuffer) qvkDestroyFramebuffer(vk.device, s->framebuffer, NULL);
    if (s->renderPass) qvkDestroyRenderPass(vk.device, s->renderPass, NULL);
    if (s->pool) qvkDestroyDescriptorPool(vk.device, s->pool, NULL);
    if (s->pipelineLayout) qvkDestroyPipelineLayout(vk.device, s->pipelineLayout, NULL);
    if (s->firstHitPipelineLayout) qvkDestroyPipelineLayout(vk.device, s->firstHitPipelineLayout, NULL);
    if (s->setLayout) qvkDestroyDescriptorSetLayout(vk.device, s->setLayout, NULL);
    if (s->firstHitSetLayout) qvkDestroyDescriptorSetLayout(vk.device, s->firstHitSetLayout, NULL);
    if (s->nearestSampler) qvkDestroySampler(vk.device, s->nearestSampler, NULL);
    surface_destroy_image(&s->id);
    surface_destroy_image(&s->barycentric);
    surface_destroy_image(&s->depth);
    surface_destroy_image(&s->output);
    surface_destroy_buffer(&s->triangles);
    surface_destroy_buffer(&s->materials);
    surface_destroy_buffer(&s->appearance);
    surface_destroy_buffer(&s->correction);
    surface_destroy_buffer(&s->bvh);
    surface_destroy_buffer(&s->diagnostic);
    surface_destroy_buffer(&s->readback);
    Com_Memset(s, 0, sizeof(*s));
}

static void vk_surface_destroy(void) {
    surface_destroy_renderer();
    if (vk_surface_fallback.image) {
        VK_CHECK(qvkQueueWaitIdle(vk.queue));
        surface_destroy_image(&vk_surface_fallback);
    }
}

/* Surface commands use the graphics queue directly and do not consume/replace
 * the engine upload or presentation semaphores. The queue has a single host
 * owner. Setup/capture waits are deliberately excluded from frame timings. */
static void surface_submit_and_wait(VkCommandBuffer cmd) {
    VkSubmitInfo submit;
    VK_CHECK(qvkEndCommandBuffer(cmd));
    Com_Memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VK_CHECK(qvkQueueSubmit(vk.queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(qvkQueueWaitIdle(vk.queue));
    qvkFreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd);
}

static void surface_allocate_buffer(surfaceBuffer_t *b, VkDeviceSize bytes,
        VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    VkBufferCreateInfo ci;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    Com_Memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = bytes;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(qvkCreateBuffer(vk.device, &ci, NULL, &b->buffer));
    qvkGetBufferMemoryRequirements(vk.device, b->buffer, &req);
    Com_Memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, properties);
    VK_CHECK(qvkAllocateMemory(vk.device, &ai, NULL, &b->memory));
    VK_CHECK(qvkBindBufferMemory(vk.device, b->buffer, b->memory, 0));
    b->requested = bytes;
    b->allocated = req.size;
}

/* One-time upload. Setup wait and temporary staging bytes are not frame costs. */
static void surface_upload_buffer(surfaceBuffer_t *b, const void *data, VkDeviceSize bytes) {
    surfaceBuffer_t staging;
    VkCommandBuffer cmd;
    VkBufferCopy copy;
    VkBufferMemoryBarrier barrier;
    void *mapped;
    Com_Memset(&staging, 0, sizeof(staging));
    surface_allocate_buffer(b, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    surface_allocate_buffer(&staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(qvkMapMemory(vk.device, staging.memory, 0, bytes, 0, &mapped));
    Com_Memcpy(mapped, data, (size_t)bytes);
    qvkUnmapMemory(vk.device, staging.memory);
    cmd = begin_command_buffer();
    Com_Memset(&copy, 0, sizeof(copy));
    copy.size = bytes;
    qvkCmdCopyBuffer(cmd, staging.buffer, b->buffer, 1, &copy);
    Com_Memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = b->buffer;
    barrier.size = VK_WHOLE_SIZE;
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 1, &barrier, 0, NULL);
    surface_submit_and_wait(cmd);
    ri.Printf(PRINT_ALL, "surface upload: static=%llu B allocated=%llu B temporary staging=%llu B\n",
        (unsigned long long)bytes, (unsigned long long)b->allocated, (unsigned long long)staging.allocated);
    surface_destroy_buffer(&staging);
}

static qboolean surface_format_supported(VkFormat format, VkFormatFeatureFlags needed) {
    VkFormatProperties props;
    qvkGetPhysicalDeviceFormatProperties(vk.physical_device, format, &props);
    return (props.optimalTilingFeatures & needed) == needed;
}

static void surface_allocate_image(surfaceImage_t *i, uint32_t w, uint32_t h,
        VkFormat format, uint32_t bytesPerPixel, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    VkImageCreateInfo ci;
    VkImageViewCreateInfo vi;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    Com_Memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent.width = w; ci.extent.height = h; ci.extent.depth = 1;
    ci.mipLevels = 1; ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(qvkCreateImage(vk.device, &ci, NULL, &i->image));
    qvkGetImageMemoryRequirements(vk.device, i->image, &req);
    Com_Memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(qvkAllocateMemory(vk.device, &ai, NULL, &i->memory));
    VK_CHECK(qvkBindImageMemory(vk.device, i->image, i->memory, 0));
    Com_Memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = i->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange.aspectMask = aspect;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    VK_CHECK(qvkCreateImageView(vk.device, &vi, NULL, &i->view));
    i->requested = (VkDeviceSize)w * h * bytesPerPixel;
    i->allocated = req.size;
}

static void surface_image_barrier(VkCommandBuffer cmd, VkImage image,
        VkImageLayout oldLayout, VkImageLayout newLayout,
        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
        VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier;
    Com_Memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess; barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout; barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
    qvkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

/* The compositor always declares rgba32f storage binding 6, including disabled
 * and rejected-asset modes. Never substitute an incompatible ordinary HDR view. */
static qboolean vk_surface_ensure_fallback(void) {
    PFN_vkCmdClearColorImage clearColorImage;
    VkCommandBuffer cmd;
    VkClearColorValue zero;
    VkImageSubresourceRange range;
    if (vk_surface_fallback.view) return qtrue;
    if (!vk.device || !surface_format_supported(VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
        ri.Error(ERR_FATAL,"Surface compositor rgba32f fallback unsupported or Vulkan device unavailable");
        return qfalse;
    }
    clearColorImage = (PFN_vkCmdClearColorImage)qvkGetDeviceProcAddr(vk.device, "vkCmdClearColorImage");
    if (!clearColorImage) {
        ri.Error(ERR_FATAL,"Surface compositor rgba32f fallback requires vkCmdClearColorImage");
        return qfalse;
    }
    surface_allocate_image(&vk_surface_fallback, 1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    cmd = begin_command_buffer();
    surface_image_barrier(cmd, vk_surface_fallback.image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    Com_Memset(&zero, 0, sizeof(zero));
    Com_Memset(&range, 0, sizeof(range));
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; range.levelCount = range.layerCount = 1;
    clearColorImage(cmd, vk_surface_fallback.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    surface_image_barrier(cmd, vk_surface_fallback.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    surface_submit_and_wait(cmd);
    ri.Printf(PRINT_ALL, "surface fallback rgba32f=16/%llu bytes (requested/allocated), initialized transparent\n",
        (unsigned long long)vk_surface_fallback.allocated);
    return qtrue;
}

static void surface_create_descriptors(void) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    VkDescriptorSetLayoutBinding binding[8];
    VkDescriptorSetLayoutCreateInfo layout;
    VkDescriptorPoolSize sizes[3];
    VkDescriptorPoolCreateInfo pool;
    VkDescriptorSetAllocateInfo allocate;
    VkPipelineLayoutCreateInfo pipeline;
    VkPushConstantRange push;
    VkSamplerCreateInfo sampler;
    VkDescriptorBufferInfo buffer[5];
    VkDescriptorImageInfo image[3];
    VkWriteDescriptorSet write[8];
    int i;
    Com_Memset(binding, 0, sizeof(binding));
    for (i = 0; i < 8; ++i) {
        binding[i].binding = i;
        binding[i].descriptorCount = 1;
        binding[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        binding[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    binding[0].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
    binding[3].descriptorType = binding[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    Com_Memset(&layout, 0, sizeof(layout));
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 8; layout.pBindings = binding;
    VK_CHECK(qvkCreateDescriptorSetLayout(vk.device, &layout, NULL, &s->setLayout));
    Com_Memset(&push, 0, sizeof(push));
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    push.size = sizeof(surfacePush_t);
    Com_Memset(&pipeline, 0, sizeof(pipeline));
    pipeline.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline.setLayoutCount = 1; pipeline.pSetLayouts = &s->setLayout;
    pipeline.pushConstantRangeCount = 1; pipeline.pPushConstantRanges = &push;
    VK_CHECK(qvkCreatePipelineLayout(vk.device, &pipeline, NULL, &s->pipelineLayout));
    sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; sizes[0].descriptorCount = s->hasFirstHit ? 7 : 5;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; sizes[1].descriptorCount = 2;
    sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; sizes[2].descriptorCount = s->hasFirstHit ? 3 : 1;
    Com_Memset(&pool, 0, sizeof(pool));
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = s->hasFirstHit ? 2 : 1; pool.poolSizeCount = 3; pool.pPoolSizes = sizes;
    VK_CHECK(qvkCreateDescriptorPool(vk.device, &pool, NULL, &s->pool));
    Com_Memset(&allocate, 0, sizeof(allocate));
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = s->pool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &s->setLayout;
    VK_CHECK(qvkAllocateDescriptorSets(vk.device, &allocate, &s->descriptor));
    Com_Memset(&sampler, 0, sizeof(sampler));
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0.0f;
    VK_CHECK(qvkCreateSampler(vk.device, &sampler, NULL, &s->nearestSampler));
    Com_Memset(buffer, 0, sizeof(buffer));
    buffer[0].buffer = s->triangles.buffer; buffer[1].buffer = s->materials.buffer;
    buffer[2].buffer = s->appearance.buffer; buffer[3].buffer = s->diagnostic.buffer;
    buffer[4].buffer = s->correction.buffer;
    for (i = 0; i < 5; ++i) buffer[i].range = VK_WHOLE_SIZE;
    Com_Memset(image, 0, sizeof(image));
    image[0].imageView = s->id.view; image[1].imageView = s->barycentric.view;
    for (i = 0; i < 2; ++i) {
        image[i].sampler = s->nearestSampler;
        image[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    image[2].imageView = s->output.view; image[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    Com_Memset(write, 0, sizeof(write));
    for (i = 0; i < 8; ++i) {
        write[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write[i].dstSet = s->descriptor; write[i].dstBinding = i;
        write[i].descriptorCount = 1; write[i].descriptorType = binding[i].descriptorType;
        if (i < 3) write[i].pBufferInfo = &buffer[i];
        else if (i >= 6) write[i].pBufferInfo = &buffer[i - 3];
        else write[i].pImageInfo = &image[i - 3];
    }
    qvkUpdateDescriptorSets(vk.device, 8, write, 0, NULL);
}

/* The first-hit compute pass writes the same ID/bary images consumed by the
 * unmodified resolve contract, but uses storage descriptors in a separate set. */
static void surface_create_firsthit_descriptors(void) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    VkDescriptorSetLayoutBinding binding[4];
    VkDescriptorSetLayoutCreateInfo layout;
    VkDescriptorSetAllocateInfo allocate;
    VkPushConstantRange push;
    VkPipelineLayoutCreateInfo pipeline;
    VkDescriptorBufferInfo buffer[2];
    VkDescriptorImageInfo image[2];
    VkWriteDescriptorSet write[4];
    uint32_t numbers[4] = {0, 3, 4, 8};
    int i;
    if (!s->hasFirstHit) return;
    Com_Memset(binding, 0, sizeof(binding));
    for (i = 0; i < 4; ++i) {
        binding[i].binding = numbers[i];
        binding[i].descriptorCount = 1;
        binding[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        binding[i].descriptorType = (i == 0 || i == 3) ?
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    Com_Memset(&layout, 0, sizeof(layout));
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 4; layout.pBindings = binding;
    VK_CHECK(qvkCreateDescriptorSetLayout(vk.device, &layout, NULL, &s->firstHitSetLayout));
    Com_Memset(&push, 0, sizeof(push));
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; push.size = sizeof(surfacePush_t);
    Com_Memset(&pipeline, 0, sizeof(pipeline));
    pipeline.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline.setLayoutCount = 1; pipeline.pSetLayouts = &s->firstHitSetLayout;
    pipeline.pushConstantRangeCount = 1; pipeline.pPushConstantRanges = &push;
    VK_CHECK(qvkCreatePipelineLayout(vk.device, &pipeline, NULL, &s->firstHitPipelineLayout));
    Com_Memset(&allocate, 0, sizeof(allocate));
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = s->pool; allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &s->firstHitSetLayout;
    VK_CHECK(qvkAllocateDescriptorSets(vk.device, &allocate, &s->firstHitDescriptor));
    Com_Memset(buffer, 0, sizeof(buffer));
    buffer[0].buffer = s->triangles.buffer; buffer[1].buffer = s->bvh.buffer;
    buffer[0].range = buffer[1].range = VK_WHOLE_SIZE;
    Com_Memset(image, 0, sizeof(image));
    image[0].imageView = s->id.view; image[1].imageView = s->barycentric.view;
    image[0].imageLayout = image[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    Com_Memset(write, 0, sizeof(write));
    for (i = 0; i < 4; ++i) {
        write[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write[i].dstSet = s->firstHitDescriptor; write[i].dstBinding = numbers[i];
        write[i].descriptorCount = 1; write[i].descriptorType = binding[i].descriptorType;
        if (i == 0) write[i].pBufferInfo = &buffer[0];
        else if (i == 3) write[i].pBufferInfo = &buffer[1];
        else write[i].pImageInfo = &image[i - 1];
    }
    qvkUpdateDescriptorSets(vk.device, 4, write, 0, NULL);
}

static void surface_create_render_pass(void) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    VkAttachmentDescription attachment[3];
    VkAttachmentReference colors[2], depth;
    VkSubpassDescription subpass;
    VkSubpassDependency dependency[2];
    VkRenderPassCreateInfo pass;
    VkFramebufferCreateInfo frame;
    VkImageView views[3];
    int i;
    Com_Memset(attachment, 0, sizeof(attachment));
    attachment[0].format = VK_FORMAT_R32_UINT;
    attachment[1].format = VK_FORMAT_R32G32_SFLOAT;
    attachment[2].format = VK_FORMAT_D32_SFLOAT;
    for (i = 0; i < 3; ++i) {
        attachment[i].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment[i].finalLayout = i < 2 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    for (i = 0; i < 2; ++i) { colors[i].attachment = i; colors[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; }
    depth.attachment = 2; depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    Com_Memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 2; subpass.pColorAttachments = colors; subpass.pDepthStencilAttachment = &depth;
    Com_Memset(dependency, 0, sizeof(dependency));
    dependency[0].srcSubpass = VK_SUBPASS_EXTERNAL; dependency[0].dstSubpass = 0;
    dependency[0].srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency[1].srcSubpass = 0; dependency[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency[1].dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dependency[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    Com_Memset(&pass, 0, sizeof(pass));
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = 3; pass.pAttachments = attachment;
    pass.subpassCount = 1; pass.pSubpasses = &subpass;
    pass.dependencyCount = 2; pass.pDependencies = dependency;
    VK_CHECK(qvkCreateRenderPass(vk.device, &pass, NULL, &s->renderPass));
    views[0] = s->id.view; views[1] = s->barycentric.view; views[2] = s->depth.view;
    Com_Memset(&frame, 0, sizeof(frame));
    frame.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frame.renderPass = s->renderPass; frame.attachmentCount = 3; frame.pAttachments = views;
    frame.width = 2 * s->width; frame.height = 2 * s->height; frame.layers = 1;
    VK_CHECK(qvkCreateFramebuffer(vk.device, &frame, NULL, &s->framebuffer));
}

static void surface_create_pipelines(VkShaderModule vertex, VkShaderModule fragment,
        VkShaderModule resolveDirectional, VkShaderModule resolveSmall32, VkShaderModule firstHit) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    VkPipelineShaderStageCreateInfo stages[2];
    VkPipelineVertexInputStateCreateInfo input;
    VkPipelineInputAssemblyStateCreateInfo assembly;
    VkPipelineViewportStateCreateInfo viewport;
    VkPipelineRasterizationStateCreateInfo raster;
    VkPipelineMultisampleStateCreateInfo multisample;
    VkPipelineDepthStencilStateCreateInfo depth;
    VkPipelineColorBlendAttachmentState blends[2];
    VkPipelineColorBlendStateCreateInfo blend;
    VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic;
    VkGraphicsPipelineCreateInfo graphics;
    VkComputePipelineCreateInfo compute;
    Com_Memset(stages, 0, sizeof(stages));
    set_shader_stage_desc(&stages[0], VK_SHADER_STAGE_VERTEX_BIT, vertex, "main");
    set_shader_stage_desc(&stages[1], VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main");
    Com_Memset(&input, 0, sizeof(input)); input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    Com_Memset(&assembly, 0, sizeof(assembly)); assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    Com_Memset(&viewport, 0, sizeof(viewport)); viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    Com_Memset(&raster, 0, sizeof(raster)); raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1.0f;
    Com_Memset(&multisample, 0, sizeof(multisample)); multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    Com_Memset(&depth, 0, sizeof(depth)); depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE; depth.depthCompareOp = VK_COMPARE_OP_GREATER;
    Com_Memset(blends, 0, sizeof(blends));
    blends[0].colorWriteMask = blends[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    Com_Memset(&blend, 0, sizeof(blend)); blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 2; blend.pAttachments = blends;
    Com_Memset(&dynamic, 0, sizeof(dynamic)); dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamics;
    Com_Memset(&graphics, 0, sizeof(graphics)); graphics.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    graphics.stageCount = 2; graphics.pStages = stages;
    graphics.pVertexInputState = &input; graphics.pInputAssemblyState = &assembly;
    graphics.pViewportState = &viewport; graphics.pRasterizationState = &raster;
    graphics.pMultisampleState = &multisample; graphics.pDepthStencilState = &depth;
    graphics.pColorBlendState = &blend; graphics.pDynamicState = &dynamic;
    graphics.layout = s->pipelineLayout; graphics.renderPass = s->renderPass;
    VK_CHECK(qvkCreateGraphicsPipelines(vk.device, vk.pipelineCache, 1, &graphics, NULL, &s->visibilityPipeline));
    Com_Memset(&compute, 0, sizeof(compute)); compute.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute.layout = s->pipelineLayout;
    set_shader_stage_desc(&compute.stage, VK_SHADER_STAGE_COMPUTE_BIT, resolveDirectional, "main");
    VK_CHECK(qvkCreateComputePipelines(vk.device, vk.pipelineCache, 1, &compute, NULL, &s->resolveDirectionalPipeline));
    if (resolveSmall32 && s->hasCorrection) {
        set_shader_stage_desc(&compute.stage, VK_SHADER_STAGE_COMPUTE_BIT, resolveSmall32, "main");
        VK_CHECK(qvkCreateComputePipelines(vk.device, vk.pipelineCache, 1, &compute, NULL, &s->resolveSmall32Pipeline));
    }
    if (firstHit && s->hasFirstHit) {
        compute.layout = s->firstHitPipelineLayout;
        set_shader_stage_desc(&compute.stage, VK_SHADER_STAGE_COMPUTE_BIT, firstHit, "main");
        VK_CHECK(qvkCreateComputePipelines(vk.device, vk.pipelineCache, 1, &compute, NULL, &s->firstHitPipeline));
    }
}

static void surface_log_memory(void) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    VkDeviceSize staticRequested = s->triangles.requested + s->materials.requested + s->appearance.requested + s->correction.requested + s->bvh.requested + s->diagnostic.requested;
    VkDeviceSize staticAllocated = s->triangles.allocated + s->materials.allocated + s->appearance.allocated + s->correction.allocated + s->bvh.allocated + s->diagnostic.allocated;
    VkDeviceSize targetRequested = s->id.requested + s->barycentric.requested + s->depth.requested + s->output.requested;
    VkDeviceSize targetAllocated = s->id.allocated + s->barycentric.allocated + s->depth.allocated + s->output.allocated;
    ri.Printf(PRINT_ALL, "surface memory native=%ux%u static requested=%llu allocated=%llu targets requested=%llu allocated=%llu bytes; excludes driver pipeline/descriptor allocations, ordinary engine buffers, host RAM and setup staging\n",
        s->width, s->height, (unsigned long long)staticRequested, (unsigned long long)staticAllocated,
        (unsigned long long)targetRequested, (unsigned long long)targetAllocated);
    ri.Printf(PRINT_ALL, "surface target bytes ID=%llu/%llu bary=%llu/%llu depth=%llu/%llu resolve=%llu/%llu (requested/allocated); final game output remains separately allocated\n",
        (unsigned long long)s->id.requested, (unsigned long long)s->id.allocated,
        (unsigned long long)s->barycentric.requested, (unsigned long long)s->barycentric.allocated,
        (unsigned long long)s->depth.requested, (unsigned long long)s->depth.allocated,
        (unsigned long long)s->output.requested, (unsigned long long)s->output.allocated);
    ri.Printf(PRINT_ALL, "surface appearance bytes frozen directional=%llu/%llu small32 correction=%llu/%llu; QA raw readback=%llu/%llu (requested/allocated); GPU attachment targets and QA bytes are not appearance parameters\n",
        (unsigned long long)s->appearance.requested, (unsigned long long)s->appearance.allocated,
        (unsigned long long)s->correction.requested, (unsigned long long)s->correction.allocated,
        (unsigned long long)s->readback.requested, (unsigned long long)s->readback.allocated);
    ri.Printf(PRINT_ALL, "surface optional first-hit BVH=%llu/%llu bytes (requested/allocated) enabled=%d; geometry acceleration bytes are not appearance parameters\n",
        (unsigned long long)s->bvh.requested, (unsigned long long)s->bvh.allocated, s->hasFirstHit);
}

static qboolean surface_capture4x_extent(uint32_t width, uint32_t height) {
    return r_surfaceUpscale && r_surfaceUpscale->integer==9 &&
        ((width==1920u && height==1080u) || (width==960u && height==540u) || (width==512u && height==288u));
}

static qboolean vk_surface_create(uint32_t width, uint32_t height,
        const surfaceValidatedAssets_t *assets, VkShaderModule vertex, VkShaderModule fragment,
        VkShaderModule resolveDirectional, VkShaderModule resolveSmall32, VkShaderModule firstHit) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    VkCommandBuffer cmd;
    uint32_t diagnosticZero = 0;
    qboolean firstHitSupported;
    VkImageUsageFlags visibilityUsage;
    VkFormatFeatureFlags colorSampled = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (!width || !height || ((VkDeviceSize)width * height > 131072 &&
            !(r_surfaceUpscale && ((r_surfaceUpscale->integer == 4 && ((width == 512 && height == 288) || (width == 960 && height == 540))) ||
             (r_surfaceUpscale->integer >= 1 && r_surfaceUpscale->integer <= 11 && width == 1920 && height == 1080))) &&
            !surface_capture4x_extent(width,height)) || !assets ||
            !assets->triangles || !assets->materials || !assets->appearance ||
            !assets->triangleCount || assets->triangleCount > 100000 ||
            assets->triangleBytes != (VkDeviceSize)assets->triangleCount * 112 ||
            !assets->materialBytes || assets->materialBytes > 1024*1024 || assets->materialBytes % 16 ||
            assets->appearanceBytes < 256 || assets->appearanceBytes > SURFACE_EXPERIMENT_BASE_CAP || assets->appearanceBytes % 4 ||
            ((assets->correction != NULL) != (assets->correctionBytes != 0)) ||
            (assets->correctionBytes && (assets->correctionBytes < 256 || assets->correctionBytes > SURFACE_EXPERIMENT_CORRECTION_CAP || assets->correctionBytes % 4)) ||
            ((assets->bvh != NULL) != (assets->bvhBytes != 0)) ||
            (assets->bvhBytes && (assets->bvhBytes < 64 || assets->bvhBytes > 16*1024*1024 || assets->bvhBytes % 4)) ||
            !vertex || !fragment || !resolveDirectional) return qfalse;
    if (!surface_format_supported(VK_FORMAT_R32_UINT, colorSampled) ||
            !surface_format_supported(VK_FORMAT_R32G32_SFLOAT, colorSampled) ||
            !surface_format_supported(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
            !surface_format_supported(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
        ri.Printf(PRINT_WARNING, "Surface renderer: required private attachment formats unsupported; diagnostic checker required.\n");
        return qfalse;
    }
    firstHitSupported = firstHit && assets->bvh && vk.storageImageExtendedFormats &&
        surface_format_supported(VK_FORMAT_R32_UINT, colorSampled | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) &&
        surface_format_supported(VK_FORMAT_R32G32_SFLOAT, colorSampled | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
    surface_destroy_renderer();
    s->width = width; s->height = height; s->triangleCount = assets->triangleCount;
    s->hasCorrection = assets->correction != NULL;
    s->hasFirstHit = firstHitSupported;
    s->copyImageToBuffer = (PFN_vkCmdCopyImageToBuffer)qvkGetDeviceProcAddr(vk.device, "vkCmdCopyImageToBuffer");
    surface_upload_buffer(&s->triangles, assets->triangles, assets->triangleBytes);
    surface_upload_buffer(&s->materials, assets->materials, assets->materialBytes);
    surface_upload_buffer(&s->appearance, assets->appearance, assets->appearanceBytes);
    surface_upload_buffer(&s->correction, s->hasCorrection ? assets->correction : &diagnosticZero,
        s->hasCorrection ? assets->correctionBytes : sizeof(diagnosticZero));
    if (s->hasFirstHit) surface_upload_buffer(&s->bvh, assets->bvh, assets->bvhBytes);
    surface_upload_buffer(&s->diagnostic, &diagnosticZero, sizeof(diagnosticZero));
    /* RGBA32F first (16 B/native pixel), then quadrant IDs (16), then quadrant
     * barycentrics (32). Only explicit QA captures copy/map these bytes. */
    surface_allocate_buffer(&s->readback, (VkDeviceSize)width * height * 64,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    visibilityUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (s->hasFirstHit) visibilityUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    surface_allocate_image(&s->id, 2*width, 2*height, VK_FORMAT_R32_UINT, 4,
        visibilityUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    surface_allocate_image(&s->barycentric, 2*width, 2*height, VK_FORMAT_R32G32_SFLOAT, 8,
        visibilityUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    surface_allocate_image(&s->depth, 2*width, 2*height, VK_FORMAT_D32_SFLOAT, 4,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    surface_allocate_image(&s->output, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, 16,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    surface_create_descriptors();
    surface_create_firsthit_descriptors();
    surface_create_render_pass();
    surface_create_pipelines(vertex, fragment, resolveDirectional, resolveSmall32, firstHit);
    cmd = begin_command_buffer();
    surface_image_barrier(cmd, s->output.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
    surface_submit_and_wait(cmd);
    s->ready = qtrue;
    surface_log_memory();
    return qtrue;
}

/* Called only outside the physical render pass, after safe camera preparation.
 * Corrected selects small32; firstHit selects the separately certified contract
 * attempt. A missing requested pipeline returns false; the caller must disable
 * the compositor surface mode for that frame instead of sampling old output. */
static qboolean vk_surface_draw(const surfacePush_t *push, qboolean corrected, qboolean firstHit) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    VkCommandBuffer cmd = vk.cmd->command_buffer;
    VkClearValue clear[3];
    VkRenderPassBeginInfo begin;
    VkViewport viewport;
    VkRect2D scissor;
    VkImageLayout visibilityLayout;
    VkPipelineStageFlags visibilityStage;
    VkAccessFlags visibilityAccess;
    int i;
    s->outputWritten = qfalse;
    if (!s->ready || !push || push->upNear[3] != 0.128f ||
            push->info[0] != s->width || push->info[1] != s->height ||
            push->info[2] != (firstHit ? 2u : 0u) || push->info[3] != s->triangleCount ||
            (corrected && !s->resolveSmall32Pipeline) || (firstHit && !s->firstHitPipeline)) return qfalse;
    if (firstHit) {
        visibilityLayout = s->visibilityWritten ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        visibilityStage = s->visibilityWritten ?
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        visibilityAccess = s->visibilityWritten ?
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0;
        surface_image_barrier(cmd, s->id.image, visibilityLayout, VK_IMAGE_LAYOUT_GENERAL,
            visibilityStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, visibilityAccess, VK_ACCESS_SHADER_WRITE_BIT);
        surface_image_barrier(cmd, s->barycentric.image, visibilityLayout, VK_IMAGE_LAYOUT_GENERAL,
            visibilityStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, visibilityAccess, VK_ACCESS_SHADER_WRITE_BIT);
        qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s->firstHitPipeline);
        qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s->firstHitPipelineLayout, 0, 1, &s->firstHitDescriptor, 0, NULL);
        qvkCmdPushConstants(cmd, s->firstHitPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(*push), push);
        qvkCmdDispatch(cmd, (2*s->width + 7)/8, (2*s->height + 7)/8, 1);
        surface_image_barrier(cmd, s->id.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        surface_image_barrier(cmd, s->barycentric.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    } else {
    Com_Memset(clear, 0, sizeof(clear));
    clear[0].color.uint32[0] = 0xffffffffU;
    clear[2].depthStencil.depth = 0.0f;
    Com_Memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = s->renderPass; begin.framebuffer = s->framebuffer;
    begin.renderArea.extent.width = 2*s->width; begin.renderArea.extent.height = 2*s->height;
    begin.clearValueCount = 3; begin.pClearValues = clear;
    qvkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    Com_Memset(&viewport, 0, sizeof(viewport));
    viewport.width = (float)(2*s->width); viewport.height = (float)(2*s->height); viewport.maxDepth = 1.0f;
    Com_Memset(&scissor, 0, sizeof(scissor)); scissor.extent = begin.renderArea.extent;
    qvkCmdSetViewport(cmd, 0, 1, &viewport); qvkCmdSetScissor(cmd, 0, 1, &scissor);
    qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->visibilityPipeline);
    qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipelineLayout, 0, 1, &s->descriptor, 0, NULL);
    qvkCmdPushConstants(cmd, s->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(*push), push);
    qvkCmdDraw(cmd, s->triangleCount * 3, 1, 0, 0);
    qvkCmdEndRenderPass(cmd);
    }
    s->visibilityWritten = qtrue;
    surface_image_barrier(cmd, s->output.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        corrected ? s->resolveSmall32Pipeline : s->resolveDirectionalPipeline);
    qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s->pipelineLayout, 0, 1, &s->descriptor, 0, NULL);
    qvkCmdPushConstants(cmd, s->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(*push), push);
    qvkCmdDispatch(cmd, (s->width + 7)/8, (s->height + 7)/8, 1);
    surface_image_barrier(cmd, s->output.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    s->outputWritten = qtrue;
    vk.cmd->last_pipeline = VK_NULL_HANDLE;
    vk.cmd->depth_range = DEPTH_RANGE_COUNT;
    Com_Memset(&vk.cmd->scissor_rect, 0xff, sizeof(vk.cmd->scissor_rect));
    /* Preserve engine descriptor values while forcing the next graphics bind:
     * private graphics descriptors use an incompatible pipeline layout. */
    vk.cmd->descriptor_set.start = ~0U;
    vk.cmd->descriptor_set.end = 0;
    for (i = 0; i < VK_DESC_COUNT; ++i) {
        if (vk.cmd->descriptor_set.current[i] != VK_NULL_HANDLE) {
            if (vk.cmd->descriptor_set.start == ~0U) vk.cmd->descriptor_set.start = i;
            vk.cmd->descriptor_set.end = i;
        }
    }
    return qtrue;
}

/* QA only: call after submitting the frame that produced this output, from the
 * renderer thread with no command buffer being recorded against these targets.
 * Pixels are tightly packed native-size premultiplied RGBA32F in image row
 * order. This is the private reconstruction output, before engine composition.
 * No display transfer, alpha unpremultiplication or image flip is performed. */
static qboolean vk_surface_capture_rgba(float *pixels, VkDeviceSize bytes) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    VkCommandBuffer cmd;
    VkBufferImageCopy copy;
    VkBufferMemoryBarrier readback;
    void *mapped;
    if (!s->ready || !s->outputWritten || !s->copyImageToBuffer || !pixels ||
            bytes != s->output.requested || bytes > s->readback.requested) return qfalse;
    cmd = begin_command_buffer();
    surface_image_barrier(cmd, s->output.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    Com_Memset(&readback, 0, sizeof(readback));
    readback.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    readback.srcAccessMask = VK_ACCESS_HOST_READ_BIT;
    readback.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    readback.srcQueueFamilyIndex = readback.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readback.buffer = s->readback.buffer; readback.size = VK_WHOLE_SIZE;
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 1, &readback, 0, NULL);
    Com_Memset(&copy, 0, sizeof(copy));
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = s->width; copy.imageExtent.height = s->height; copy.imageExtent.depth = 1;
    s->copyImageToBuffer(cmd, s->output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s->readback.buffer, 1, &copy);
    surface_image_barrier(cmd, s->output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    readback.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; readback.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, NULL, 1, &readback, 0, NULL);
    surface_submit_and_wait(cmd);
    VK_CHECK(qvkMapMemory(vk.device, s->readback.memory, 0, bytes, 0, &mapped));
    Com_Memcpy(pixels, mapped, (size_t)bytes);
    qvkUnmapMemory(vk.device, s->readback.memory);
    return qtrue;
}

/* Explicit QA only, same submitted-frame precondition as RGBA capture. Export
 * the 2W x 2H visibility image in image row order: one uint32 ID and two float
 * barycentrics per subpixel. The adapter writes dimensions in its JSON. This
 * does not enable shader diagnostics or access the dummy binding-6 buffer. */
static qboolean vk_surface_capture_quadrants(uint32_t *ids, VkDeviceSize idBytes,
        float *barycentrics, VkDeviceSize barycentricBytes) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    VkCommandBuffer cmd;
    VkBufferImageCopy copy;
    VkBufferMemoryBarrier readback;
    VkDeviceSize idOffset, barycentricOffset;
    VkPipelineStageFlags visibilityStages;
    VkAccessFlags visibilityAccess;
    void *mapped;
    if (!s->ready || !s->outputWritten || !s->visibilityWritten || !s->copyImageToBuffer ||
            !ids || !barycentrics || idBytes != s->id.requested || barycentricBytes != s->barycentric.requested)
        return qfalse;
    idOffset = s->output.requested;
    barycentricOffset = idOffset + idBytes;
    if (barycentricOffset + barycentricBytes > s->readback.requested) return qfalse;
    cmd = begin_command_buffer();
    visibilityStages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_TRANSFER_BIT;
    visibilityAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    surface_image_barrier(cmd, s->id.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        visibilityStages, VK_PIPELINE_STAGE_TRANSFER_BIT, visibilityAccess, VK_ACCESS_TRANSFER_READ_BIT);
    surface_image_barrier(cmd, s->barycentric.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        visibilityStages, VK_PIPELINE_STAGE_TRANSFER_BIT, visibilityAccess, VK_ACCESS_TRANSFER_READ_BIT);
    Com_Memset(&readback, 0, sizeof(readback));
    readback.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    readback.srcAccessMask = VK_ACCESS_HOST_READ_BIT; readback.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    readback.srcQueueFamilyIndex = readback.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readback.buffer = s->readback.buffer; readback.offset = idOffset;
    readback.size = idBytes + barycentricBytes;
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 1, &readback, 0, NULL);
    Com_Memset(&copy, 0, sizeof(copy));
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = 2*s->width; copy.imageExtent.height = 2*s->height; copy.imageExtent.depth = 1;
    copy.bufferOffset = idOffset;
    s->copyImageToBuffer(cmd, s->id.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s->readback.buffer, 1, &copy);
    copy.bufferOffset = barycentricOffset;
    s->copyImageToBuffer(cmd, s->barycentric.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s->readback.buffer, 1, &copy);
    surface_image_barrier(cmd, s->id.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
    surface_image_barrier(cmd, s->barycentric.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
    readback.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; readback.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, NULL, 1, &readback, 0, NULL);
    surface_submit_and_wait(cmd);
    VK_CHECK(qvkMapMemory(vk.device, s->readback.memory, 0, s->readback.requested, 0, &mapped));
    Com_Memcpy(ids, (const byte *)mapped + (size_t)idOffset, (size_t)idBytes);
    Com_Memcpy(barycentrics, (const byte *)mapped + (size_t)barycentricOffset, (size_t)barycentricBytes);
    qvkUnmapMemory(vk.device, s->readback.memory);
    return qtrue;
}

/* Additive, fixed-size two-pass control. The ordinary native path above remains
 * unchanged. We keep each image in an explicit layout, and only recreate these
 * objects at the adapter's fence-safe prepare point. */
#define SURFACE_FHD_WIDTH 1920u
#define SURFACE_FHD_HEIGHT 1080u
#define SURFACE_LR_WIDTH 480u
#define SURFACE_LR_HEIGHT 270u

#include "vk_surface_benchmark.inc"

static void surface_fhd_timing_read(void) {
    surfaceRenderer_t *s = &vk_surface_renderer;
    uint64_t result[2*SURFACE_QUERY_STRIDE];
    uint32_t i,count;
    if (!s->fhdQueries || !s->getQueryResults || vk.cmd_index<0 || vk.cmd_index>=NUM_COMMAND_BUFFERS || !s->querySlotSubmitted[vk.cmd_index]) return;
    s->timingReady = qfalse;
    count=s->querySlotMode[vk.cmd_index]>=10?SURFACE_QUERY_STRIDE:8u;
    Com_Memset(s->timestampValues,0,sizeof(s->timestampValues));
    if (s->getQueryResults(vk.device, s->fhdQueries, SURFACE_QUERY_STRIDE*(uint32_t)vk.cmd_index, count, sizeof(result), result,
            2*sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != VK_SUCCESS) return;
    for (i=0;i<count;i++) if (!result[2*i+1]) return;
    for (i=0;i<count;i++) s->timestampValues[i]=result[2*i];
    s->timingMode=s->querySlotMode[vk.cmd_index];
    s->timingSerial=s->querySlotSerial[vk.cmd_index];
    s->timingReady=qtrue;
}

static void surface_fhd_submitted(const surfacePush_t *push, int pose, qboolean captured) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    if(!s->fhdReady || !s->outputWritten || !r_surfaceUpscale || r_surfaceUpscale->integer<1 || r_surfaceUpscale->integer>11)return;
    s->querySlotSubmitted[vk.cmd_index]=qtrue;
    s->querySlotMode[vk.cmd_index]=s->fhdLastMode;
    s->querySlotSerial[vk.cmd_index]=++s->submittedSerial;
    s->querySlotPose[vk.cmd_index]=pose;
    s->querySlotPush[vk.cmd_index]=*push;
    s->querySlotCaptured[vk.cmd_index]=captured;
    Q_strncpyz(s->querySlotCnnSHA[vk.cmd_index],s->fhdLastMode==10?surface_cnn_sha_for_width(s->cnnWidth):"none",sizeof(s->querySlotCnnSHA[vk.cmd_index]));
    s->querySlotPixelWidth[vk.cmd_index]=s->fhdLastMode==10?s->cnnWidth:s->fhdLastMode==8?s->pixelWidth:0;
    s->querySlotPixelParameters[vk.cmd_index]=s->fhdLastMode==10?s->cnnParameters:s->fhdLastMode==8?s->pixelParameters:0;
    s->querySlotPixelBytes[vk.cmd_index]=s->fhdLastMode==10?(uint32_t)s->cnnModel.requested:s->fhdLastMode==8?(uint32_t)s->pixelModel.requested:0;
    s->querySlotPixelMACs[vk.cmd_index]=s->fhdLastMode==10?s->cnnMACsPerLR/16u:s->fhdLastMode==8?s->pixelMACs:0;
    s->querySlotPixelDiagnostic[vk.cmd_index]=s->fhdLastMode==10?SURFACE_CNN_DIAGNOSTIC:s->fhdLastMode==8?SURFACE_PIXEL_DIAGNOSTIC:s->fhdLastMode==6?1u:0u;
    s->querySlotCPUReady[vk.cmd_index]=qfalse;
    s->querySlotBenchmarkEpoch[vk.cmd_index]=surface_benchmark.active?surface_benchmark.epoch:0u;
}

static double surface_fhd_timestamp_ms(uint32_t begin, uint32_t end) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    uint64_t difference;
    if (!s->timingReady || begin>=SURFACE_QUERY_STRIDE || end>=SURFACE_QUERY_STRIDE || end<begin) return -1.0;
    difference=s->timestampValues[end]-s->timestampValues[begin];
    if (s->timestampValidBits && s->timestampValidBits<64)
        difference &= (((uint64_t)1 << s->timestampValidBits)-1);
    return (double)difference*(double)s->timestampPeriod/1000000.0;
}

static void surface_fhd_timestamp(VkCommandBuffer cmd, uint32_t index) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    if (s->fhdQueries && s->writeTimestamp)
        s->writeTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, s->fhdQueries, SURFACE_QUERY_STRIDE*(uint32_t)vk.cmd_index+index);
}

static void surface_fhd_frame_begin(void) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    /* The prior slot was consumed at prepare. Non-FHD submissions must never
     * inherit its query validity or acquire CPU timing on stale FHD work. */
    s->querySlotSubmitted[vk.cmd_index]=qfalse;
    s->querySlotCPUReady[vk.cmd_index]=qfalse;
    if (!s->fhdReady || !r_surfaceUpscale || r_surfaceUpscale->integer<1 || r_surfaceUpscale->integer>11) return;
    if (s->fhdQueries && s->resetQueryPool) {
        s->querySlotSubmitted[vk.cmd_index]=qfalse;
        s->timingReady=qfalse;
        s->resetQueryPool(vk.cmd->command_buffer,s->fhdQueries,SURFACE_QUERY_STRIDE*(uint32_t)vk.cmd_index,SURFACE_QUERY_STRIDE);
        surface_fhd_timestamp(vk.cmd->command_buffer,0);
    }
}

static qboolean surface_create_fhd(const void *model, VkDeviceSize modelBytes,
        VkShaderModule linearResolve, VkShaderModule upscale, VkShaderModule upscaleFast,
        VkShaderModule bilinearFast, VkShaderModule firstHitGated) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    VkDescriptorSetLayoutBinding binding[15];
    VkDescriptorSetLayoutCreateInfo layout;
    VkDescriptorPoolSize sizes[3];
    VkDescriptorPoolCreateInfo pool;
    VkDescriptorSetAllocateInfo allocate;
    VkPipelineLayoutCreateInfo pipeline;
    VkPushConstantRange push;
    VkDescriptorBufferInfo buffers[6];
    VkDescriptorImageInfo images[9];
    VkWriteDescriptorSet write[15];
    VkComputePipelineCreateInfo compute;
    VkDescriptorSetLayout layouts[2];
    VkDescriptorSet sets[2];
    VkPhysicalDeviceProperties properties;
    VkQueueFamilyProperties *families;
    VkQueryPoolCreateInfo query;
    VkCommandBuffer cmd;
    uint32_t familyCount=0, i, pass;
    VkImageUsageFlags sampled=VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImageUsageFlags storage=VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!s->ready || ((s->width!=SURFACE_FHD_WIDTH || s->height!=SURFACE_FHD_HEIGHT) &&
            !surface_capture4x_extent(s->width,s->height)) ||
            !s->hasFirstHit || !s->hasCorrection || !model || modelBytes!=8876 ||
            !linearResolve || !upscale || !upscaleFast || !bilinearFast || !firstHitGated || !surface_format_supported(VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) return qfalse;
    qvkGetPhysicalDeviceProperties(vk.physical_device,&properties);
    s->fhdVendorID=properties.vendorID;s->fhdDeviceID=properties.deviceID;
    s->fhdDriverVersion=properties.driverVersion;s->fhdAPIVersion=properties.apiVersion;
    s->fhdMaxImageDimension2D=properties.limits.maxImageDimension2D;
    if (properties.limits.maxImageDimension2D<2*s->width) return qfalse;
    s->lrWidth=s->width/4u;s->lrHeight=s->height/4u;
    surface_upload_buffer(&s->upscale,model,modelBytes);
    surface_allocate_image(&s->lrID,2*s->lrWidth,2*s->lrHeight,VK_FORMAT_R32_UINT,4,sampled,VK_IMAGE_ASPECT_COLOR_BIT);
    surface_allocate_image(&s->lrBarycentric,2*s->lrWidth,2*s->lrHeight,VK_FORMAT_R32G32_SFLOAT,8,sampled,VK_IMAGE_ASPECT_COLOR_BIT);
    surface_allocate_image(&s->lrLinear,s->lrWidth,s->lrHeight,VK_FORMAT_R32G32B32A32_SFLOAT,16,sampled,VK_IMAGE_ASPECT_COLOR_BIT);
    surface_allocate_image(&s->lrCenterGate,s->lrWidth,s->lrHeight,VK_FORMAT_R32_UINT,4,storage,VK_IMAGE_ASPECT_COLOR_BIT);
    surface_allocate_image(&s->hrCenterGate,s->width,s->height,VK_FORMAT_R32_UINT,4,storage,VK_IMAGE_ASPECT_COLOR_BIT);
    surface_allocate_image(&s->geometryLinear,s->width,s->height,VK_FORMAT_R32G32B32A32_SFLOAT,16,storage,VK_IMAGE_ASPECT_COLOR_BIT);
    Com_Memset(binding,0,sizeof(binding));
    for (i=0;i<15;i++) {
        binding[i].binding=i;binding[i].descriptorCount=1;
        binding[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        binding[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    binding[3].descriptorType=binding[4].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding[9].descriptorType=binding[10].descriptorType=binding[11].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding[5].descriptorType=binding[12].descriptorType=binding[13].descriptorType=binding[14].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    Com_Memset(&layout,0,sizeof(layout));layout.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount=15;layout.pBindings=binding;
    VK_CHECK(qvkCreateDescriptorSetLayout(vk.device,&layout,NULL,&s->fhdSetLayout));
    Com_Memset(&push,0,sizeof(push));push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;push.size=sizeof(surfacePush_t);
    Com_Memset(&pipeline,0,sizeof(pipeline));pipeline.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline.setLayoutCount=1;pipeline.pSetLayouts=&s->fhdSetLayout;
    pipeline.pushConstantRangeCount=1;pipeline.pPushConstantRanges=&push;
    VK_CHECK(qvkCreatePipelineLayout(vk.device,&pipeline,NULL,&s->fhdPipelineLayout));
    sizes[0].type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;sizes[0].descriptorCount=14;
    sizes[1].type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;sizes[1].descriptorCount=10;
    sizes[2].type=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;sizes[2].descriptorCount=10;
    Com_Memset(&pool,0,sizeof(pool));pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets=3;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
    VK_CHECK(qvkCreateDescriptorPool(vk.device,&pool,NULL,&s->fhdPool));
    layouts[0]=layouts[1]=s->fhdSetLayout;
    Com_Memset(&allocate,0,sizeof(allocate));allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool=s->fhdPool;allocate.descriptorSetCount=2;allocate.pSetLayouts=layouts;
    VK_CHECK(qvkAllocateDescriptorSets(vk.device,&allocate,sets));
    s->fhdDescriptor=sets[0];s->lrResolveDescriptor=sets[1];
    for (pass=0;pass<2;pass++) {
        Com_Memset(buffers,0,sizeof(buffers));
        buffers[0].buffer=s->triangles.buffer;buffers[1].buffer=s->materials.buffer;
        buffers[2].buffer=pass?s->appearance.buffer:s->upscale.buffer;
        buffers[3].buffer=s->diagnostic.buffer;buffers[4].buffer=s->correction.buffer;buffers[5].buffer=s->bvh.buffer;
        for(i=0;i<6;i++)buffers[i].range=VK_WHOLE_SIZE;
        Com_Memset(images,0,sizeof(images));
        images[0].imageView=pass?s->lrID.view:s->id.view;
        images[1].imageView=pass?s->lrBarycentric.view:s->barycentric.view;
        images[2].imageView=pass?s->lrLinear.view:s->output.view;
        images[3].imageView=s->lrID.view;images[4].imageView=s->lrBarycentric.view;
        images[5].imageView=s->lrLinear.view;images[6].imageView=s->geometryLinear.view;
        images[7].imageView=s->hrCenterGate.view;images[8].imageView=s->lrCenterGate.view;
        for(i=0;i<9;i++) {
            images[i].imageLayout=(i==0 || i==1 || (i>=3 && i<=5))?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
            if(images[i].imageLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)images[i].sampler=s->nearestSampler;
        }
        Com_Memset(write,0,sizeof(write));
        for(i=0;i<15;i++) {
            write[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write[i].dstSet=sets[pass];write[i].dstBinding=i;
            write[i].descriptorCount=1;write[i].descriptorType=binding[i].descriptorType;
            if(i<3)write[i].pBufferInfo=&buffers[i];
            else if(i>=6 && i<=8)write[i].pBufferInfo=&buffers[i-3];
            else write[i].pImageInfo=&images[i<6?i-3:i-6];
        }
        qvkUpdateDescriptorSets(vk.device,15,write,0,NULL);
    }
    /* Canonical first-hit pipeline/layout are reused with an LR-only set. */
    allocate.descriptorSetCount=1;allocate.pSetLayouts=&s->firstHitSetLayout;
    VK_CHECK(qvkAllocateDescriptorSets(vk.device,&allocate,&s->lrFirstHitDescriptor));
    Com_Memset(write,0,4*sizeof(write[0]));Com_Memset(images,0,2*sizeof(images[0]));
    images[0].imageView=s->lrID.view;images[1].imageView=s->lrBarycentric.view;
    images[0].imageLayout=images[1].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    for(i=0;i<4;i++) {
        static const uint32_t numbers[4]={0,3,4,8};
        write[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write[i].dstSet=s->lrFirstHitDescriptor;
        write[i].dstBinding=numbers[i];write[i].descriptorCount=1;
        write[i].descriptorType=(i==0 || i==3)?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        if(i==0)write[i].pBufferInfo=&buffers[0];
        else if(i==3)write[i].pBufferInfo=&buffers[5];
        else write[i].pImageInfo=&images[i-1];
    }
    qvkUpdateDescriptorSets(vk.device,4,write,0,NULL);
    Com_Memset(&compute,0,sizeof(compute));compute.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute.layout=s->fhdPipelineLayout;
    set_shader_stage_desc(&compute.stage,VK_SHADER_STAGE_COMPUTE_BIT,linearResolve,"main");
    VK_CHECK(qvkCreateComputePipelines(vk.device,vk.pipelineCache,1,&compute,NULL,&s->lrResolvePipeline));
    set_shader_stage_desc(&compute.stage,VK_SHADER_STAGE_COMPUTE_BIT,upscale,"main");
    VK_CHECK(qvkCreateComputePipelines(vk.device,vk.pipelineCache,1,&compute,NULL,&s->upscalePipeline));
    set_shader_stage_desc(&compute.stage,VK_SHADER_STAGE_COMPUTE_BIT,upscaleFast,"main");
    VK_CHECK(qvkCreateComputePipelines(vk.device,vk.pipelineCache,1,&compute,NULL,&s->upscaleFastPipeline));
    set_shader_stage_desc(&compute.stage,VK_SHADER_STAGE_COMPUTE_BIT,bilinearFast,"main");
    VK_CHECK(qvkCreateComputePipelines(vk.device,vk.pipelineCache,1,&compute,NULL,&s->bilinearFastPipeline));
    compute.layout=s->firstHitPipelineLayout;
    set_shader_stage_desc(&compute.stage,VK_SHADER_STAGE_COMPUTE_BIT,firstHitGated,"main");
    VK_CHECK(qvkCreateComputePipelines(vk.device,vk.pipelineCache,1,&compute,NULL,&s->firstHitGatedPipeline));
    cmd=begin_command_buffer();
    surface_image_barrier(cmd,s->lrLinear.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,VK_ACCESS_SHADER_WRITE_BIT);
    surface_image_barrier(cmd,s->lrCenterGate.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,VK_ACCESS_SHADER_WRITE_BIT);
    surface_image_barrier(cmd,s->hrCenterGate.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,VK_ACCESS_SHADER_WRITE_BIT);
    surface_image_barrier(cmd,s->geometryLinear.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,VK_ACCESS_SHADER_WRITE_BIT);
    surface_submit_and_wait(cmd);
    s->createQueryPool=(PFN_vkCreateQueryPool)qvkGetDeviceProcAddr(vk.device,"vkCreateQueryPool");
    s->destroyQueryPool=(PFN_vkDestroyQueryPool)qvkGetDeviceProcAddr(vk.device,"vkDestroyQueryPool");
    s->resetQueryPool=(PFN_vkCmdResetQueryPool)qvkGetDeviceProcAddr(vk.device,"vkCmdResetQueryPool");
    s->writeTimestamp=(PFN_vkCmdWriteTimestamp)qvkGetDeviceProcAddr(vk.device,"vkCmdWriteTimestamp");
    s->getQueryResults=(PFN_vkGetQueryPoolResults)qvkGetDeviceProcAddr(vk.device,"vkGetQueryPoolResults");
    qvkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device,&familyCount,NULL);
    if(familyCount && familyCount<=256 && vk.queue_family_index<familyCount) {
        families=ri.Malloc((int)(familyCount*sizeof(*families)));
        qvkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device,&familyCount,families);
        s->timestampValidBits=families[vk.queue_family_index].timestampValidBits;ri.Free(families);
    }
    s->timestampPeriod=properties.limits.timestampPeriod;
    if(s->timestampValidBits && s->createQueryPool && s->destroyQueryPool && s->resetQueryPool && s->writeTimestamp && s->getQueryResults) {
        Com_Memset(&query,0,sizeof(query));query.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=SURFACE_QUERY_STRIDE*NUM_COMMAND_BUFFERS;
        VK_CHECK(s->createQueryPool(vk.device,&query,NULL,&s->fhdQueries));
    }
    s->fhdReady=qtrue;
    ri.Printf(PRINT_ALL,"Surface two-pass resources ready: actual output%ux%u LR%ux%u mode%d; retained legacy buffer8876B, no claim of inactive decoder work\n",s->width,s->height,s->lrWidth,s->lrHeight,r_surfaceUpscale->integer);
    ri.Printf(PRINT_ALL,"FHD extra target requested/allocated: LR IDs%llu/%llu bary%llu/%llu linear%llu/%llu center%llu/%llu; HR center%llu/%llu geometry base linear%llu/%llu; model%llu/%lluB. Readback and original renderer targets listed separately.\n",
        (unsigned long long)s->lrID.requested,(unsigned long long)s->lrID.allocated,
        (unsigned long long)s->lrBarycentric.requested,(unsigned long long)s->lrBarycentric.allocated,
        (unsigned long long)s->lrLinear.requested,(unsigned long long)s->lrLinear.allocated,
        (unsigned long long)s->lrCenterGate.requested,(unsigned long long)s->lrCenterGate.allocated,
        (unsigned long long)s->hrCenterGate.requested,(unsigned long long)s->hrCenterGate.allocated,
        (unsigned long long)s->geometryLinear.requested,(unsigned long long)s->geometryLinear.allocated,
        (unsigned long long)s->upscale.requested,(unsigned long long)s->upscale.allocated);
    return qtrue;
}

#include "vk_surface_pixel.inc"
#include "vk_surface_cnn.inc"

static void surface_fhd_visibility(VkCommandBuffer cmd, surfaceImage_t *id,
        surfaceImage_t *bary, VkDescriptorSet set, const surfacePush_t *push, qboolean written, qboolean gated) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    VkImageLayout before=written?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags stage=written?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags access=written?(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT):0;
    surface_image_barrier(cmd,id->image,before,VK_IMAGE_LAYOUT_GENERAL,stage,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,access,VK_ACCESS_SHADER_WRITE_BIT);
    surface_image_barrier(cmd,bary->image,before,VK_IMAGE_LAYOUT_GENERAL,stage,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,access,VK_ACCESS_SHADER_WRITE_BIT);
    qvkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,gated?s->firstHitGatedPipeline:s->firstHitPipeline);
    qvkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->firstHitPipelineLayout,0,1,&set,0,NULL);
    qvkCmdPushConstants(cmd,s->firstHitPipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(*push),push);
    qvkCmdDispatch(cmd,(2*push->info[0]+7)/8,(2*push->info[1]+7)/8,1);
    surface_image_barrier(cmd,id->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    surface_image_barrier(cmd,bary->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
}

static qboolean surface_fhd_draw(const surfacePush_t *hr, uint32_t mode) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    VkCommandBuffer cmd=vk.cmd->command_buffer;
    surfacePush_t lr=*hr, output=*hr;
    int i;
    s->outputWritten=qfalse;s->hrDiagnosticWritten=qfalse;
    s->fhdLRCurrent=qfalse;s->fhdHRGuidesCurrent=qfalse;
    if(!s->fhdReady || mode<1 || mode>11 || (mode>=8 && (!s->pixelReady || (mode==8 && !s->pixelModelReady) || (mode==10 && !s->cnnReady))) ||
       hr->info[0]!=s->width || hr->info[1]!=s->height || hr->info[2]!=2u ||
       ((s->width!=SURFACE_FHD_WIDTH || s->height!=SURFACE_FHD_HEIGHT) && (mode!=9 || !surface_capture4x_extent(s->width,s->height)))) return qfalse;
    lr.info[0]=s->lrWidth;lr.info[1]=s->lrHeight;
    surface_fhd_timestamp(cmd,1);
    if(mode!=4) {
        surface_fhd_visibility(cmd,&s->lrID,&s->lrBarycentric,s->lrFirstHitDescriptor,&lr,s->lrVisibilityWritten,qfalse);
        s->lrVisibilityWritten=qtrue;
    }
    surface_fhd_timestamp(cmd,2);
    if(mode!=4) {
        surface_image_barrier(cmd,s->lrLinear.image,s->lrWritten?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT);
        surface_image_barrier(cmd,s->lrCenterGate.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT);
        qvkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->lrResolvePipeline);
        qvkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->fhdPipelineLayout,0,1,&s->lrResolveDescriptor,0,NULL);
        qvkCmdPushConstants(cmd,s->fhdPipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(lr),&lr);
        qvkCmdDispatch(cmd,(s->lrWidth+7)/8,(s->lrHeight+7)/8,1);
        surface_image_barrier(cmd,s->lrLinear.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        s->lrWritten=qtrue;
        s->fhdLRCurrent=qtrue;
    }
    surface_fhd_timestamp(cmd,3);
    if(mode>=8) {
        /* q3..q4 deliberately contains no HR visibility, gate, guide load or
         * diagnostic image barrier. Only LR pixels and subpixel phase enter. */
        surface_fhd_timestamp(cmd,4);
        if(mode==10)surface_cnn_dispatch(cmd);
        else {
            if(mode==11){surface_fhd_timestamp(cmd,8);surface_fhd_timestamp(cmd,9);surface_fhd_timestamp(cmd,10);}
            surface_pixel_dispatch(cmd,mode==11?9u:mode);
            if(mode==11)surface_fhd_timestamp(cmd,11);
        }
        goto pixel_finished_dispatch;
    }
    if(mode!=2 && mode!=6) {
        surface_fhd_visibility(cmd,&s->id,&s->barycentric,s->firstHitDescriptor,hr,s->visibilityWritten,mode==7);
        s->visibilityWritten=qtrue;s->fhdHRGuidesCurrent=qtrue;
    } else if(!s->visibilityWritten) {
        /* Even textureSize-only descriptors require their declared layout. No
         * HR rays or guide reads occur in the bilinear kernel. */
        surface_image_barrier(cmd,s->id.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,VK_ACCESS_SHADER_READ_BIT);
        surface_image_barrier(cmd,s->barycentric.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,VK_ACCESS_SHADER_READ_BIT);
        s->visibilityWritten=qtrue;
    }
    surface_fhd_timestamp(cmd,4);
    surface_image_barrier(cmd,s->output.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    if(mode==4) {
        qvkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->resolveSmall32Pipeline);
        qvkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipelineLayout,0,1,&s->descriptor,0,NULL);
        qvkCmdPushConstants(cmd,s->pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(*hr),hr);
    } else {
        surface_image_barrier(cmd,s->hrCenterGate.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT);
        surface_image_barrier(cmd,s->geometryLinear.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT);
        if(mode==2 || mode==6)output.info[2]|=8u;else if(mode==3)output.info[2]|=16u;
        qvkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,mode==6?s->bilinearFastPipeline:
            (mode==5 || mode==7)?s->upscaleFastPipeline:s->upscalePipeline);
        qvkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->fhdPipelineLayout,0,1,&s->fhdDescriptor,0,NULL);
        qvkCmdPushConstants(cmd,s->fhdPipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(output),&output);
        s->hrDiagnosticWritten=qtrue;
    }
    qvkCmdDispatch(cmd,(s->width+7)/8,(s->height+7)/8,1);
pixel_finished_dispatch:
    surface_fhd_timestamp(cmd,5);
    surface_image_barrier(cmd,s->output.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    s->outputWritten=qtrue;s->fhdLastMode=mode;
    /* Restore the engine's descriptor cache after private compute layouts. */
    vk.cmd->descriptor_set.start=~0U;vk.cmd->descriptor_set.end=0;
    for(i=0;i<VK_DESC_COUNT;i++)if(vk.cmd->descriptor_set.current[i]!=VK_NULL_HANDLE) {
        if(vk.cmd->descriptor_set.start==~0U)vk.cmd->descriptor_set.start=i;
        vk.cmd->descriptor_set.end=i;
    }
    return qtrue;
}

/* Generic submitted-frame readback. One shared, bounded host buffer is reused
 * serially; this is explicit QA work and is not included in frame timings. */
static qboolean surface_fhd_capture_image(surfaceImage_t *image, uint32_t width,
        uint32_t height, VkImageLayout layout, void *data, VkDeviceSize bytes) {
    surfaceRenderer_t *s=&vk_surface_renderer;
    VkCommandBuffer cmd;
    VkBufferImageCopy copy;
    VkBufferMemoryBarrier barrier;
    void *mapped;
    /* Native mode4 at512/960 uses the already valid base renderer without
     * allocating inactive LR/FHD resources. Its submitted HR guides may
     * use the same bounded readback buffer as the FHD path. */
    if((!s->fhdReady && !(s->ready && r_surfaceUpscale && r_surfaceUpscale->integer==4 &&
          s->fhdLastMode==4 && s->fhdHRGuidesCurrent && !s->fhdLRCurrent &&
          ((s->width==512u && s->height==288u) || (s->width==960u && s->height==540u) ||
           (s->width==1920u && s->height==1080u)))) ||
            !s->outputWritten || !s->copyImageToBuffer || !image->image ||
            !data || bytes!=image->requested || bytes>s->readback.requested) return qfalse;
    cmd=begin_command_buffer();
    surface_image_barrier(cmd,image->image,layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    Com_Memset(&barrier,0,sizeof(barrier));barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer=s->readback.buffer;barrier.size=VK_WHOLE_SIZE;
    qvkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&barrier,0,NULL);
    Com_Memset(&copy,0,sizeof(copy));copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount=1;copy.imageExtent.width=width;copy.imageExtent.height=height;copy.imageExtent.depth=1;
    s->copyImageToBuffer(cmd,image->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s->readback.buffer,1,&copy);
    surface_image_barrier(cmd,image->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,layout,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    qvkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);
    surface_submit_and_wait(cmd);
    VK_CHECK(qvkMapMemory(vk.device,s->readback.memory,0,bytes,0,&mapped));
    Com_Memcpy(data,mapped,(size_t)bytes);qvkUnmapMemory(vk.device,s->readback.memory);
    return qtrue;
}
