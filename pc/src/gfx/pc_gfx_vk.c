/* pc_gfx_vk.c - the Vulkan backend of pc_gfx (Vulkan 1.0 or later).
 *
 * pc_gfx.c keeps OpenGL's state (g_gfx_state); a draw here turns the state
 * of the moment into a pipeline (built once for each combination, kept in
 * shader_cache/vulkan.bin's pipeline cache), and the data into the frame's
 * streaming buffer.
 *
 * - The GX shader is one SPIR-V module (pc/src/gen/pc_shaders.h) with the
 *   configuration as specialization constants: the uber pipeline draws a
 *   configuration until the one specialized for it is built on the worker
 *   thread, so a new configuration never stalls a frame. The configurations
 *   seen are kept in shader_cache/vulkan_configs.bin and built at start.
 * - The three uniform blocks are written to the frame's buffer when they
 *   change and bound with dynamic offsets; the textures of a draw are one
 *   descriptor set, reused while they stay the same.
 * - Clip space: the shaders put OpenGL's depth into [0, w] and flip y, the
 *   viewport and scissor are turned from OpenGL's bottom-left origin, and
 *   front faces wind clockwise, so the pictures are OpenGL's.
 * - Textures take uploads through a staging buffer before the frame's
 *   draws; an image a frame has drawn with gets a new image when it is
 *   uploaded again (as a GL driver renames it). glReadPixels copies the
 *   picture back after waiting for the GPU (screenshots, scene copies). */
#ifdef PC_GFX_VULKAN_BUILT

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"
#include <SDL_vulkan.h>

#include "pc_gfx_internal.h"
#include "pc_settings.h"
#include "../gen/pc_shaders.h"

/* (structures are started as { VK_STRUCTURE_TYPE_... }, the rest zero) */
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

/* --- functions, loaded through SDL --- */

#define VK_GLOBAL(X) X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties)
#define VK_INSTANCE(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFormatProperties) X(vkCreateDevice) \
    X(vkGetDeviceProcAddr) X(vkEnumerateDeviceExtensionProperties) X(vkDestroySurfaceKHR) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR)
#define VK_DEVICE(X) \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkQueueWaitIdle) \
    X(vkQueuePresentKHR) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) \
    X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateBuffer) X(vkDestroyBuffer) \
    X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkAllocateMemory) X(vkFreeMemory) \
    X(vkMapMemory) X(vkUnmapMemory) X(vkCreateSampler) X(vkDestroySampler) X(vkCreateRenderPass) \
    X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) X(vkResetDescriptorPool) X(vkAllocateDescriptorSets) \
    X(vkUpdateDescriptorSets) X(vkFreeDescriptorSets) X(vkCreatePipelineCache) X(vkDestroyPipelineCache) X(vkGetPipelineCacheData) \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateCommandPool) X(vkDestroyCommandPool) \
    X(vkResetCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
    X(vkResetCommandBuffer) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) \
    X(vkCmdBindPipeline) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdSetLineWidth) \
    X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdDraw) \
    X(vkCmdDrawIndexed) X(vkCmdPushConstants) X(vkCmdClearAttachments) X(vkCmdPipelineBarrier) \
    X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdBlitImage)

#define VK_DECLARE(name) static PFN_##name name;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_;
VK_GLOBAL(VK_DECLARE)
VK_INSTANCE(VK_DECLARE)
VK_DEVICE(VK_DECLARE)

#define FRAMES 2                  /* frames in flight */
#define STREAM_SIZE (8u << 20)    /* a frame's vertices, indices and uniforms */
#define STAGING_SIZE (32u << 20)  /* a frame's texture uploads */
#define BLOCK_SIZE (64u << 20)    /* device memory taken at once for images */
#define PIPELINES 4096            /* a power of two */
#define CACHE_DIR "shader_cache"
#define CACHE_FILE CACHE_DIR "/vulkan.bin"
#define CONFIGS_FILE CACHE_DIR "/vulkan_configs.bin"

#define VK_CHECK(call, what) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { vk_fail(what, r_); } } while (0)

/* --- device memory: blocks shared by many images --- */

typedef struct { VkDeviceSize off, size; } Range;

typedef struct {
    VkDeviceMemory mem;
    VkDeviceSize size;
    uint32_t type;
    Range* free;
    int nfree, capfree;
} MemBlock;

typedef struct { int block; VkDeviceSize off, size; VkDeviceMemory dedicated; } Alloc;

/* --- textures --- */

typedef struct {
    VkImage image;
    VkImageView view;
    Alloc mem;
    VkFormat format;
    int w, h, levels;
    uint64_t uid;         /* of this image: no other image ever has it (the descriptor set cache's key) */
} VkTex;

/* --- buffers that the CPU writes --- */

typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    unsigned char* map;
    VkDeviceSize size, used;
    VkDescriptorSet ubo_set; /* the GX blocks in this buffer (dynamic offsets) */
} HostBuf;

/* --- what the GPU may still use: freed when its frame is done --- */

typedef struct {
    uint64_t serial;
    VkImage image;
    VkImageView view;
    Alloc mem;
    VkBuffer buf;
    VkDeviceMemory buf_mem;
} Garbage;

typedef struct {
    VkCommandPool pool, upload_pool;
    VkCommandBuffer cmd, upload;
    VkFence fence;
    VkSemaphore acquired, rendered;
    VkDescriptorPool desc_pool;
    HostBuf stream, staging;
    HostBuf extra[8]; /* more streaming space, made when a frame needs it and kept */
    int nextra;
    int cur;          /* the streaming buffer in use: 0 stream, i extra[i - 1] */
    uint64_t serial;
    int begun, upload_begun, acquired_image, acquire_waited;
    int desc_pool_used;
} Frame;

/* --- pipelines --- */

enum { KIND_UBER = 0, KIND_SPEC = 1, KIND_UI = 2 };

typedef struct {
    int kind;
    uint32_t rkey;
    PCGXShaderConfig config;
    uint32_t hash;
    VkPipeline pipe;
    SDL_atomic_t state; /* 0 empty, 1 queued, 2 ready, 3 failed */
} Pipe;

/* --- descriptor sets of textures, kept while they are used --- */

#define TEXSETS 4096 /* a power of two */

typedef struct {
    int n;                /* units: 5 (GX), 1 (UI); 0 = empty */
    uint64_t uid[5];
    VkSampler smp[5];
    VkDescriptorSet set;
    uint64_t used;        /* the last frame that drew with it */
} TexSet;

/* --- the backend --- */

static struct {
    SDL_Window* window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice phys;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties memprops;
    VkDevice dev;
    uint32_t qfamily;
    VkQueue queue;
    int wide_lines, bc;

    VkSwapchainKHR swapchain;
    VkFormat color_format;
    VkExtent2D extent;
    uint32_t nimages;
    VkImage images[8];
    VkImageView views[8];
    VkFramebuffer fbs[8];
    VkImageLayout layout[8];
    VkFormat depth_format;
    VkImage depth, msaa;
    VkImageView depth_view, msaa_view;
    Alloc depth_mem, msaa_mem;
    VkSampleCountFlagBits samples;
    int want_samples;
    int vsync;
    int swapchain_dirty, suspended, want_suspend;
    VkRenderPass passes[3][3]; /* [color load op][depth load op]: 0 load, 1 clear, 2 don't care */

    Frame frames[FRAMES];
    int fi;               /* the frame being recorded */
    uint64_t serial;      /* of the frame being recorded */
    uint64_t done_serial; /* frames up to this one are finished */
    uint32_t image;       /* the acquired swapchain image */
    int in_pass;
    int drew;             /* the image has content this frame (passes after the first load) */

    MemBlock* blocks;
    int nblocks;
    uint32_t image_mem_type;
    Garbage* garbage;
    int ngarbage, capgarbage;

    VkSampler samplers[3][3][3][2];
    VkDescriptorSetLayout ubo_layout, tex_layout, ui_layout;
    VkPipelineLayout gx_layout, ui_pipe_layout;
    VkShaderModule gx_vs, gx_fs, ui_vs, ui_fs;
    VkPipelineCache cache;
    VkBuffer quad_buf;
    VkDeviceMemory quad_mem;
    PCTex dummy, white;
    VkBuffer readback;
    VkDeviceMemory readback_mem;
    VkDeviceSize readback_size;
    void* readback_map;

    Pipe* pipes;
    SDL_Thread* worker;
    SDL_mutex* lock;
    SDL_cond* wake;
    int* queue_jobs;
    int njobs, capjobs, quit;
    int pipes_ready, pipes_built;
    FILE* configs_out;

    /* what the command buffer has bound */
    VkPipeline bound_pipe;
    VkPipelineLayout bound_layout;
    VkViewport bound_vp;
    VkRect2D bound_scissor;
    float bound_line;
    VkDescriptorSet bound_ubo_set, bound_tex_set;
    uint32_t bound_offsets[3];
    uint64_t last_uid[5];
    VkSampler last_smp[5];
    VkDescriptorSet last_tex_set;
    TexSet* texsets;
    int ntexsets;
    VkDescriptorPool set_pool; /* the cached sets and the frames' block sets */
    uint64_t next_uid;
    int rebind;           /* a flush ended the pass: the draw starts over */
    HostBuf* blk_buf[3];  /* where each block was written this frame */
    VkDeviceSize blk_off[3];
    float clear_color[4];
    float clear_depth;
    int pending_clear;    /* 1 color, 2 depth */
    int lost;
} vk;

static PCGfxBackend s_backend;

static void vk_fail(const char* what, VkResult r);

/* --- memory --- */

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < vk.memprops.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (vk.memprops.memoryTypes[i].propertyFlags & want) == want) return i;
    }
    return UINT32_MAX;
}

static void range_insert(MemBlock* b, VkDeviceSize off, VkDeviceSize size) {
    int i = 0;

    while (i < b->nfree && b->free[i].off < off) i++;
    /* merge with the ranges before and after */
    if (i > 0 && b->free[i - 1].off + b->free[i - 1].size == off) {
        b->free[i - 1].size += size;
        if (i < b->nfree && off + size == b->free[i].off) {
            b->free[i - 1].size += b->free[i].size;
            memmove(&b->free[i], &b->free[i + 1], (size_t)(b->nfree - i - 1) * sizeof(Range));
            b->nfree--;
        }
        return;
    }
    if (i < b->nfree && off + size == b->free[i].off) {
        b->free[i].off = off;
        b->free[i].size += size;
        return;
    }
    if (b->nfree == b->capfree) {
        b->capfree = b->capfree ? b->capfree * 2 : 16;
        b->free = (Range*)realloc(b->free, (size_t)b->capfree * sizeof(Range));
    }
    memmove(&b->free[i + 1], &b->free[i], (size_t)(b->nfree - i) * sizeof(Range));
    b->free[i].off = off;
    b->free[i].size = size;
    b->nfree++;
}

static int block_alloc(MemBlock* b, VkDeviceSize size, VkDeviceSize align, VkDeviceSize* out) {
    for (int i = 0; i < b->nfree; i++) {
        VkDeviceSize start = (b->free[i].off + align - 1) / align * align;
        VkDeviceSize end = b->free[i].off + b->free[i].size;

        if (start + size <= end) {
            Range r = b->free[i];

            memmove(&b->free[i], &b->free[i + 1], (size_t)(b->nfree - i - 1) * sizeof(Range));
            b->nfree--;
            if (start > r.off) range_insert(b, r.off, start - r.off);
            if (start + size < end) range_insert(b, start + size, end - start - size);
            *out = start;
            return 1;
        }
    }
    return 0;
}

static int image_memory(VkImage image, Alloc* a) {
    VkMemoryRequirements req;
    uint32_t type;

    vkGetImageMemoryRequirements(vk.dev, image, &req);
    type = mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) type = mem_type(req.memoryTypeBits, 0);
    memset(a, 0, sizeof(*a));
    if (req.size > BLOCK_SIZE / 4) { /* big: its own */
        VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, type };

        if (vkAllocateMemory(vk.dev, &ai, NULL, &a->dedicated) != VK_SUCCESS) return 0;
        a->block = -1;
        a->size = req.size;
        return vkBindImageMemory(vk.dev, image, a->dedicated, 0) == VK_SUCCESS;
    }
    for (int i = 0; i <= vk.nblocks; i++) {
        MemBlock* b;

        if (i == vk.nblocks) {
            VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, BLOCK_SIZE, type };
            VkDeviceMemory mem;

            if (vkAllocateMemory(vk.dev, &ai, NULL, &mem) != VK_SUCCESS) return 0;
            vk.blocks = (MemBlock*)realloc(vk.blocks, (size_t)(vk.nblocks + 1) * sizeof(MemBlock));
            b = &vk.blocks[vk.nblocks++];
            memset(b, 0, sizeof(*b));
            b->mem = mem;
            b->size = BLOCK_SIZE;
            b->type = type;
            range_insert(b, 0, BLOCK_SIZE);
        }
        b = &vk.blocks[i];
        if (b->type == type && block_alloc(b, req.size, req.alignment, &a->off)) {
            a->block = i;
            a->size = req.size;
            return vkBindImageMemory(vk.dev, image, b->mem, a->off) == VK_SUCCESS;
        }
    }
    return 0;
}

