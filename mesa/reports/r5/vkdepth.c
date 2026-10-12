/* vkdepth: does HiZ work in hasvk on CHV at all? Native Vulkan, offscreen, no window.
 * Each frame: clear, then LAYERS full-screen triangles front to back with depth test LESS_OR_EQUAL,
 * so all but the first are hidden. With working HiZ, hidden layers are rejected in bulk and
 * INTEL_DEBUG=nohiz should be clearly slower. Prints ms per frame.
 * Build (on the GPD): cc -O2 -o vkdepth vkdepth.c -lvulkan   (needs q_vert.inc/q_frag.inc from
 *   glslc -mfmt=c, shaders in the comment at the end)
 * Run: ./vkdepth [format: d16|d24s8|d32] [frames]   and   INTEL_DEBUG=nohiz ./vkdepth ... */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 960
#define H 540
#define LAYERS 200
#define CK(x) do { VkResult r_ = (x); if (r_) { fprintf(stderr, "%s: %d\n", #x, r_); exit(1); } } while (0)

static const uint32_t vs[] =
#include "q_vert.inc"
;
static const uint32_t fs[] =
#include "q_frag.inc"
;

static VkDevice dev;
static VkPhysicalDevice pd;

static uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    exit(2);
}

static VkImage image(VkFormat f, VkImageUsageFlags u, VkImageView *view, VkImageAspectFlags aspect)
{
    VkImageCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = f, .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1, .samples = 1,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = u };
    VkImage img;
    CK(vkCreateImage(dev, &ci, NULL, &img));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
        .memoryTypeIndex = memtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    VkDeviceMemory m;
    CK(vkAllocateMemory(dev, &ai, NULL, &m));
    CK(vkBindImageMemory(dev, img, m, 0));
    VkImageViewCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = f, .subresourceRange = { aspect, 0, 1, 0, 1 } };
    CK(vkCreateImageView(dev, &vi, NULL, view));
    return img;
}

