/*
 * glow_vk_test <shader.spv> <in.argb> <src.argb> <out.argb> [strength/256] [repeats]: the GPU glow (ps3recomp
 * libs/video/vulkan/rsx_vk_glow.comp) on a raw 1280x720 A,R,G,B image with a 640x360 glow source, headless, with the
 * stage sequence the renderer records; prints the GPU time of one run. For checking it against glow_ref_cli all
 * (made from mb_vk_test.c).
 */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 1280
#define H 720
#define HW 640
#define HH 360
#define NB 6
#define QW 320
#define QH 180
#define CK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #x, _r); exit(1); } } while (0)

static VkDevice dev; static VkPhysicalDevice phys;
typedef struct { VkBuffer b; VkDeviceMemory m; void* map; VkDeviceSize size; } buf_t;
static uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    fprintf(stderr, "no memory type\n"); exit(1);
}
static buf_t mkbuf(VkDeviceSize size, int host)
{
    buf_t r = { 0 }; r.size = size;
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT };
    CK(vkCreateBuffer(dev, &bi, NULL, &r.b));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, r.b, &mr);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
        .memoryTypeIndex = memtype(mr.memoryTypeBits, host ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    CK(vkAllocateMemory(dev, &ai, NULL, &r.m)); CK(vkBindBufferMemory(dev, r.b, r.m, 0));
    if (host) CK(vkMapMemory(dev, r.m, 0, size, 0, &r.map));
    return r;
}
static void barrier(VkCommandBuffer cb)
{
    VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
}

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage: glow_vk_test shader.spv in.argb src.argb out.argb [strength] [repeats]\n"); return 1; }
    const int strength = argc > 5 ? atoi(argv[5]) : 256, reps = argc > 6 ? atoi(argv[6]) : 1;
    FILE* f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long ssz = ftell(f); rewind(f);
    uint32_t* spv = malloc(ssz); if (fread(spv, 1, ssz, f) != (size_t)ssz) return 1; fclose(f);
    uint8_t* img = malloc(W * H * 4);
    f = fopen(argv[2], "rb"); if (!f || fread(img, 4, W * H, f) != W * H) { fprintf(stderr, "bad input\n"); return 1; } fclose(f);
    uint8_t* fld = malloc(HW * HH * 4);
    f = fopen(argv[3], "rb"); if (!f || fread(fld, 4, HW * HH, f) != HW * HH) { fprintf(stderr, "bad field\n"); return 1; } fclose(f);

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst; CK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 8; VkPhysicalDevice pds[8]; CK(vkEnumeratePhysicalDevices(inst, &n, pds));
    phys = pds[0];
    for (uint32_t i = 0; i < n; i++) { VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pds[i], &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { phys = pds[i]; break; } }
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(phys, &props);
    uint32_t qn = 16; VkQueueFamilyProperties qf[16]; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf);
    uint32_t qfi = 0; while (!(qf[qfi].queueFlags & VK_QUEUE_COMPUTE_BIT)) qfi++;
    float prio = 1;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qfi, .queueCount = 1, .pQueuePriorities = &prio };
    VkPhysicalDeviceFeatures feat = { .shaderInt64 = VK_TRUE };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci, .pEnabledFeatures = &feat };
    CK(vkCreateDevice(phys, &dci, NULL, &dev));
    VkQueue q; vkGetDeviceQueue(dev, qfi, 0, &q);

    buf_t b[NB];   /* image, glow source, QA, QB, glow texture G, control (lit, indirect sizes) */
    const VkDeviceSize sz[NB] = { W * H * 4, HW * HH * 4, QW * QH * 4, QW * QH * 4, HW * HH * 4, 128 };
    b[0] = mkbuf(sz[0], 1);
    for (int i = 1; i < NB; i++) b[i] = mkbuf(sz[i], 0);
    buf_t src = mkbuf((VkDeviceSize)W * H * 4, 1), fsrc = mkbuf((VkDeviceSize)HW * HH * 4, 1);
    uint32_t* io = b[0].map; uint32_t* in = src.map; uint32_t* fin = fsrc.map;
    for (int i = 0; i < W * H; i++)      /* A,R,G,B bytes -> texel R,G,B,A */
        in[i] = img[4 * i + 1] | img[4 * i + 2] << 8 | img[4 * i + 3] << 16 | (uint32_t)img[4 * i] << 24;
    for (int i = 0; i < HW * HH; i++)
        fin[i] = fld[4 * i + 1] | fld[4 * i + 2] << 8 | fld[4 * i + 3] << 16 | (uint32_t)fld[4 * i] << 24;

    VkDescriptorSetLayoutBinding lb[NB];
    for (int i = 0; i < NB; i++) lb[i] = (VkDescriptorSetLayoutBinding){ i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL };
    VkDescriptorSetLayoutCreateInfo dl = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = NB, .pBindings = lb };
    VkDescriptorSetLayout dsl; CK(vkCreateDescriptorSetLayout(dev, &dl, NULL, &dsl));
    VkPushConstantRange pr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 20 };
    VkPipelineLayoutCreateInfo pl = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pr };
    VkPipelineLayout layout; CK(vkCreatePipelineLayout(dev, &pl, NULL, &layout));
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = ssz, .pCode = spv };
    VkShaderModule mod; CK(vkCreateShaderModule(dev, &smi, NULL, &mod));
    VkComputePipelineCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = mod, .pName = "main" }, .layout = layout };
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    VkPipeline pipe; CK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, NULL, &pipe));
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "pipeline created in %.1f ms\n", (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NB };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
    VkDescriptorPool pool; CK(vkCreateDescriptorPool(dev, &dpi, NULL, &pool));
    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &dsl };
    VkDescriptorSet set; CK(vkAllocateDescriptorSets(dev, &dai, &set));
    VkDescriptorBufferInfo dbi[NB]; VkWriteDescriptorSet wr[NB];
    for (int i = 0; i < NB; i++) {
        dbi[i] = (VkDescriptorBufferInfo){ b[i].b, 0, VK_WHOLE_SIZE };
        wr[i] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = i, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi[i] };
    }
    vkUpdateDescriptorSets(dev, NB, wr, 0, NULL);
    VkQueryPoolCreateInfo qpi = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 64 };
    VkQueryPool qp; CK(vkCreateQueryPool(dev, &qpi, NULL, &qp));

    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = qfi };
    VkCommandPool cp; CK(vkCreateCommandPool(dev, &cpci, NULL, &cp));
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cp, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb; CK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CK(vkBeginCommandBuffer(cb, &bi));
    vkCmdResetQueryPool(cb, qp, 0, 64);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, 0);
    int nts = 0, stname[64];
    for (int r = 0; r < reps; r++) {
        { VkBufferCopy bc = { 0, 0, (VkDeviceSize)W * H * 4 }; vkCmdCopyBuffer(cb, src.b, b[0].b, 1, &bc); }   /* each run on the input */
        { VkBufferCopy bc = { 0, 0, (VkDeviceSize)HW * HH * 4 }; vkCmdCopyBuffer(cb, fsrc.b, b[1].b, 1, &bc); barrier(cb); }
#define TS(s) do { if (nts < 62 && r == reps - 1) { stname[nts] = s; vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 1 + nts); nts++; } } while (0)
#define PC(s, j) do { int _p[5] = { s, j, 0, 0, strength }; vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 20, _p); } while (0)
#define ST(s, j, gx) do { PC(s, j); vkCmdDispatch(cb, gx, 1, 1); barrier(cb); TS(s); } while (0)
#define STI(s, j, off) do { PC(s, j); vkCmdDispatchIndirect(cb, b[5].b, off); barrier(cb); TS(s); } while (0)
        vkCmdFillBuffer(cb, b[5].b, 0, 128, 0); barrier(cb);
        ST(0, 0, (QW * QH + 255) / 256);                           /* DOWN */
        ST(6, 0, 1);                                               /* ARM */
        for (int k = 0; k < 7; k++) { STI(1, 0, 4); STI(1, 1, 4); } /* BLUR across, down */
        STI(2, 0, 4);                                              /* MOD */
        for (int k = 0; k < 4; k++) { STI(1, 0, 4); STI(1, 1, 4); }
        STI(3, 0, 16);                                             /* UP */
        STI(4, 0, 28);                                             /* ADD */
        STI(7, 0, 56);                                             /* ALPHA, when ADD did not run */
    }
    for (int i = nts + 1; i < 64; i++) vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, i);
    CK(vkEndCommandBuffer(cb));
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    CK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE)); CK(vkQueueWaitIdle(q));
    uint64_t ts[64]; CK(vkGetQueryPoolResults(dev, qp, 0, 64, sizeof ts, ts, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    fprintf(stderr, "%s: %.3f ms per run\n", props.deviceName, (ts[63] - ts[0]) * props.limits.timestampPeriod / 1e6 / reps);
    if (getenv("GLOW_STAGES")) for (int i = 0; i < nts; i++)
        fprintf(stderr, "  stage %d: %.3f ms\n", stname[i], (ts[1 + i] - ts[i]) * props.limits.timestampPeriod / 1e6);
    for (int i = 0; i < W * H; i++) {
        const uint32_t v = io[i];
        img[4 * i] = v >> 24; img[4 * i + 1] = v; img[4 * i + 2] = v >> 8; img[4 * i + 3] = v >> 16;
    }
    f = fopen(argv[4], "wb"); fwrite(img, 4, W * H, f); fclose(f);
    return 0;
}