static void alloc_free(Alloc* a) {
    if (a->dedicated != VK_NULL_HANDLE) {
        vkFreeMemory(vk.dev, a->dedicated, NULL);
    } else if (a->block >= 0 && a->size > 0) {
        range_insert(&vk.blocks[a->block], a->off, a->size);
    }
    memset(a, 0, sizeof(*a));
}

static int host_buffer(HostBuf* hb, VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    void* map = NULL;

    memset(hb, 0, sizeof(*hb));
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk.dev, &bi, NULL, &hb->buf) != VK_SUCCESS) return 0;
    vkGetBufferMemoryRequirements(vk.dev, hb->buf, &req);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(vk.dev, &ai, NULL, &hb->mem) != VK_SUCCESS ||
        vkBindBufferMemory(vk.dev, hb->buf, hb->mem, 0) != VK_SUCCESS ||
        vkMapMemory(vk.dev, hb->mem, 0, VK_WHOLE_SIZE, 0, &map) != VK_SUCCESS) {
        return 0;
    }
    hb->map = (unsigned char*)map;
    hb->size = size;
    return 1;
}

static void host_buffer_free(HostBuf* hb) {
    if (hb->buf != VK_NULL_HANDLE) vkDestroyBuffer(vk.dev, hb->buf, NULL);
    if (hb->mem != VK_NULL_HANDLE) vkFreeMemory(vk.dev, hb->mem, NULL);
    memset(hb, 0, sizeof(*hb));
}

static void garbage_add(Garbage g) {
    if (vk.ngarbage == vk.capgarbage) {
        vk.capgarbage = vk.capgarbage ? vk.capgarbage * 2 : 64;
        vk.garbage = (Garbage*)realloc(vk.garbage, (size_t)vk.capgarbage * sizeof(Garbage));
    }
    g.serial = vk.serial;
    vk.garbage[vk.ngarbage++] = g;
}

static void garbage_collect(int all) {
    int j = 0;

    for (int i = 0; i < vk.ngarbage; i++) {
        Garbage* g = &vk.garbage[i];

        if (all || g->serial <= vk.done_serial) {
            if (g->view != VK_NULL_HANDLE) vkDestroyImageView(vk.dev, g->view, NULL);
            if (g->image != VK_NULL_HANDLE) vkDestroyImage(vk.dev, g->image, NULL);
            alloc_free(&g->mem);
            if (g->buf != VK_NULL_HANDLE) vkDestroyBuffer(vk.dev, g->buf, NULL);
            if (g->buf_mem != VK_NULL_HANDLE) vkFreeMemory(vk.dev, g->buf_mem, NULL);
        } else {
            vk.garbage[j++] = *g;
        }
    }
    vk.ngarbage = j;
}

/* --- frames and command buffers --- */

static void cmd_begin(VkCommandBuffer cb) {
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer");
}

static void bound_reset(void) {
    vk.bound_pipe = VK_NULL_HANDLE;
    vk.bound_layout = VK_NULL_HANDLE;
    vk.bound_ubo_set = vk.bound_tex_set = VK_NULL_HANDLE;
    memset(&vk.bound_vp, 0, sizeof(vk.bound_vp));
    memset(&vk.bound_scissor, 0, sizeof(vk.bound_scissor));
    vk.bound_line = -1.0f;
}

/* the frame's resources, once its previous use is done */
static void frame_begin(void) {
    Frame* f = &vk.frames[vk.fi];

    if (f->begun) return;
    VK_CHECK(vkWaitForFences(vk.dev, 1, &f->fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    if (f->serial > vk.done_serial) vk.done_serial = f->serial;
    garbage_collect(0);
    vkResetCommandPool(vk.dev, f->pool, 0);
    vkResetCommandPool(vk.dev, f->upload_pool, 0);
    if (f->desc_pool_used) vkResetDescriptorPool(vk.dev, f->desc_pool, 0);
    f->desc_pool_used = 0;
    f->stream.used = 0;
    for (int i = 0; i < f->nextra; i++) f->extra[i].used = 0;
    f->cur = 0;
    f->staging.used = 0;
    f->serial = ++vk.serial;
    f->begun = 1;
    f->upload_begun = 0;
    f->acquired_image = 0;
    f->acquire_waited = 0;
    cmd_begin(f->cmd);
    vk.in_pass = 0;
    vk.drew = 0;
    vk.pending_clear = 0;
    vk.blk_buf[0] = vk.blk_buf[1] = vk.blk_buf[2] = NULL;
    vk.last_tex_set = VK_NULL_HANDLE;
    bound_reset();
}

static VkCommandBuffer upload_cmd(void) {
    Frame* f;

    frame_begin();
    f = &vk.frames[vk.fi];
    if (!f->upload_begun) {
        cmd_begin(f->upload);
        f->upload_begun = 1;
    }
    return f->upload;
}

static void pass_end(void);

/* submits the frame's command buffers so far and waits for them (glReadPixels,
 * a staging buffer that is full); recording goes on in new ones */
static void flush_and_wait(void) {
    Frame* f = &vk.frames[vk.fi];
    VkCommandBuffer cbs[2];
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    uint32_t n = 0;

    if (!f->begun) return;
    pass_end();
    vk.rebind = 1; /* (a draw being set up binds again in a new pass) */
    if (f->upload_begun) {
        VK_CHECK(vkEndCommandBuffer(f->upload), "vkEndCommandBuffer");
        cbs[n++] = f->upload;
    }
    VK_CHECK(vkEndCommandBuffer(f->cmd), "vkEndCommandBuffer");
    cbs[n++] = f->cmd;
    si.commandBufferCount = n;
    si.pCommandBuffers = cbs;
    if (f->acquired_image && !f->acquire_waited) {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &f->acquired;
        si.pWaitDstStageMask = &wait_stage;
        f->acquire_waited = 1;
    }
    VK_CHECK(vkQueueSubmit(vk.queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit");
    VK_CHECK(vkQueueWaitIdle(vk.queue), "vkQueueWaitIdle");
    vkResetCommandBuffer(f->cmd, 0);
    vkResetCommandBuffer(f->upload, 0);
    cmd_begin(f->cmd);
    f->upload_begun = 0;
    f->staging.used = 0;
    bound_reset();
}

static HostBuf* frame_buf(Frame* f, int i) {
    return i == 0 ? &f->stream : &f->extra[i - 1];
}

static void ubo_set_make(HostBuf* hb);

/* n bytes of the frame's streaming buffers */
static unsigned char* stream_alloc(VkDeviceSize n, VkDeviceSize align, HostBuf** hb, VkDeviceSize* off) {
    Frame* f = &vk.frames[vk.fi];
    HostBuf* b = frame_buf(f, f->cur);
    VkDeviceSize start = (b->used + align - 1) / align * align;

    while (start + n > b->size) {
        if (f->cur < f->nextra) {
            b = frame_buf(f, ++f->cur); /* the next one this frame has */
        } else if (f->nextra < 8) {
            HostBuf* e = &f->extra[f->nextra];

            if (!host_buffer(e, n > STREAM_SIZE ? n * 2 : STREAM_SIZE,
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
                vk_fail("streaming buffer", VK_ERROR_OUT_OF_DEVICE_MEMORY);
                return NULL;
            }
            ubo_set_make(e);
            f->nextra++;
            b = frame_buf(f, ++f->cur);
        } else {
            /* all full: wait for the GPU and use them again */
            flush_and_wait();
            f->stream.used = 0;
            for (int i = 0; i < f->nextra; i++) f->extra[i].used = 0;
            f->cur = 0;
            b = &f->stream;
            vk.blk_buf[0] = vk.blk_buf[1] = vk.blk_buf[2] = NULL;
            if (n > b->size) return NULL;
        }
        start = 0;
    }
    b->used = start + n;
    *hb = b;
    *off = start;
    return b->map + start;
}

/* --- swapchain and render targets --- */

static VkRenderPass render_pass(int color_op, int depth_op);

static int make_image(VkFormat format, uint32_t w, uint32_t h, uint32_t levels, VkSampleCountFlagBits samples,
                      VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage* image, VkImageView* view,
                      Alloc* mem) {
    VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent.width = w;
    ii.extent.height = h;
    ii.extent.depth = 1;
    ii.mipLevels = levels;
    ii.arrayLayers = 1;
    ii.samples = samples;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk.dev, &ii, NULL, image) != VK_SUCCESS) return 0;
    if (!image_memory(*image, mem)) {
        vkDestroyImage(vk.dev, *image, NULL);
        *image = VK_NULL_HANDLE;
        return 0;
    }
    vi.image = *image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange.aspectMask = aspect;
    vi.subresourceRange.levelCount = levels;
    vi.subresourceRange.layerCount = 1;
    return vkCreateImageView(vk.dev, &vi, NULL, view) == VK_SUCCESS;
}

static void swapchain_destroy(int keep_swapchain) {
    for (uint32_t i = 0; i < vk.nimages; i++) {
        if (vk.fbs[i]) vkDestroyFramebuffer(vk.dev, vk.fbs[i], NULL);
        if (vk.views[i]) vkDestroyImageView(vk.dev, vk.views[i], NULL);
        vk.fbs[i] = VK_NULL_HANDLE;
        vk.views[i] = VK_NULL_HANDLE;
    }
    vk.nimages = 0;
    if (vk.depth_view) vkDestroyImageView(vk.dev, vk.depth_view, NULL);
    if (vk.depth) vkDestroyImage(vk.dev, vk.depth, NULL);
    alloc_free(&vk.depth_mem);
    if (vk.msaa_view) vkDestroyImageView(vk.dev, vk.msaa_view, NULL);
    if (vk.msaa) vkDestroyImage(vk.dev, vk.msaa, NULL);
    alloc_free(&vk.msaa_mem);
    vk.depth = vk.msaa = VK_NULL_HANDLE;
    vk.depth_view = vk.msaa_view = VK_NULL_HANDLE;
    if (!keep_swapchain && vk.swapchain) {
        vkDestroySwapchainKHR(vk.dev, vk.swapchain, NULL);
        vk.swapchain = VK_NULL_HANDLE;
    }
}

static int swapchain_create(void) {
    VkSurfaceCapabilitiesKHR caps;
    VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    VkPresentModeKHR modes[16];
    uint32_t nmodes = 16, count = 0;
    VkSwapchainKHR old = vk.swapchain;
    int w = 0, h = 0;

    if (vk.surface == VK_NULL_HANDLE) return 0;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.phys, vk.surface, &caps) != VK_SUCCESS) return 0;
    SDL_Vulkan_GetDrawableSize(vk.window, &w, &h);
    if (caps.currentExtent.width != UINT32_MAX) {
        vk.extent = caps.currentExtent;
    } else {
        vk.extent.width = (uint32_t)(w > 0 ? w : 1);
        vk.extent.height = (uint32_t)(h > 0 ? h : 1);
    }
    if (vk.extent.width == 0 || vk.extent.height == 0) return 0; /* minimized */
    ci.surface = vk.surface;
    ci.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && ci.minImageCount > caps.maxImageCount) ci.minImageCount = caps.maxImageCount;
    if (ci.minImageCount > 8) ci.minImageCount = 8;
    ci.imageFormat = vk.color_format;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = vk.extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    /* the system turns the picture when it has to (no pre-rotation) */
    ci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                            : (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
                                  ? VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR : VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vk.vsync && vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys, vk.surface, &nmodes, modes) >= 0) {
        for (uint32_t i = 0; i < nmodes; i++) {
            if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) ci.presentMode = modes[i];
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR && ci.presentMode == VK_PRESENT_MODE_FIFO_KHR) ci.presentMode = modes[i];
        }
    }
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    if (vkCreateSwapchainKHR(vk.dev, &ci, NULL, &vk.swapchain) != VK_SUCCESS) {
        vk.swapchain = old;
        return 0;
    }
    if (old != VK_NULL_HANDLE) vkDestroySwapchainKHR(vk.dev, old, NULL);
    count = 8;
    vkGetSwapchainImagesKHR(vk.dev, vk.swapchain, &count, vk.images);
    vk.nimages = count;
    if (vk.samples != VK_SAMPLE_COUNT_1_BIT &&
        !make_image(vk.color_format, vk.extent.width, vk.extent.height, 1, vk.samples,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, &vk.msaa, &vk.msaa_view, &vk.msaa_mem)) {
        return 0;
    }
    if (!make_image(vk.depth_format, vk.extent.width, vk.extent.height, 1, vk.samples,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, &vk.depth, &vk.depth_view,
                    &vk.depth_mem)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        VkImageView att[3];

        vi.image = vk.images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = vk.color_format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(vk.dev, &vi, NULL, &vk.views[i]) != VK_SUCCESS) return 0;
        if (vk.samples != VK_SAMPLE_COUNT_1_BIT) {
            att[0] = vk.msaa_view;
            att[1] = vk.depth_view;
            att[2] = vk.views[i];
        } else {
            att[0] = vk.views[i];
            att[1] = vk.depth_view;
        }
        fi.renderPass = render_pass(0, 0);
        fi.attachmentCount = vk.samples != VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
        fi.pAttachments = att;
        fi.width = vk.extent.width;
        fi.height = vk.extent.height;
        fi.layers = 1;
        if (vkCreateFramebuffer(vk.dev, &fi, NULL, &vk.fbs[i]) != VK_SUCCESS) return 0;
        vk.layout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    vk.swapchain_dirty = 0;
    return 1;
}