int main(int argc, char **argv)
{
    const char *fmtname = argc > 1 ? argv[1] : "d16";
    int frames = argc > 2 ? atoi(argv[2]) : 200;
    VkFormat df = !strcmp(fmtname, "d24s8") ? VK_FORMAT_D24_UNORM_S8_UINT
                : !strcmp(fmtname, "d32") ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_D16_UNORM;
    VkImageAspectFlags daspect = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    CK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 1;
    vkEnumeratePhysicalDevices(inst, &n, &pd);
    float prio = 1;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
    CK(vkCreateDevice(pd, &dci, NULL, &dev));
    VkQueue q;
    vkGetDeviceQueue(dev, 0, 0, &q);

    VkImageView cview, dview;
    VkImage cimg = image(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         &cview, VK_IMAGE_ASPECT_COLOR_BIT);
    VkImage dimg = image(df, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &dview, daspect);

    VkAttachmentDescription att[2] = {
        { .format = VK_FORMAT_R8G8B8A8_UNORM, .samples = 1, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { .format = df, .samples = 1, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL } };
    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &cref, .pDepthStencilAttachment = &dref };
    VkRenderPassCreateInfo rpi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 2,
        .pAttachments = att, .subpassCount = 1, .pSubpasses = &sp };
    VkRenderPass rp;
    CK(vkCreateRenderPass(dev, &rpi, NULL, &rp));
    VkImageView views[2] = { cview, dview };
    VkFramebufferCreateInfo fbi = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = rp,
        .attachmentCount = 2, .pAttachments = views, .width = W, .height = H, .layers = 1 };
    VkFramebuffer fb;
    CK(vkCreateFramebuffer(dev, &fbi, NULL, &fb));

    VkShaderModule sm[2];
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof vs, .pCode = vs };
    CK(vkCreateShaderModule(dev, &smi, NULL, &sm[0]));
    smi.codeSize = sizeof fs;
    smi.pCode = fs;
    CK(vkCreateShaderModule(dev, &smi, NULL, &sm[1]));
    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4 };
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr };
    VkPipelineLayout pl;
    CK(vkCreatePipelineLayout(dev, &pli, NULL, &pl));
    VkPipelineShaderStageCreateInfo st[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = sm[0], .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = sm[1], .pName = "main" } };
    VkPipelineVertexInputStateCreateInfo vin = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkViewport vp = { 0, 0, W, H, 0, 1 };
    VkRect2D sc = { { 0, 0 }, { W, H } };
    VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1 };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = 1 };
    VkPipelineDepthStencilStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = 1, .depthWriteEnable = 1, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL };
    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cba };
    VkGraphicsPipelineCreateInfo gpi = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2,
        .pStages = st, .pVertexInputState = &vin, .pInputAssemblyState = &ia, .pViewportState = &vps,
        .pRasterizationState = &rs, .pMultisampleState = &ms, .pDepthStencilState = &ds, .pColorBlendState = &cb,
        .layout = pl, .renderPass = rp };
    VkPipeline pipe;
    CK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, NULL, &pipe));

    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandPool pool;
    CK(vkCreateCommandPool(dev, &cpi, NULL, &pool));
    VkCommandBufferAllocateInfo cbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cmd;
    CK(vkAllocateCommandBuffers(dev, &cbi, &cmd));
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CK(vkBeginCommandBuffer(cmd, &bi));
    VkClearValue cv[2] = { { .color = { { 0, 0, 0.2f, 1 } } }, { .depthStencil = { 1, 0 } } };
    VkRenderPassBeginInfo rbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = rp,
        .framebuffer = fb, .renderArea = sc, .clearValueCount = 2, .pClearValues = cv };
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    for (int i = 0; i < LAYERS; i++) {
        float z = 0.1f + 0.8f * i / LAYERS;
        vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &z);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmd);
    CK(vkEndCommandBuffer(cmd));

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
    CK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE)); /* warm-up */
    CK(vkQueueWaitIdle(q));
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int f = 0; f < frames; f++) {
        CK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
        CK(vkQueueWaitIdle(q));
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    double msf = ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6) / frames;
    printf("%-6s %dx%d, %d layers (%d hidden): %.2f ms/frame\n", fmtname, W, H, LAYERS, LAYERS - 1, msf);

    /* Check the result: the centre pixel must be layer 0 (nearest, z = 0.1): colour (26,128,230), depth 0.1.
     * Reading the depth goes through the driver's HiZ resolve when HiZ is on. */
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 64, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    VkBuffer rb;
    CK(vkCreateBuffer(dev, &bci, NULL, &rb));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, rb, &mr);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
        .memoryTypeIndex = memtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory rm;
    CK(vkAllocateMemory(dev, &ai, NULL, &rm));
    CK(vkBindBufferMemory(dev, rb, rm, 0));
    VkCommandBuffer c2;
    CK(vkAllocateCommandBuffers(dev, &cbi, &c2));
    CK(vkBeginCommandBuffer(c2, &bi));
    VkImageMemoryBarrier bar[2] = {
        { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT, .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .image = cimg, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } },
        { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT, .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .image = dimg,
          .subresourceRange = { df == VK_FORMAT_D24_UNORM_S8_UINT ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 } } };
    vkCmdPipelineBarrier(c2, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, bar);
    VkBufferImageCopy cc = { .bufferOffset = 0, .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { W / 2, H / 2, 0 }, .imageExtent = { 1, 1, 1 } };
    vkCmdCopyImageToBuffer(c2, cimg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &cc);
    VkBufferImageCopy dc = { .bufferOffset = 16, .imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 },
        .imageOffset = { W / 2, H / 2, 0 }, .imageExtent = { 1, 1, 1 } };
    vkCmdCopyImageToBuffer(c2, dimg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &dc);
    CK(vkEndCommandBuffer(c2));
    VkSubmitInfo s2 = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &c2 };
    CK(vkQueueSubmit(q, 1, &s2, VK_NULL_HANDLE));
    CK(vkQueueWaitIdle(q));
    unsigned char *p;
    CK(vkMapMemory(dev, rm, 0, 64, 0, (void **)&p));
    double depth = df == VK_FORMAT_D16_UNORM ? *(uint16_t *)(p + 16) / 65535.0
                 : df == VK_FORMAT_D32_SFLOAT ? *(float *)(p + 16)
                 : (*(uint32_t *)(p + 16) & 0xffffff) / 16777215.0;
    int ok = abs(p[0] - 26) <= 2 && abs(p[1] - 128) <= 2 && abs(p[2] - 230) <= 2 && depth > 0.099 && depth < 0.101;
    printf("       centre colour (%d,%d,%d) want (26,128,230), depth %.4f want 0.1000: %s\n",
           p[0], p[1], p[2], depth, ok ? "OK" : "WRONG");
    return 0;
}
/* q.vert: #version 450 / layout(push_constant) uniform P { float z; } p;
 *   void main() { vec2 v = vec2((gl_VertexIndex & 1) * 4.0 - 1.0, (gl_VertexIndex >> 1) * 4.0 - 1.0);
 *                 gl_Position = vec4(v, p.z, 1.0); }
 * q.frag: #version 450 / layout(location = 0) out vec4 c; layout(push_constant) uniform P { float z; } p;
 *   void main() { c = vec4(p.z, 0.5, 1.0 - p.z, 1.0); } */