static void swapchain_recreate(void) {
    vkDeviceWaitIdle(vk.dev);
    swapchain_destroy(1);
    if (!swapchain_create()) {
        vk.swapchain_dirty = 1;
    }
}

static VkAttachmentLoadOp load_op(int op) {
    return op == 1 ? VK_ATTACHMENT_LOAD_OP_CLEAR : op == 2 ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD;
}

/* color op and depth op: 0 load, 1 clear, 2 don't care; all compatible */
static VkRenderPass render_pass(int color_op, int depth_op) {
    VkRenderPass* rp = &vk.passes[color_op][depth_op];
    VkAttachmentDescription a[3];
    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkAttachmentReference rref = { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { 0 };
    VkSubpassDependency dep = { 0 };
    VkRenderPassCreateInfo ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    int msaa = vk.samples != VK_SAMPLE_COUNT_1_BIT;

    if (*rp != VK_NULL_HANDLE) return *rp;
    memset(a, 0, sizeof(a));
    a[0].format = vk.color_format;
    a[0].samples = vk.samples;
    a[0].loadOp = load_op(color_op);
    a[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[0].initialLayout = color_op == 0 ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    a[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a[1] = a[0];
    a[1].format = vk.depth_format;
    a[1].loadOp = load_op(depth_op);
    a[1].initialLayout = depth_op == 0 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    a[2] = a[0];
    a[2].samples = VK_SAMPLE_COUNT_1_BIT;
    a[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &cref;
    sub.pDepthStencilAttachment = &dref;
    if (msaa) sub.pResolveAttachments = &rref;
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_TRANSFER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    ci.attachmentCount = msaa ? 3 : 2;
    ci.pAttachments = a;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(vk.dev, &ci, NULL, rp), "vkCreateRenderPass");
    return *rp;
}

static void image_barrier(VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to,
                          VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage, uint32_t base_level, uint32_t levels) {
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = base_level;
    b.subresourceRange.levelCount = levels;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

/* the surface goes with the window Android takes away in the background */
static void suspend(void) {
    vkDeviceWaitIdle(vk.dev);
    swapchain_destroy(0);
    if (vk.surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
    vk.surface = VK_NULL_HANDLE;
    vk.suspended = 1;
    vk.want_suspend = 0;
}

/* the swapchain image to draw this frame into; 0 when there is none now */
static int acquire(void) {
    Frame* f = &vk.frames[vk.fi];

    frame_begin();
    if (f->acquired_image) return 1;
    if (vk.want_suspend) suspend();
    if (vk.suspended) return 0;
    if (vk.surface == VK_NULL_HANDLE) {
        if (!SDL_Vulkan_CreateSurface(vk.window, vk.instance, &vk.surface)) return 0;
        vk.swapchain_dirty = 1;
    }
    for (int tries = 0; tries < 3; tries++) {
        VkResult r;

        if (vk.swapchain_dirty || vk.swapchain == VK_NULL_HANDLE) swapchain_recreate();
        if (vk.swapchain == VK_NULL_HANDLE || vk.swapchain_dirty) return 0;
        r = vkAcquireNextImageKHR(vk.dev, vk.swapchain, UINT64_MAX, f->acquired, VK_NULL_HANDLE, &vk.image);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            f->acquired_image = 1;
            if (r == VK_SUBOPTIMAL_KHR) vk.swapchain_dirty = 1; /* after this frame */
            return 1;
        }
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            vk.swapchain_dirty = 1;
            continue;
        }
        if (r == VK_ERROR_SURFACE_LOST_KHR) {
            vkDeviceWaitIdle(vk.dev);
            swapchain_destroy(0);
            vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
            vk.surface = VK_NULL_HANDLE;
            if (!SDL_Vulkan_CreateSurface(vk.window, vk.instance, &vk.surface)) return 0;
            vk.swapchain_dirty = 1;
            continue;
        }
        vk_fail("vkAcquireNextImageKHR", r);
    }
    return 0;
}

static void pass_end(void) {
    if (vk.in_pass) {
        vkCmdEndRenderPass(vk.frames[vk.fi].cmd);
        vk.in_pass = 0;
        vk.layout[vk.image] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
}

/* in a render pass on the frame's image; 0 when there is no image */
static int pass_begin(void) {
    Frame* f = &vk.frames[vk.fi];
    VkRenderPassBeginInfo bi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    VkClearValue cv[3];
    int color_op, depth_op;

    if (vk.in_pass) return 1;
    if (!acquire()) return 0;
    if (!vk.drew) {
        /* the frame's first pass: cleared (OpenGL leaves the picture after a swap undefined) */
        color_op = 1;
        depth_op = 1;
        if (!(vk.pending_clear & 1)) {
            vk.clear_color[0] = vk.clear_color[1] = vk.clear_color[2] = 0.0f;
            vk.clear_color[3] = 1.0f;
        }
        if (!(vk.pending_clear & 2)) vk.clear_depth = 1.0f;
    } else {
        color_op = (vk.pending_clear & 1) ? 1 : 0;
        depth_op = (vk.pending_clear & 2) ? 1 : 0;
        if (color_op == 0 && vk.layout[vk.image] != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
            vk.samples == VK_SAMPLE_COUNT_1_BIT) {
            image_barrier(f->cmd, vk.images[vk.image], vk.layout[vk.image], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 1);
        }
    }
    memset(cv, 0, sizeof(cv));
    memcpy(cv[0].color.float32, vk.clear_color, sizeof(vk.clear_color));
    cv[1].depthStencil.depth = vk.clear_depth;
    bi.renderPass = render_pass(color_op, depth_op);
    bi.framebuffer = vk.fbs[vk.image];
    bi.renderArea.extent = vk.extent;
    bi.clearValueCount = vk.samples != VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    bi.pClearValues = cv;
    vkCmdBeginRenderPass(f->cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    vk.in_pass = 1;
    vk.drew = 1;
    vk.pending_clear = 0;
    bound_reset();
    return 1;
}

static void vk_present(void) {
    Frame* f = &vk.frames[vk.fi];
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkCommandBuffer cbs[2];
    uint32_t n = 0;
    int show;
    VkResult r;

    frame_begin();
    show = pass_begin(); /* (a frame that drew nothing shows black) */
    pass_end();
    if (show) {
        image_barrier(f->cmd, vk.images[vk.image], vk.layout[vk.image], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, 0,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 1);
        vk.layout[vk.image] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    if (f->upload_begun) {
        VK_CHECK(vkEndCommandBuffer(f->upload), "vkEndCommandBuffer");
        cbs[n++] = f->upload;
    }
    VK_CHECK(vkEndCommandBuffer(f->cmd), "vkEndCommandBuffer");
    cbs[n++] = f->cmd;
    si.commandBufferCount = n;
    si.pCommandBuffers = cbs;
    if (show && !f->acquire_waited) {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &f->acquired;
        si.pWaitDstStageMask = &wait_stage;
    }
    if (show) {
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &f->rendered;
    }
    VK_CHECK(vkResetFences(vk.dev, 1, &f->fence), "vkResetFences");
    VK_CHECK(vkQueueSubmit(vk.queue, 1, &si, f->fence), "vkQueueSubmit");
    if (show) {
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &f->rendered;
        pi.swapchainCount = 1;
        pi.pSwapchains = &vk.swapchain;
        pi.pImageIndices = &vk.image;
        r = vkQueuePresentKHR(vk.queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
            vk.swapchain_dirty = 1;
        } else if (r == VK_ERROR_SURFACE_LOST_KHR) {
            vk.swapchain_dirty = 1;
        } else if (r != VK_SUCCESS) {
            vk_fail("vkQueuePresentKHR", r);
        }
    }
    f->begun = 0;
    vk.fi = (vk.fi + 1) % FRAMES;
    if (vk.want_suspend) suspend();
    {
        int w = 0, h = 0;

        SDL_Vulkan_GetDrawableSize(vk.window, &w, &h);
        if ((uint32_t)w != vk.extent.width || (uint32_t)h != vk.extent.height) vk.swapchain_dirty = 1;
    }
}

/* --- textures --- */

static VkFormat tex_format(int format) {
    switch (format) {
        case PC_TEXF_BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case PC_TEXF_BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
        case PC_TEXF_BC7: return VK_FORMAT_BC7_UNORM_BLOCK;
        default:          return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

static void tex_release(VkTex* t) {
    Garbage g;

    memset(&g, 0, sizeof(g));
    g.image = t->image;
    g.view = t->view;
    g.mem = t->mem;
    garbage_add(g);
    t->image = VK_NULL_HANDLE;
    t->view = VK_NULL_HANDLE;
    memset(&t->mem, 0, sizeof(t->mem));
}

/* staging space for an upload: the frame's, or a buffer of its own */
static unsigned char* staging_alloc(VkDeviceSize n, VkBuffer* buf, VkDeviceSize* off) {
    Frame* f;

    frame_begin();
    f = &vk.frames[vk.fi];
    if (n > STAGING_SIZE / 2) {
        HostBuf hb;

        if (!host_buffer(&hb, n, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return NULL;
        garbage_add((Garbage){ 0, 0, 0, { 0 }, hb.buf, hb.mem }); /* freed after this frame */
        *buf = hb.buf;
        *off = 0;
        return hb.map;
    }
    if (f->staging.used + n > f->staging.size) {
        flush_and_wait(); /* the uploads so far are done */
    }
    *off = (f->staging.used + 15) & ~(VkDeviceSize)15;
    f->staging.used = *off + n;
    *buf = f->staging.buf;
    return f->staging.map + *off;
}

static int vk_tex_image(PCGfxTex* o, int w, int h, int format, const void* data, size_t size) {
    VkTex* t = (VkTex*)o->impl;
    VkFormat vf = tex_format(format);
    VkCommandBuffer cb;
    VkBuffer src;
    VkDeviceSize off;
    VkBufferImageCopy region;
    unsigned char* dst;
    size_t bytes = format == PC_TEXF_RGBA8 || format == PC_TEXF_RGB565 ? (size_t)w * h * 4 : size;

    if (w <= 0 || h <= 0) return 0;
    if ((format == PC_TEXF_BC1 || format == PC_TEXF_BC3 || format == PC_TEXF_BC7) && !vk.bc) return 0;
    if (t == NULL) {
        t = (VkTex*)calloc(1, sizeof(VkTex));
        if (t == NULL) return 0;
        o->impl = t;
    }
    /* a new image (the old one may be in this frame's draws, or another size) */
    if (t->image != VK_NULL_HANDLE) tex_release(t);
    if (!make_image(vf, (uint32_t)w, (uint32_t)h, 1, VK_SAMPLE_COUNT_1_BIT,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, &t->image, &t->view, &t->mem)) {
        return 0;
    }
    t->format = vf;
    t->w = w;
    t->h = h;
    t->levels = 1;
    t->uid = ++vk.next_uid;
    o->w = w;
    o->h = h;
    o->format = format;
    o->levels = 1;
    dst = staging_alloc(bytes, &src, &off);
    if (dst == NULL) return 0;
    if (format == PC_TEXF_RGB565) {
        pc_gfx_rgb565_to_rgba((const unsigned short*)data, dst, w * h);
    } else if (data != NULL) {
        memcpy(dst, data, bytes);
    } else {
        memset(dst, 0, bytes);
    }
    cb = upload_cmd();
    image_barrier(cb, t->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1);
    memset(&region, 0, sizeof(region));
    region.bufferOffset = off;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)w;
    region.imageExtent.height = (uint32_t)h;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cb, src, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    image_barrier(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1);
    return 1;
}

static void vk_tex_subimage(PCGfxTex* o, int x, int y, int w, int h, int row_length, const void* rgba) {
    VkTex* t = (VkTex*)o->impl;
    VkCommandBuffer cb;
    VkBuffer src;
    VkDeviceSize off;
    VkBufferImageCopy region;
    unsigned char* dst;

    if (t == NULL || t->image == VK_NULL_HANDLE || t->format != VK_FORMAT_R8G8B8A8_UNORM || w <= 0 || h <= 0) return;
    dst = staging_alloc((VkDeviceSize)w * h * 4, &src, &off);
    if (dst == NULL) return;
    for (int r = 0; r < h; r++) {
        memcpy(dst + (size_t)r * w * 4, (const unsigned char*)rgba + (size_t)r * row_length * 4, (size_t)w * 4);
    }
    cb = upload_cmd();
    image_barrier(cb, t->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1);
    memset(&region, 0, sizeof(region));
    region.bufferOffset = off;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = x;
    region.imageOffset.y = y;
    region.imageExtent.width = (uint32_t)w;
    region.imageExtent.height = (uint32_t)h;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cb, src, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    image_barrier(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1);
}

/* mipmaps by halving with blits, as glGenerateMipmap */
static void vk_tex_mipmap(PCGfxTex* o) {
    VkTex* t = (VkTex*)o->impl;
    VkTex n;
    VkCommandBuffer cb;
    int levels = 1;

    if (t == NULL || t->image == VK_NULL_HANDLE || t->format != VK_FORMAT_R8G8B8A8_UNORM) return;
    while ((t->w >> levels) > 0 || (t->h >> levels) > 0) levels++;
    if (levels == 1) return;
    memset(&n, 0, sizeof(n));
    if (!make_image(t->format, (uint32_t)t->w, (uint32_t)t->h, (uint32_t)levels, VK_SAMPLE_COUNT_1_BIT,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, &n.image, &n.view, &n.mem)) {
        return;
    }
    cb = upload_cmd();
    image_barrier(cb, t->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1);
    image_barrier(cb, n.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                  (uint32_t)levels);
    for (int l = 0; l < levels; l++) {
        VkImageBlit b;
        int sw = l == 0 ? t->w : (t->w >> (l - 1)) > 0 ? t->w >> (l - 1) : 1;
        int sh = l == 0 ? t->h : (t->h >> (l - 1)) > 0 ? t->h >> (l - 1) : 1;
        int dw = (t->w >> l) > 0 ? t->w >> l : 1, dh = (t->h >> l) > 0 ? t->h >> l : 1;

        memset(&b, 0, sizeof(b));
        b.srcSubresource.aspectMask = b.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.srcSubresource.layerCount = b.dstSubresource.layerCount = 1;
        b.srcSubresource.mipLevel = l == 0 ? 0 : (uint32_t)(l - 1);
        b.dstSubresource.mipLevel = (uint32_t)l;
        b.srcOffsets[1].x = l == 0 ? t->w : sw;
        b.srcOffsets[1].y = l == 0 ? t->h : sh;
        b.srcOffsets[1].z = 1;
        b.dstOffsets[1].x = dw;
        b.dstOffsets[1].y = dh;
        b.dstOffsets[1].z = 1;
        /* level 0 from the old image, each next one from the level before */
        vkCmdBlitImage(cb, l == 0 ? t->image : n.image,
                       l == 0 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, n.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
        image_barrier(cb, n.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, (uint32_t)l, 1);
    }
    image_barrier(cb, n.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, (uint32_t)levels);
    tex_release(t);
    t->image = n.image;
    t->view = n.view;
    t->mem = n.mem;
    t->levels = levels;
    t->uid = ++vk.next_uid;
    o->levels = levels;
}

static void vk_tex_free(PCGfxTex* o) {
    VkTex* t = (VkTex*)o->impl;

    if (t == NULL) return;
    if (t->image != VK_NULL_HANDLE) tex_release(t);
    free(t);
    o->impl = NULL;
}

static VkSampler sampler(const PCGfxTex* o) {
    int ws = o->wrap_s % 3, wt = o->wrap_t % 3, mn = o->min_filter % 3, mg = o->mag_filter ? 1 : 0;
    VkSampler* s = &vk.samplers[ws][wt][mn][mg];

    if (*s == VK_NULL_HANDLE) {
        static const VkSamplerAddressMode modes[3] = { VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                       VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                       VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT };
        VkSamplerCreateInfo ci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

        ci.magFilter = mg ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        ci.minFilter = mn == PC_FILTER_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        ci.addressModeU = modes[ws];
        ci.addressModeV = modes[wt];
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.maxLod = mn == PC_FILTER_LINEAR_MIPMAP_LINEAR ? VK_LOD_CLAMP_NONE : 0.0f;
        VK_CHECK(vkCreateSampler(vk.dev, &ci, NULL, s), "vkCreateSampler");
    }
    return *s;
}

/* what a texture unit samples: the texture, or black for none (OpenGL's
 * incomplete texture); returns the image's uid */
static uint64_t unit_image(PCTex name, VkDescriptorImageInfo* ii) {
    PCGfxTex* o = pc_gfx_tex_obj(name);
    VkTex* t = o != NULL ? (VkTex*)o->impl : NULL;

    if (t == NULL || t->image == VK_NULL_HANDLE) {
        o = pc_gfx_tex_obj(vk.dummy);
        t = (VkTex*)o->impl;
    }
    ii->sampler = sampler(o);
    ii->imageView = t->view;
    ii->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return t->uid;
}

/* a set of the frame's own pool (freed when the frame is done) */
static VkDescriptorSet alloc_set(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkDescriptorSet set = VK_NULL_HANDLE;
    Frame* f = &vk.frames[vk.fi];

    ai.descriptorPool = f->desc_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    f->desc_pool_used = 1;
    if (vkAllocateDescriptorSets(vk.dev, &ai, &set) != VK_SUCCESS) {
        /* the pool is out of sets: wait for the GPU, start the pool over
         * (flush_and_wait has the draw bind everything again) */
        flush_and_wait();
        vkResetDescriptorPool(vk.dev, f->desc_pool, 0);
        vk.last_tex_set = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateDescriptorSets(vk.dev, &ai, &set), "vkAllocateDescriptorSets");
    }
    return set;
}

/* the set of a GX buffer's three blocks, bound with their offsets */
static void ubo_set_make(HostBuf* hb) {
    static const VkDeviceSize sizes[3] = { sizeof(PCGXVertBlock), sizeof(PCGXFragBlock), sizeof(PCGXShaderConfig) };
    VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkDescriptorBufferInfo bi[3];
    VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

    ai.descriptorPool = vk.set_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &vk.ubo_layout;
    VK_CHECK(vkAllocateDescriptorSets(vk.dev, &ai, &hb->ubo_set), "vkAllocateDescriptorSets");
    for (int i = 0; i < 3; i++) {
        bi[i].buffer = hb->buf;
        bi[i].offset = 0;
        bi[i].range = sizes[i];
    }
    w.dstSet = hb->ubo_set;
    w.descriptorCount = 3;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    w.pBufferInfo = bi;
    vkUpdateDescriptorSets(vk.dev, 1, &w, 0, NULL);
}

/* --- the texture sets, kept across frames: a draw with textures drawn
 * before reuses their set (key: the images' uids and the samplers) --- */

static uint32_t texset_hash(int n, const uint64_t* uid, const VkSampler* smp) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t)n;

    for (int i = 0; i < n; i++) {
        uint64_t v = 0;

        memcpy(&v, &smp[i], sizeof(smp[i])); /* (a pointer or a 64-bit number) */
        h = (h ^ uid[i]) * 1099511628211ull;
        h = (h ^ v) * 1099511628211ull;
    }
    return (uint32_t)(h ^ (h >> 32));
}

static TexSet* texset_slot(int n, const uint64_t* uid, const VkSampler* smp, int* found) {
    uint32_t h = texset_hash(n, uid, smp);

    for (uint32_t i = 0; i < TEXSETS; i++) {
        TexSet* e = &vk.texsets[(h + i) & (TEXSETS - 1)];

        if (e->n == 0) {
            *found = 0;
            return e;
        }
        if (e->n == n && memcmp(e->uid, uid, (size_t)n * sizeof(uint64_t)) == 0 &&
            memcmp(e->smp, smp, (size_t)n * sizeof(VkSampler)) == 0) {
            *found = 1;
            return e;
        }
    }
    *found = 0;
    return NULL;
}

/* frees the sets no frame in flight uses and none drew with lately (or,
 * when that is none, every one the GPU is done with) */
static int texset_sweep(void) {
    TexSet* keep = (TexSet*)malloc(TEXSETS * sizeof(TexSet));
    VkDescriptorSet* drop = (VkDescriptorSet*)malloc(TEXSETS * sizeof(VkDescriptorSet));
    int nkeep = 0, ndrop = 0;

    if (keep == NULL || drop == NULL) {
        free(keep);
        free(drop);
        return 0;
    }
    for (int age = 120; age >= 0 && ndrop == 0; age = age > 0 ? 0 : -1) {
        nkeep = ndrop = 0;
        for (int i = 0; i < TEXSETS; i++) {
            TexSet* e = &vk.texsets[i];

            if (e->n == 0) continue;
            if (e->used <= vk.done_serial && e->used + (uint64_t)age < vk.serial) drop[ndrop++] = e->set;
            else keep[nkeep++] = *e;
        }
    }
    if (ndrop > 0) {
        vkFreeDescriptorSets(vk.dev, vk.set_pool, (uint32_t)ndrop, drop);
        memset(vk.texsets, 0, TEXSETS * sizeof(TexSet));
        for (int i = 0; i < nkeep; i++) {
            int found;
            TexSet* e = texset_slot(keep[i].n, keep[i].uid, keep[i].smp, &found);

            *e = keep[i];
        }
        vk.ntexsets = nkeep;
    }
    free(keep);
    free(drop);
    return ndrop;
}

static VkDescriptorSet tex_set(int n, const uint64_t* uid, const VkDescriptorImageInfo* ii,
                               VkDescriptorSetLayout layout) {
    VkSampler smp[5];
    VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    VkDescriptorSet set = VK_NULL_HANDLE;
    TexSet* e;
    int found;

    for (int i = 0; i < n; i++) smp[i] = ii[i].sampler;
    e = texset_slot(n, uid, smp, &found);
    if (found) {
        e->used = vk.serial;
        return e->set;
    }
    ai.descriptorPool = vk.set_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    if (vk.ntexsets >= TEXSETS * 3 / 4 || vkAllocateDescriptorSets(vk.dev, &ai, &set) != VK_SUCCESS) {
        set = VK_NULL_HANDLE;
        if (texset_sweep() > 0 && vk.ntexsets < TEXSETS * 3 / 4 &&
            vkAllocateDescriptorSets(vk.dev, &ai, &set) != VK_SUCCESS) {
            set = VK_NULL_HANDLE;
        }
        e = texset_slot(n, uid, smp, &found);
    }
    if (set == VK_NULL_HANDLE) {
        e = NULL; /* all in use: one of this frame only */
        set = alloc_set(layout);
    }
    w.dstSet = set;
    w.descriptorCount = (uint32_t)n;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = ii;
    vkUpdateDescriptorSets(vk.dev, 1, &w, 0, NULL);
    if (e != NULL) {
        e->n = n;
        memcpy(e->uid, uid, (size_t)n * sizeof(uint64_t));
        memcpy(e->smp, smp, (size_t)n * sizeof(VkSampler));
        e->set = set;
        e->used = vk.serial;
        vk.ntexsets++;
    }
    return set;
}

/* --- pipelines --- */

static VkPrimitiveTopology topology(int prim) {
    switch (prim) {
        case PC_PRIM_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PC_PRIM_TRIANGLE_FAN:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case PC_PRIM_LINES:          return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PC_PRIM_LINE_STRIP:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PC_PRIM_POINTS:         return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default:                     return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

/* the fixed-function state of the moment, as a key */
static uint32_t raster_key(VkPrimitiveTopology topo) {
    const PCGfxState* s = &g_gfx_state;
    uint32_t k = (uint32_t)topo;

    if (s->blend) {
        k |= 1u << 3;
        k |= (uint32_t)(s->blend_src & 15) << 4;
        k |= (uint32_t)(s->blend_dst & 15) << 8;
        k |= (uint32_t)(s->blend_src_a & 15) << 12;
        k |= (uint32_t)(s->blend_dst_a & 15) << 16;
        k |= (uint32_t)(s->blend_subtract != 0) << 20;
    }
    if (s->depth_test) {
        k |= 1u << 21;
        k |= (uint32_t)(s->depth_write != 0) << 22;
        k |= (uint32_t)(s->depth_func & 7) << 23;
    }
    k |= (uint32_t)(s->color_write != 0) << 26;
    k |= (uint32_t)(s->alpha_write != 0) << 27;
    k |= (uint32_t)(s->cull & 3) << 28;
    return k;
}

static VkBlendFactor blend_factor(uint32_t f) {
    static const VkBlendFactor t[10] = {
        VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA };
    return f < 10 ? t[f] : VK_BLEND_FACTOR_ONE;
}

static VkPipeline build_pipeline(int kind, uint32_t rkey, const PCGXShaderConfig* config) {
    VkSpecializationMapEntry entries[PC_GX_CONFIG_COUNT + 1];
    int spec_data[PC_GX_CONFIG_COUNT + 1];
    VkSpecializationInfo spec = { PC_GX_CONFIG_COUNT + 1, entries, sizeof(spec_data), spec_data };
    VkPipelineShaderStageCreateInfo stages[2] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
                                                  { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO } };
    VkVertexInputBindingDescription vb = { 0 };
    VkVertexInputAttributeDescription va[5];
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState cba = { 0 };
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkDynamicState dyn[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH };
    VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    VkGraphicsPipelineCreateInfo ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipeline pipe = VK_NULL_HANDLE;
    static const VkCompareOp cmp[8] = { VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL,
                                        VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER, VK_COMPARE_OP_NOT_EQUAL,
                                        VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_ALWAYS };
    int ui = kind == KIND_UI;

    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = ui ? vk.ui_vs : vk.gx_vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ui ? vk.ui_fs : vk.gx_fs;
    stages[1].pName = "main";
    if (!ui) {
        for (int i = 0; i <= PC_GX_CONFIG_COUNT; i++) {
            entries[i].constantID = (uint32_t)i;
            entries[i].offset = (uint32_t)(i * sizeof(int));
            entries[i].size = sizeof(int);
        }
        spec_data[0] = kind == KIND_UBER;
        for (int i = 0; i < PC_GX_CONFIG_COUNT; i++) spec_data[i + 1] = config != NULL ? config->v[i] : 0;
        stages[0].pSpecializationInfo = &spec;
        stages[1].pSpecializationInfo = &spec;
    }
    memset(va, 0, sizeof(va));
    if (ui) {
        vb.stride = sizeof(PCGfxUIVertex);
        va[0] = (VkVertexInputAttributeDescription){ 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PCGfxUIVertex, x) };
        va[1] = (VkVertexInputAttributeDescription){ 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PCGfxUIVertex, u) };
        va[2] = (VkVertexInputAttributeDescription){ 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PCGfxUIVertex, lx) };
        va[3] = (VkVertexInputAttributeDescription){ 3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(PCGfxUIVertex, color) };
        va[4] = (VkVertexInputAttributeDescription){ 4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PCGfxUIVertex, kind) };
        vi.vertexAttributeDescriptionCount = 5;
    } else {
        vb.stride = sizeof(PCGXVertex);
        va[0] = (VkVertexInputAttributeDescription){ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PCGXVertex, position) };
        va[1] = (VkVertexInputAttributeDescription){ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PCGXVertex, normal) };
        va[2] = (VkVertexInputAttributeDescription){ 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(PCGXVertex, color0) };
        va[3] = (VkVertexInputAttributeDescription){ 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PCGXVertex, texcoord) };
        vi.vertexAttributeDescriptionCount = 4;
    }
    vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.pVertexAttributeDescriptions = va;
    ia.topology = (VkPrimitiveTopology)(rkey & 7);
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = (VkCullModeFlags)((rkey >> 28) & 3); /* none, front, back, both: Vulkan's numbers */
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; /* (y is flipped in the shader: the picture is OpenGL's) */
    rs.lineWidth = 1.0f;
    ms.rasterizationSamples = vk.samples;
    if (rkey & (1u << 21)) {
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = (rkey >> 22) & 1 ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = cmp[(rkey >> 23) & 7];
    }
    if (rkey & (1u << 3)) {
        VkBlendOp op = (rkey >> 20) & 1 ? VK_BLEND_OP_REVERSE_SUBTRACT : VK_BLEND_OP_ADD;

        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = blend_factor((rkey >> 4) & 15);
        cba.dstColorBlendFactor = blend_factor((rkey >> 8) & 15);
        cba.srcAlphaBlendFactor = blend_factor((rkey >> 12) & 15);
        cba.dstAlphaBlendFactor = blend_factor((rkey >> 16) & 15);
        cba.colorBlendOp = op;
        cba.alphaBlendOp = op;
    }
    cba.colorWriteMask = ((rkey >> 26) & 1 ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT : 0) |
                         ((rkey >> 27) & 1 ? VK_COLOR_COMPONENT_A_BIT : 0);
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    dy.dynamicStateCount = 3;
    dy.pDynamicStates = dyn;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vps;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = ui ? vk.ui_pipe_layout : vk.gx_layout;
    ci.renderPass = render_pass(0, 0);
    if (vkCreateGraphicsPipelines(vk.dev, vk.cache, 1, &ci, NULL, &pipe) != VK_SUCCESS) return VK_NULL_HANDLE;
    return pipe;
}

static uint32_t pipe_hash(int kind, uint32_t rkey, const PCGXShaderConfig* config) {
    uint32_t h = 2166136261u ^ (uint32_t)kind;

    h = (h ^ rkey) * 16777619u;
    if (config != NULL) {
        for (int i = 0; i < PC_GX_CONFIG_COUNT; i++) h = (h ^ (uint32_t)config->v[i]) * 16777619u;
    }
    return h;
}

/* the entry of a pipeline, a new one when create */
static Pipe* pipe_find(int kind, uint32_t rkey, const PCGXShaderConfig* config, int create) {
    uint32_t h = pipe_hash(kind, rkey, config);

    for (uint32_t i = 0; i < PIPELINES; i++) {
        Pipe* p = &vk.pipes[(h + i) & (PIPELINES - 1)];
        int state = SDL_AtomicGet(&p->state);

        if (state == 0) {
            if (!create || vk.pipes_built >= PIPELINES * 3 / 4) return NULL;
            p->kind = kind;
            p->rkey = rkey;
            p->hash = h;
            if (config != NULL) p->config = *config;
            vk.pipes_built++;
            return p;
        }
        if (p->hash == h && p->kind == kind && p->rkey == rkey &&
            (config == NULL || memcmp(&p->config, config, sizeof(*config)) == 0)) {
            return p;
        }
    }
    return NULL;
}

/* the worker: builds the specialized pipelines queued */
static int worker_main(void* arg) {
    (void)arg;
    SDL_LockMutex(vk.lock);
    for (;;) {
        Pipe* p;

        while (vk.njobs == 0 && !vk.quit) SDL_CondWait(vk.wake, vk.lock);
        if (vk.quit) break;
        p = &vk.pipes[vk.queue_jobs[--vk.njobs]];
        SDL_UnlockMutex(vk.lock);
        p->pipe = build_pipeline(KIND_SPEC, p->rkey, &p->config);
        SDL_MemoryBarrierRelease();
        SDL_AtomicSet(&p->state, p->pipe != VK_NULL_HANDLE ? 2 : 3);
        SDL_LockMutex(vk.lock);
        vk.pipes_ready++;
        if (vk.configs_out != NULL && p->pipe != VK_NULL_HANDLE) {
            fwrite(&p->rkey, sizeof(p->rkey), 1, vk.configs_out);
            fwrite(&p->config, sizeof(p->config), 1, vk.configs_out);
            fflush(vk.configs_out);
        }
    }
    SDL_UnlockMutex(vk.lock);
    return 0;
}

static void queue_spec(Pipe* p) {
    SDL_AtomicSet(&p->state, 1);
    SDL_LockMutex(vk.lock);
    if (vk.njobs == vk.capjobs) {
        vk.capjobs = vk.capjobs ? vk.capjobs * 2 : 64;
        vk.queue_jobs = (int*)realloc(vk.queue_jobs, (size_t)vk.capjobs * sizeof(int));
    }
    vk.queue_jobs[vk.njobs++] = (int)(p - vk.pipes);
    SDL_CondSignal(vk.wake);
    SDL_UnlockMutex(vk.lock);
}

static VkPipeline gx_pipeline(uint32_t rkey) {
    Pipe* p;

    if (g_pc_settings.specialized_shaders && vk.worker != NULL) {
        p = pipe_find(KIND_SPEC, rkey, &g_gx.shader_config, 1);
        if (p != NULL) {
            int state = SDL_AtomicGet(&p->state);

            if (state == 2) {
                SDL_MemoryBarrierAcquire();
                return p->pipe;
            }
            if (state == 0) queue_spec(p);
        }
    }
    p = pipe_find(KIND_UBER, rkey, NULL, 1);
    if (p == NULL) return VK_NULL_HANDLE;
    if (SDL_AtomicGet(&p->state) == 0) {
        p->pipe = build_pipeline(KIND_UBER, rkey, NULL);
        SDL_AtomicSet(&p->state, p->pipe != VK_NULL_HANDLE ? 2 : 3);
    }
    return SDL_AtomicGet(&p->state) == 2 ? p->pipe : VK_NULL_HANDLE;
}

static VkPipeline ui_pipeline(uint32_t rkey) {
    Pipe* p = pipe_find(KIND_UI, rkey, NULL, 1);

    if (p == NULL) return VK_NULL_HANDLE;
    if (SDL_AtomicGet(&p->state) == 0) {
        p->pipe = build_pipeline(KIND_UI, rkey, NULL);
        SDL_AtomicSet(&p->state, p->pipe != VK_NULL_HANDLE ? 2 : 3);
    }
    return SDL_AtomicGet(&p->state) == 2 ? p->pipe : VK_NULL_HANDLE;
}

/* --- drawing --- */

/* the viewport and scissor of the state, in Vulkan's top-left pixels; 0 when empty */
static int set_view(VkCommandBuffer cb) {
    const PCGfxState* s = &g_gfx_state;
    int fb_h = (int)vk.extent.height, fb_w = (int)vk.extent.width;
    VkViewport vp;
    VkRect2D sc;
    float line = vk.wide_lines ? s->line_width : 1.0f;

    if (s->vp[2] <= 0 || s->vp[3] <= 0) return 0;
    vp.x = (float)s->vp[0];
    vp.y = (float)(fb_h - (s->vp[1] + s->vp[3]));
    vp.width = (float)s->vp[2];
    vp.height = (float)s->vp[3];
    vp.minDepth = s->depth_near < 0.0f ? 0.0f : s->depth_near > 1.0f ? 1.0f : s->depth_near;
    vp.maxDepth = s->depth_far < 0.0f ? 0.0f : s->depth_far > 1.0f ? 1.0f : s->depth_far;
    if (s->scissor_test) {
        int x0 = s->scissor[0], y0 = fb_h - (s->scissor[1] + s->scissor[3]);
        int x1 = x0 + s->scissor[2], y1 = y0 + s->scissor[3];

        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > fb_w) x1 = fb_w;
        if (y1 > fb_h) y1 = fb_h;
        if (x1 <= x0 || y1 <= y0) return 0;
        sc.offset.x = x0;
        sc.offset.y = y0;
        sc.extent.width = (uint32_t)(x1 - x0);
        sc.extent.height = (uint32_t)(y1 - y0);
    } else {
        sc.offset.x = sc.offset.y = 0;
        sc.extent = vk.extent;
    }
    if (memcmp(&vp, &vk.bound_vp, sizeof(vp)) != 0) {
        vkCmdSetViewport(cb, 0, 1, &vp);
        vk.bound_vp = vp;
    }
    if (memcmp(&sc, &vk.bound_scissor, sizeof(sc)) != 0) {
        vkCmdSetScissor(cb, 0, 1, &sc);
        vk.bound_scissor = sc;
    }
    if (line < 1.0f) line = 1.0f;
    if (line != vk.bound_line) {
        vkCmdSetLineWidth(cb, line);
        vk.bound_line = line;
    }
    return 1;
}

static void bind_pipe(VkCommandBuffer cb, VkPipeline pipe, VkPipelineLayout layout) {
    if (pipe != vk.bound_pipe) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vk.bound_pipe = pipe;
    }
    if (layout != vk.bound_layout) {
        vk.bound_layout = layout;
        vk.bound_ubo_set = vk.bound_tex_set = VK_NULL_HANDLE;
    }
}

/* the GX uniform blocks for this draw, written to the frame's buffer when they changed */
static void gx_uniforms(VkCommandBuffer cb, unsigned changed) {
    static const VkDeviceSize sizes[3] = { sizeof(PCGXVertBlock), sizeof(PCGXFragBlock), sizeof(PCGXShaderConfig) };
    const void* data[3] = { &g_gx.vert_block, &g_gx.frag_block, &g_gx.shader_config };
    static const unsigned bits[3] = { PC_GX_BLOCK_VERT, PC_GX_BLOCK_FRAG, PC_GX_BLOCK_CONFIG };
    VkDeviceSize align = vk.props.limits.minUniformBufferOffsetAlignment;
    HostBuf* hb = NULL;
    uint32_t offsets[3];
    int rewrite = 0;

    if (align < 16) align = 16;
    for (int i = 0; i < 3; i++) {
        if ((changed & bits[i]) || vk.blk_buf[i] == NULL) {
            VkDeviceSize off;
            unsigned char* p = stream_alloc(sizes[i], align, &hb, &off);

            memcpy(p, data[i], (size_t)sizes[i]);
            if (vk.blk_buf[i] != hb) rewrite = 1;
            vk.blk_buf[i] = hb;
            vk.blk_off[i] = off;
        }
    }
    /* the three must be in one buffer: the set covers one */
    if (vk.blk_buf[0] != vk.blk_buf[1] || vk.blk_buf[1] != vk.blk_buf[2]) {
        for (int i = 0; i < 3; i++) {
            VkDeviceSize off;
            unsigned char* p = stream_alloc(sizes[i], align, &hb, &off);

            memcpy(p, data[i], (size_t)sizes[i]);
            vk.blk_buf[i] = hb;
            vk.blk_off[i] = off;
        }
        rewrite = 1;
    }
    hb = vk.blk_buf[0];
    for (int i = 0; i < 3; i++) offsets[i] = (uint32_t)vk.blk_off[i];
    if (rewrite || hb->ubo_set != vk.bound_ubo_set || memcmp(offsets, vk.bound_offsets, sizeof(offsets)) != 0) {
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.gx_layout, 0, 1, &hb->ubo_set, 3, offsets);
        vk.bound_ubo_set = hb->ubo_set;
        memcpy(vk.bound_offsets, offsets, sizeof(offsets));
    }
}

static void gx_textures(VkCommandBuffer cb) {
    VkDescriptorImageInfo ii[5];
    uint64_t uid[5];
    VkSampler smp[5];

    for (int u = 0; u < 5; u++) {
        uid[u] = unit_image(g_gfx_state.bound[u], &ii[u]);
        smp[u] = ii[u].sampler;
    }
    if (vk.last_tex_set == VK_NULL_HANDLE || memcmp(uid, vk.last_uid, sizeof(uid)) != 0 ||
        memcmp(smp, vk.last_smp, sizeof(smp)) != 0) {
        vk.last_tex_set = tex_set(5, uid, ii, vk.tex_layout);
        memcpy(vk.last_uid, uid, sizeof(uid));
        memcpy(vk.last_smp, smp, sizeof(smp));
    }
    if (vk.last_tex_set != vk.bound_tex_set) {
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.gx_layout, 1, 1, &vk.last_tex_set, 0, NULL);
        vk.bound_tex_set = vk.last_tex_set;
    }
}

/* A draw sets up its pass, state and data; when that made the frame wait
 * for the GPU (a full buffer or pool: flush_and_wait), the pass was ended
 * and the draw sets up again in the next one. */
static void vk_draw_gx(int prim, const PCGXVertex* v, int count, unsigned changed) {
    if (count <= 0 || vk.lost) return;
    frame_begin();
    for (int attempt = 0; attempt < 3; attempt++) {
        VkCommandBuffer cb;
        VkPipeline pipe;
        HostBuf* hb;
        VkDeviceSize off;
        unsigned char* p;

        vk.rebind = 0;
        if (!pass_begin()) return;
        cb = vk.frames[vk.fi].cmd;
        if (!set_view(cb)) return;
        pipe = gx_pipeline(raster_key(topology(prim)));
        if (pipe == VK_NULL_HANDLE) return;
        bind_pipe(cb, pipe, vk.gx_layout);
        gx_uniforms(cb, changed);
        if (vk.rebind) continue;
        gx_textures(cb);
        if (vk.rebind) continue;
        p = stream_alloc((VkDeviceSize)count * sizeof(PCGXVertex), 16, &hb, &off);
        if (p == NULL) return;
        if (vk.rebind) continue;
        memcpy(p, v, (size_t)count * sizeof(PCGXVertex));
        vkCmdBindVertexBuffers(cb, 0, 1, &hb->buf, &off);
        if (prim == PC_PRIM_QUADS) {
            vkCmdBindIndexBuffer(cb, vk.quad_buf, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(cb, (uint32_t)(count / 4 * 6), 1, 0, 0, 0);
        } else {
            vkCmdDraw(cb, (uint32_t)count, 1, 0, 0);
        }
        return;
    }
}

static void vk_draw_ui(PCTex tex, const PCGfxUIVertex* v, int count, const unsigned short* indices, int index_count,
                       float space_w, float space_h) {
    float space[4] = { space_w, space_h, 0.0f, 0.0f };

    if (count <= 0 || vk.lost) return;
    frame_begin();
    for (int attempt = 0; attempt < 3; attempt++) {
        VkCommandBuffer cb;
        VkPipeline pipe;
        VkDescriptorImageInfo ii;
        VkDescriptorSet set;
        HostBuf* hb;
        HostBuf* ib = NULL;
        VkDeviceSize off, ioff = 0;
        unsigned char* p;
        unsigned char* ip = NULL;
        uint64_t uid;

        vk.rebind = 0;
        if (!pass_begin()) return;
        cb = vk.frames[vk.fi].cmd;
        if (!set_view(cb)) return;
        pipe = ui_pipeline(raster_key(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST));
        if (pipe == VK_NULL_HANDLE) return;
        bind_pipe(cb, pipe, vk.ui_pipe_layout);
        uid = unit_image(tex != 0 ? tex : vk.white, &ii);
        set = tex_set(1, &uid, &ii, vk.ui_layout);
        if (vk.rebind) continue;
        p = stream_alloc((VkDeviceSize)count * sizeof(PCGfxUIVertex), 16, &hb, &off);
        if (p == NULL) return;
        if (vk.rebind) continue;
        if (indices != NULL) {
            ip = stream_alloc((VkDeviceSize)index_count * 2, 4, &ib, &ioff);
            if (ip == NULL) return;
            if (vk.rebind) continue;
        }
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.ui_pipe_layout, 0, 1, &set, 0, NULL);
        vk.bound_tex_set = VK_NULL_HANDLE;
        vkCmdPushConstants(cb, vk.ui_pipe_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(space), space);
        memcpy(p, v, (size_t)count * sizeof(PCGfxUIVertex));
        vkCmdBindVertexBuffers(cb, 0, 1, &hb->buf, &off);
        if (indices != NULL) {
            memcpy(ip, indices, (size_t)index_count * 2);
            vkCmdBindIndexBuffer(cb, ib->buf, ioff, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(cb, (uint32_t)index_count, 1, 0, 0, 0);
        } else {
            vkCmdDraw(cb, (uint32_t)count, 1, 0, 0);
        }
        return;
    }
}

static void vk_clear(int color, int depth, const float rgba[4], float depth_value) {
    const PCGfxState* s = &g_gfx_state;
    int full = !s->scissor_test || (s->scissor[0] <= 0 && s->scissor[1] <= 0 &&
                                    s->scissor[0] + s->scissor[2] >= (int)vk.extent.width &&
                                    s->scissor[1] + s->scissor[3] >= (int)vk.extent.height);

    if (vk.lost) return;
    color = color && (s->color_write || s->alpha_write); /* (as glClear: what the masks let through) */
    depth = depth && s->depth_write;
    if (!color && !depth) return;
    frame_begin();
    if (!vk.in_pass && full) {
        /* at the start of a render pass: its clear */
        if (color) {
            memcpy(vk.clear_color, rgba, sizeof(vk.clear_color));
            vk.pending_clear |= 1;
        }
        if (depth) {
            vk.clear_depth = depth_value;
            vk.pending_clear |= 2;
        }
        if (!vk.drew) {
            /* the frame's first pass clears both anyway */
            return;
        }
        if ((vk.pending_clear & 3) == 3) return;
    }
    if (!pass_begin()) return;
    {
        VkClearAttachment ca[2];
        VkClearRect cr;
        uint32_t n = 0;
        int fb_h = (int)vk.extent.height;

        memset(ca, 0, sizeof(ca));
        if (color) {
            ca[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            memcpy(ca[n].clearValue.color.float32, rgba, 4 * sizeof(float));
            n++;
        }
        if (depth) {
            ca[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            ca[n].clearValue.depthStencil.depth = depth_value;
            n++;
        }
        memset(&cr, 0, sizeof(cr));
        cr.layerCount = 1;
        if (full) {
            cr.rect.extent = vk.extent;
        } else {
            int x0 = s->scissor[0], y0 = fb_h - (s->scissor[1] + s->scissor[3]);
            int x1 = x0 + s->scissor[2], y1 = y0 + s->scissor[3];

            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > (int)vk.extent.width) x1 = (int)vk.extent.width;
            if (y1 > fb_h) y1 = fb_h;
            if (x1 <= x0 || y1 <= y0) return;
            cr.rect.offset.x = x0;
            cr.rect.offset.y = y0;
            cr.rect.extent.width = (uint32_t)(x1 - x0);
            cr.rect.extent.height = (uint32_t)(y1 - y0);
        }
        vkCmdClearAttachments(vk.frames[vk.fi].cmd, n, ca, 1, &cr);
    }
}

/* glReadPixels: GL rows (bottom up) of the picture so far */
static void vk_read_pixels(int x, int y, int w, int h, int rgba, void* out) {
    Frame* f;
    VkBufferImageCopy region;
    VkDeviceSize need = (VkDeviceSize)w * h * 4;
    unsigned char* dst = (unsigned char*)out;
    int bgra = vk.color_format == VK_FORMAT_B8G8R8A8_UNORM;
    int fb_h = (int)vk.extent.height;

    memset(out, 0, (size_t)w * h * (rgba ? 4 : 3));
    if (w <= 0 || h <= 0 || vk.lost) return;
    frame_begin();
    if (!pass_begin()) return; /* (the frame's picture, cleared if nothing was drawn) */
    pass_end();
    f = &vk.frames[vk.fi];
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)vk.extent.width) w = (int)vk.extent.width - x;
    if (y + h > fb_h) h = fb_h - y;
    if (w <= 0 || h <= 0) return;
    if (vk.readback_size < need) {
        if (vk.readback != VK_NULL_HANDLE) {
            vkUnmapMemory(vk.dev, vk.readback_mem);
            vkDestroyBuffer(vk.dev, vk.readback, NULL);
            vkFreeMemory(vk.dev, vk.readback_mem, NULL);
        }
        {
            HostBuf hb;

            if (!host_buffer(&hb, need, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) return;
            vk.readback = hb.buf;
            vk.readback_mem = hb.mem;
            vk.readback_map = hb.map;
            vk.readback_size = need;
        }
    }
    image_barrier(f->cmd, vk.images[vk.image], vk.layout[vk.image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1);
    vk.layout[vk.image] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = x;
    region.imageOffset.y = fb_h - (y + h);
    region.imageExtent.width = (uint32_t)w;
    region.imageExtent.height = (uint32_t)h;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(f->cmd, vk.images[vk.image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.readback, 1, &region);
    flush_and_wait();
    {
        const unsigned char* src = (const unsigned char*)vk.readback_map;
        int bpp = rgba ? 4 : 3;

        for (int r = 0; r < h; r++) {
            const unsigned char* s = src + (size_t)(h - 1 - r) * w * 4; /* the image's rows go down */
            unsigned char* d = dst + (size_t)r * w * bpp;

            for (int i = 0; i < w; i++) {
                d[i * bpp + 0] = s[i * 4 + (bgra ? 2 : 0)];
                d[i * bpp + 1] = s[i * 4 + 1];
                d[i * bpp + 2] = s[i * 4 + (bgra ? 0 : 2)];
                if (rgba) d[i * bpp + 3] = s[i * 4 + 3];
            }
        }
    }
}

/* --- setup and teardown --- */

static void vk_fail(const char* what, VkResult r) {
    fprintf(stderr, "[PC/Vulkan] %s failed (%d)\n", what, (int)r);
    if (r == VK_ERROR_DEVICE_LOST || r == VK_ERROR_OUT_OF_DEVICE_MEMORY || r == VK_ERROR_OUT_OF_HOST_MEMORY) {
        /* next time OpenGL: this GPU does not take the game's Vulkan */
        g_pc_settings.renderer = PC_GFX_OPENGL;
        pc_settings_save();
        pc_fatal("The graphics driver stopped (Vulkan: %s, error %d). The game will use OpenGL from the next start "
                 "(Settings > Video > Graphics API).", what, (int)r);
    }
    vk.lost = 1;
}

static void save_cache(void) {
    size_t size = 0;
    void* data;
    FILE* f;

    if (vk.cache == VK_NULL_HANDLE || vkGetPipelineCacheData(vk.dev, vk.cache, &size, NULL) != VK_SUCCESS || size == 0) return;
    data = malloc(size);
    if (data == NULL) return;
    if (vkGetPipelineCacheData(vk.dev, vk.cache, &size, data) == VK_SUCCESS) {
        f = fopen(CACHE_FILE, "wb");
        if (f != NULL) {
            fwrite(data, 1, size, f);
            fclose(f);
        }
    }
    free(data);
}

static void load_cache(void) {
    VkPipelineCacheCreateInfo ci = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    FILE* f = fopen(CACHE_FILE, "rb");
    void* data = NULL;
    long n = 0;

    if (f != NULL) {
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 32 && n < (64L << 20) && (data = malloc((size_t)n)) != NULL && fread(data, 1, (size_t)n, f) == (size_t)n) {
            ci.initialDataSize = (size_t)n; /* (the driver checks the header: another GPU's is ignored) */
            ci.pInitialData = data;
        }
        fclose(f);
    }
    if (vkCreatePipelineCache(vk.dev, &ci, NULL, &vk.cache) != VK_SUCCESS) {
        ci.initialDataSize = 0;
        ci.pInitialData = NULL;
        vkCreatePipelineCache(vk.dev, &ci, NULL, &vk.cache);
    }
    free(data);
}

/* the configurations of earlier runs: queued at start */
static void load_configs(void) {
    FILE* f = fopen(CONFIGS_FILE, "rb");
    unsigned head[2] = { 0x31435856u /* "VXC1" */, PC_GX_CONFIG_COUNT };
    unsigned got[2];
    int good = 0, queued = 0;

    if (f != NULL) {
        good = fread(got, sizeof(got), 1, f) == 1 && got[0] == head[0] && got[1] == head[1];
        while (good) {
            uint32_t rkey;
            PCGXShaderConfig c;
            Pipe* p;

            if (fread(&rkey, sizeof(rkey), 1, f) != 1 || fread(&c, sizeof(c), 1, f) != 1) break;
            p = pipe_find(KIND_SPEC, rkey, &c, 1);
            if (p != NULL && SDL_AtomicGet(&p->state) == 0) {
                queue_spec(p);
                queued++;
            }
        }
        fclose(f);
    }
    vk.configs_out = fopen(CONFIGS_FILE, good ? "ab" : "wb");
    if (vk.configs_out != NULL && !good) {
        fwrite(head, sizeof(head), 1, vk.configs_out);
        fflush(vk.configs_out);
    }
    if (queued) printf("[PC/Vulkan] %d pipelines of earlier runs queued\n", queued);
}

static VkShaderModule shader_module(const unsigned int* code, size_t words) {
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule m = VK_NULL_HANDLE;

    ci.codeSize = words * 4;
    ci.pCode = code;
    vkCreateShaderModule(vk.dev, &ci, NULL, &m);
    return m;
}

static int layouts(void) {
    VkDescriptorSetLayoutBinding b[5];
    VkDescriptorSetLayoutCreateInfo li = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkPipelineLayoutCreateInfo pi = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkDescriptorSetLayout sets[2];
    VkPushConstantRange pc = { VK_SHADER_STAGE_VERTEX_BIT, 0, 16 };

    memset(b, 0, sizeof(b));
    for (int i = 0; i < 3; i++) {
        b[i].binding = (uint32_t)i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    li.bindingCount = 3;
    li.pBindings = b;
    if (vkCreateDescriptorSetLayout(vk.dev, &li, NULL, &vk.ubo_layout) != VK_SUCCESS) return 0;
    for (int i = 0; i < 5; i++) {
        b[i].binding = (uint32_t)i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    li.bindingCount = 5;
    if (vkCreateDescriptorSetLayout(vk.dev, &li, NULL, &vk.tex_layout) != VK_SUCCESS) return 0;
    li.bindingCount = 1;
    if (vkCreateDescriptorSetLayout(vk.dev, &li, NULL, &vk.ui_layout) != VK_SUCCESS) return 0;
    sets[0] = vk.ubo_layout;
    sets[1] = vk.tex_layout;
    pi.setLayoutCount = 2;
    pi.pSetLayouts = sets;
    if (vkCreatePipelineLayout(vk.dev, &pi, NULL, &vk.gx_layout) != VK_SUCCESS) return 0;
    pi.setLayoutCount = 1;
    pi.pSetLayouts = &vk.ui_layout;
    pi.pushConstantRangeCount = 1;
    pi.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(vk.dev, &pi, NULL, &vk.ui_pipe_layout) != VK_SUCCESS) return 0;
    vk.gx_vs = shader_module(pc_shader_gx_vert_spv, sizeof(pc_shader_gx_vert_spv) / 4);
    vk.gx_fs = shader_module(pc_shader_gx_frag_spv, sizeof(pc_shader_gx_frag_spv) / 4);
    vk.ui_vs = shader_module(pc_shader_ui_vert_spv, sizeof(pc_shader_ui_vert_spv) / 4);
    vk.ui_fs = shader_module(pc_shader_ui_frag_spv, sizeof(pc_shader_ui_frag_spv) / 4);
    return vk.gx_vs && vk.gx_fs && vk.ui_vs && vk.ui_fs;
}

static int frames_create(void) {
    {   /* the sets kept: textures (TEXSETS) and the streaming buffers' blocks (9 a frame) */
        VkDescriptorPoolSize sizes[2] = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, FRAMES * 9 * 3 },
                                          { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TEXSETS * 5 } };
        VkDescriptorPoolCreateInfo di = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };

        di.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        di.maxSets = TEXSETS + FRAMES * 9;
        di.poolSizeCount = 2;
        di.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(vk.dev, &di, NULL, &vk.set_pool) != VK_SUCCESS) return 0;
        vk.texsets = (TexSet*)calloc(TEXSETS, sizeof(TexSet));
        if (vk.texsets == NULL) return 0;
    }
    for (int i = 0; i < FRAMES; i++) {
        Frame* f = &vk.frames[i];
        VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        /* (sets of one frame, when the kept ones are all in use) */
        VkDescriptorPoolSize sizes[1] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 * 5 } };
        VkDescriptorPoolCreateInfo di = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };

        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = vk.qfamily;
        if (vkCreateCommandPool(vk.dev, &pi, NULL, &f->pool) != VK_SUCCESS ||
            vkCreateCommandPool(vk.dev, &pi, NULL, &f->upload_pool) != VK_SUCCESS) return 0;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        ai.commandPool = f->pool;
        if (vkAllocateCommandBuffers(vk.dev, &ai, &f->cmd) != VK_SUCCESS) return 0;
        ai.commandPool = f->upload_pool;
        if (vkAllocateCommandBuffers(vk.dev, &ai, &f->upload) != VK_SUCCESS) return 0;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(vk.dev, &fi, NULL, &f->fence) != VK_SUCCESS ||
            vkCreateSemaphore(vk.dev, &si, NULL, &f->acquired) != VK_SUCCESS ||
            vkCreateSemaphore(vk.dev, &si, NULL, &f->rendered) != VK_SUCCESS) return 0;
        di.maxSets = 1024;
        di.poolSizeCount = 1;
        di.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(vk.dev, &di, NULL, &f->desc_pool) != VK_SUCCESS) return 0;
        if (!host_buffer(&f->stream, STREAM_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ||
            !host_buffer(&f->staging, STAGING_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return 0;
        ubo_set_make(&f->stream);
    }
    return 1;
}

static int has_ext(const VkExtensionProperties* e, uint32_t n, const char* name) {
    for (uint32_t i = 0; i < n; i++) {
        if (strcmp(e[i].extensionName, name) == 0) return 1;
    }
    return 0;
}

/* a GPU that draws to the surface; discrete before integrated before the rest */
static int pick_device(void) {
    VkPhysicalDevice devs[16];
    uint32_t n = 16;
    int best = -1, best_score = -1;
    uint32_t best_family = 0;

    if (vkEnumeratePhysicalDevices(vk.instance, &n, devs) < 0 || n == 0) return 0;
    for (uint32_t d = 0; d < n; d++) {
        VkPhysicalDeviceProperties p;
        VkQueueFamilyProperties q[16];
        uint32_t nq = 16, ne = 0;
        VkExtensionProperties* ext;
        int score;

        vkGetPhysicalDeviceProperties(devs[d], &p);
        vkEnumerateDeviceExtensionProperties(devs[d], NULL, &ne, NULL);
        ext = (VkExtensionProperties*)calloc(ne ? ne : 1, sizeof(*ext));
        vkEnumerateDeviceExtensionProperties(devs[d], NULL, &ne, ext);
        if (!has_ext(ext, ne, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            free(ext);
            continue;
        }
        free(ext);
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &nq, q);
        score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
              : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
              : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2 : 1;
        for (uint32_t i = 0; i < nq; i++) {
            VkBool32 present = VK_FALSE;

            vkGetPhysicalDeviceSurfaceSupportKHR(devs[d], i, vk.surface, &present);
            if ((q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present && score > best_score) {
                best = (int)d;
                best_score = score;
                best_family = i;
                break;
            }
        }
    }
    if (best < 0) return 0;
    vk.phys = devs[best];
    vk.qfamily = best_family;
    vkGetPhysicalDeviceProperties(vk.phys, &vk.props);
    vkGetPhysicalDeviceMemoryProperties(vk.phys, &vk.memprops);
    return 1;
}

static int create_device(void) {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    VkDeviceCreateInfo ci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkPhysicalDeviceFeatures have, want;
    const char* exts[2] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_portability_subset" };
    uint32_t ne = 0;
    VkExtensionProperties* ext;

    vkGetPhysicalDeviceFeatures(vk.phys, &have);
    memset(&want, 0, sizeof(want));
    want.textureCompressionBC = have.textureCompressionBC;
    want.wideLines = have.wideLines;
    vk.bc = have.textureCompressionBC;
    vk.wide_lines = have.wideLines;
    qi.queueFamilyIndex = vk.qfamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    vkEnumerateDeviceExtensionProperties(vk.phys, NULL, &ne, NULL);
    ext = (VkExtensionProperties*)calloc(ne ? ne : 1, sizeof(*ext));
    vkEnumerateDeviceExtensionProperties(vk.phys, NULL, &ne, ext);
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = has_ext(ext, ne, exts[1]) ? 2 : 1;
    ci.ppEnabledExtensionNames = exts;
    ci.pEnabledFeatures = &want;
    free(ext);
    if (vkCreateDevice(vk.phys, &ci, NULL, &vk.dev) != VK_SUCCESS) return 0;
#define VK_LOAD_DEVICE(name) name = (PFN_##name)vkGetDeviceProcAddr(vk.dev, #name); if (name == NULL) return 0;
    VK_DEVICE(VK_LOAD_DEVICE)
#undef VK_LOAD_DEVICE
    vkGetDeviceQueue(vk.dev, vk.qfamily, 0, &vk.queue);
    return 1;
}

static int pick_formats(void) {
    VkSurfaceFormatKHR fmts[64];
    uint32_t n = 64;
    static const VkFormat depths[3] = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT };
    VkSampleCountFlags counts;

    if (vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys, vk.surface, &n, fmts) < 0 || n == 0) return 0;
    vk.color_format = VK_FORMAT_UNDEFINED;
    for (uint32_t i = 0; i < n; i++) {
        /* not an sRGB one: the game's colors go to the screen as they are, as with OpenGL */
        if ((fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) &&
            fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            vk.color_format = fmts[i].format;
            break;
        }
    }
    if (vk.color_format == VK_FORMAT_UNDEFINED) {
        if (n == 1 && fmts[0].format == VK_FORMAT_UNDEFINED) vk.color_format = VK_FORMAT_B8G8R8A8_UNORM;
        else return 0;
    }
    vk.depth_format = VK_FORMAT_UNDEFINED;
    for (int i = 0; i < 3; i++) {
        VkFormatProperties fp;

        vkGetPhysicalDeviceFormatProperties(vk.phys, depths[i], &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            vk.depth_format = depths[i];
            break;
        }
    }
    if (vk.depth_format == VK_FORMAT_UNDEFINED) return 0;
    counts = vk.props.limits.framebufferColorSampleCounts & vk.props.limits.framebufferDepthSampleCounts;
    vk.samples = VK_SAMPLE_COUNT_1_BIT;
    for (int s = 8; s >= 2; s /= 2) {
        if (s <= vk.want_samples && (counts & (VkSampleCountFlags)s)) {
            vk.samples = (VkSampleCountFlagBits)s;
            break;
        }
    }
    if ((int)vk.samples != (vk.want_samples > 1 ? vk.want_samples : 1)) {
        printf("[PC] MSAA %dx is not available, using %dx\n", vk.want_samples, vk.samples > 1 ? (int)vk.samples : 0);
    }
    return 1;
}

static void vk_shutdown(void) {
    if (vk.dev != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vk.dev);
        if (vk.worker != NULL) {
            SDL_LockMutex(vk.lock);
            vk.quit = 1;
            SDL_CondSignal(vk.wake);
            SDL_UnlockMutex(vk.lock);
            SDL_WaitThread(vk.worker, NULL);
            vk.worker = NULL;
        }
        save_cache();
        if (vk.configs_out != NULL) fclose(vk.configs_out);
        for (int i = 0; vk.pipes != NULL && i < PIPELINES; i++) {
            if (vk.pipes[i].pipe != VK_NULL_HANDLE) vkDestroyPipeline(vk.dev, vk.pipes[i].pipe, NULL);
        }
        free(vk.pipes);
        if (vk.cache) vkDestroyPipelineCache(vk.dev, vk.cache, NULL);
        garbage_collect(1);
        swapchain_destroy(0);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                if (vk.passes[i][j]) vkDestroyRenderPass(vk.dev, vk.passes[i][j], NULL);
        for (int i = 0; i < FRAMES; i++) {
            Frame* f = &vk.frames[i];

            if (f->pool) vkDestroyCommandPool(vk.dev, f->pool, NULL);
            if (f->upload_pool) vkDestroyCommandPool(vk.dev, f->upload_pool, NULL);
            if (f->fence) vkDestroyFence(vk.dev, f->fence, NULL);
            if (f->acquired) vkDestroySemaphore(vk.dev, f->acquired, NULL);
            if (f->rendered) vkDestroySemaphore(vk.dev, f->rendered, NULL);
            if (f->desc_pool) vkDestroyDescriptorPool(vk.dev, f->desc_pool, NULL);
            /* (the sets of set_pool go with it) */
            host_buffer_free(&f->stream);
            host_buffer_free(&f->staging);
            for (int k = 0; k < f->nextra; k++) host_buffer_free(&f->extra[k]);
        }
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++)
                for (int c = 0; c < 3; c++)
                    for (int d = 0; d < 2; d++)
                        if (vk.samplers[a][b][c][d]) vkDestroySampler(vk.dev, vk.samplers[a][b][c][d], NULL);
        if (vk.set_pool) vkDestroyDescriptorPool(vk.dev, vk.set_pool, NULL);
        free(vk.texsets);
        if (vk.gx_layout) vkDestroyPipelineLayout(vk.dev, vk.gx_layout, NULL);
        if (vk.ui_pipe_layout) vkDestroyPipelineLayout(vk.dev, vk.ui_pipe_layout, NULL);
        if (vk.ubo_layout) vkDestroyDescriptorSetLayout(vk.dev, vk.ubo_layout, NULL);
        if (vk.tex_layout) vkDestroyDescriptorSetLayout(vk.dev, vk.tex_layout, NULL);
        if (vk.ui_layout) vkDestroyDescriptorSetLayout(vk.dev, vk.ui_layout, NULL);
        if (vk.gx_vs) vkDestroyShaderModule(vk.dev, vk.gx_vs, NULL);
        if (vk.gx_fs) vkDestroyShaderModule(vk.dev, vk.gx_fs, NULL);
        if (vk.ui_vs) vkDestroyShaderModule(vk.dev, vk.ui_vs, NULL);
        if (vk.ui_fs) vkDestroyShaderModule(vk.dev, vk.ui_fs, NULL);
        if (vk.quad_buf) vkDestroyBuffer(vk.dev, vk.quad_buf, NULL);
        if (vk.quad_mem) vkFreeMemory(vk.dev, vk.quad_mem, NULL);
        if (vk.readback) vkDestroyBuffer(vk.dev, vk.readback, NULL);
        if (vk.readback_mem) vkFreeMemory(vk.dev, vk.readback_mem, NULL);
        for (int i = 0; i < vk.nblocks; i++) {
            vkFreeMemory(vk.dev, vk.blocks[i].mem, NULL);
            free(vk.blocks[i].free);
        }
        free(vk.blocks);
        free(vk.garbage);
        free(vk.queue_jobs);
        vkDestroyDevice(vk.dev, NULL);
    }
    if (vk.surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
    if (vk.instance != VK_NULL_HANDLE) vkDestroyInstance(vk.instance, NULL);
    if (vk.lock) SDL_DestroyMutex(vk.lock);
    if (vk.wake) SDL_DestroyCond(vk.wake);
    memset(&vk, 0, sizeof(vk));
}

static void vk_drawable_size(int* w, int* h) {
    SDL_Vulkan_GetDrawableSize(vk.window, w, h);
    if ((uint32_t)*w != vk.extent.width || (uint32_t)*h != vk.extent.height) vk.swapchain_dirty = 1;
}

static void vk_set_vsync(int on) {
    if (on != vk.vsync) {
        vk.vsync = on;
        vk.swapchain_dirty = 1;
    }
}

/* Android takes the window away while the app is in the background */
void pc_gfx_vk_event(const SDL_Event* e) {
    if (vk.dev == VK_NULL_HANDLE) return;
    if (e->type == SDL_APP_WILLENTERBACKGROUND) {
        vk.want_suspend = 1; /* (after the frame that may hold an image) */
        if (!vk.frames[vk.fi].acquired_image) suspend();
    } else if (e->type == SDL_APP_DIDENTERFOREGROUND) {
        vk.want_suspend = 0;
        vk.suspended = 0; /* acquire() makes the surface again */
        vk.swapchain_dirty = 1;
    } else if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        vk.swapchain_dirty = 1;
    }
}

int pc_gfx_vk_open(const char* title, int w, int h, Uint32 sdl_flags, int msaa) {
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo ii = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    const char* exts[16];
    unsigned int next = 16;

    memset(&vk, 0, sizeof(vk));
    if (SDL_Vulkan_LoadLibrary(NULL) != 0) {
        printf("[PC/Vulkan] no Vulkan here: %s\n", SDL_GetError());
        return 0;
    }
    vk.window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, sdl_flags | SDL_WINDOW_VULKAN);
    if (vk.window == NULL) {
        printf("[PC/Vulkan] window: %s\n", SDL_GetError());
        SDL_Vulkan_UnloadLibrary();
        return 0;
    }
    vkGetInstanceProcAddr_ = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
#define VK_LOAD_GLOBAL(name) name = (PFN_##name)vkGetInstanceProcAddr_(VK_NULL_HANDLE, #name);
    VK_GLOBAL(VK_LOAD_GLOBAL)
#undef VK_LOAD_GLOBAL
    if (vkGetInstanceProcAddr_ == NULL || vkCreateInstance == NULL ||
        !SDL_Vulkan_GetInstanceExtensions(vk.window, &next, exts)) {
        goto fail;
    }
    app.pApplicationName = "Dobutsu no Mori e+";
    app.pEngineName = "afe-port";
    app.apiVersion = VK_API_VERSION_1_0;
    ii.pApplicationInfo = &app;
    ii.enabledExtensionCount = next;
    ii.ppEnabledExtensionNames = exts;
    if (vkCreateInstance(&ii, NULL, &vk.instance) != VK_SUCCESS) goto fail;
#define VK_LOAD_INSTANCE(name) name = (PFN_##name)vkGetInstanceProcAddr_(vk.instance, #name); if (name == NULL) goto fail;
    VK_INSTANCE(VK_LOAD_INSTANCE)
#undef VK_LOAD_INSTANCE
    if (!SDL_Vulkan_CreateSurface(vk.window, vk.instance, &vk.surface)) goto fail;
    vk.want_samples = msaa;
    vk.vsync = g_pc_settings.vsync;
    if (!pick_device() || !create_device() || !pick_formats() || !layouts() || !frames_create()) goto fail;
    load_cache();
    vk.pipes = (Pipe*)calloc(PIPELINES, sizeof(Pipe));
    {   /* two triangles for each quad */
        HostBuf hb;

        if (!host_buffer(&hb, PC_GX_MAX_VERTS / 4 * 6 * 2, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) goto fail;
        memcpy(hb.map, pc_gfx_quad_indices(), PC_GX_MAX_VERTS / 4 * 6 * 2);
        vk.quad_buf = hb.buf;
        vk.quad_mem = hb.mem;
    }
    if (!swapchain_create()) goto fail;
    g_pc_window = vk.window;

    snprintf(s_backend.device, sizeof(s_backend.device), "%.120s, Vulkan %u.%u.%u", vk.props.deviceName,
             VK_VERSION_MAJOR(vk.props.apiVersion), VK_VERSION_MINOR(vk.props.apiVersion),
             VK_VERSION_PATCH(vk.props.apiVersion));
    s_backend.bc7 = s_backend.s3tc = vk.bc;
    s_backend.shutdown = vk_shutdown;
    s_backend.drawable_size = vk_drawable_size;
    s_backend.set_vsync = vk_set_vsync;
    s_backend.present = vk_present;
    s_backend.tex_image = vk_tex_image;
    s_backend.tex_subimage = vk_tex_subimage;
    s_backend.tex_mipmap = vk_tex_mipmap;
    s_backend.tex_free = vk_tex_free;
    s_backend.clear = vk_clear;
    s_backend.draw_gx = vk_draw_gx;
    s_backend.draw_ui = vk_draw_ui;
    s_backend.read_pixels = vk_read_pixels;
    g_gfx_backend = &s_backend;

    {   /* black for units with nothing bound, white for UI drawing without a texture */
        static const unsigned char black[4] = { 0, 0, 0, 255 }, white[4] = { 255, 255, 255, 255 };

        vk.dummy = pc_gfx_tex_new();
        pc_gfx_tex_upload(vk.dummy, 1, 1, PC_TEXF_RGBA8, black, 4, PC_FILTER_NEAREST);
        vk.white = pc_gfx_tex_new();
        pc_gfx_tex_upload(vk.white, 1, 1, PC_TEXF_RGBA8, white, 4, PC_FILTER_NEAREST);
    }
#ifdef _WIN32
    _mkdir(CACHE_DIR);
#else
    mkdir(CACHE_DIR, 0755);
#endif
    vk.lock = SDL_CreateMutex();
    vk.wake = SDL_CreateCond();
    if (g_pc_settings.specialized_shaders && vk.lock != NULL && vk.wake != NULL) {
        vk.worker = SDL_CreateThread(worker_main, "VkPipelines", NULL);
        if (vk.worker != NULL) load_configs();
    }
    printf("[PC/Vulkan] %s; %s, %dx MSAA, specialized pipelines %s\n", s_backend.device,
           vk.color_format == VK_FORMAT_B8G8R8A8_UNORM ? "BGRA" : "RGBA", vk.samples > 1 ? (int)vk.samples : 0,
           vk.worker != NULL ? "on (built on a thread)" : "off");
    return 1;

fail:
    printf("[PC/Vulkan] cannot start Vulkan here\n");
    {
        SDL_Window* win = vk.window;

        g_gfx_backend = NULL;
        vk_shutdown();
        if (win != NULL) SDL_DestroyWindow(win);
    }
    SDL_Vulkan_UnloadLibrary();
    return 0;
}

#endif /* PC_GFX_VULKAN_BUILT */
