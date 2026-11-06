/* pc_gfx_d3d12.c - the Direct3D 12 backend of pc_gfx (Windows 10 or later).
 *
 * Like the Vulkan one (pc_gfx_vk.c): pc_gfx.c keeps OpenGL's state, a draw
 * turns the state of the moment into a pipeline state object and the data
 * into the frame's upload buffer.
 *
 * - Shaders: HLSL (pc/src/gen/pc_shaders.h, from the same GLSL through
 *   SPIR-V and spirv-cross) compiled at run time with D3DCompile; a GX
 *   configuration is a set of SPIRV_CROSS_CONSTANT_ID_<n> defines. The uber
 *   shader draws a configuration until the one specialized for it is built
 *   on the worker thread. Compiled shaders are kept in shader_cache/d3d12.bin,
 *   the configurations seen in shader_cache/d3d12_configs.bin (built at start).
 * - The uniform blocks are root constant buffer views into the frame's
 *   upload buffer; the textures and samplers of a draw are descriptor tables
 *   kept across frames.
 * - D3D's clip space is OpenGL's but for depth (0..w, done in the shader);
 *   the viewport and scissor are turned from OpenGL's bottom-left origin.
 * - Textures are placed in shared heaps; an upload goes through the frame's
 *   staging buffer before its draws, and an image a frame drew with gets a
 *   new one when it is uploaded again. Mipmaps (HD textures) are made on the
 *   CPU. glReadPixels copies the picture back after waiting for the GPU. */
#ifdef PC_GFX_D3D12_BUILT

#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS

#include "pc_gfx_internal.h"
#include "pc_settings.h"
#include "../gen/pc_shaders.h"

#include <initguid.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <SDL_syswm.h>
#include <direct.h>

/* (structures are started as { 0 } and filled in) */
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#define FRAMES 2                  /* frames in flight */
#define BUFFERS 3                 /* swapchain images */
#define STREAM_SIZE (8u << 20)    /* a frame's vertices, indices and constants */
#define STAGING_SIZE (32u << 20)  /* a frame's texture uploads */
#define BLOCK_SIZE (64u << 20)    /* memory taken at once for textures */
#define PIPELINES 4096            /* a power of two */
#define TEXSETS 8192              /* kept texture tables (a power of two) */
#define SRV_TABLES 8192           /* tables of 5 in the shader-visible heap */
#define SMP_TABLES 400            /* sampler tables of 5 (the heap has 2048 at most) */
#define CPU_SRV_CHUNK 16384       /* textures' own descriptors, a heap of this many at a time */
#define CACHE_DIR "shader_cache"
#define CACHE_FILE CACHE_DIR "/d3d12.bin"
#define CONFIGS_FILE CACHE_DIR "/d3d12_configs.bin"
#define NCONST (PC_GX_CONFIG_COUNT + 1) /* specialization values: uber + the configuration */

typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT, REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_D3DCompile_)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                         UINT, UINT, ID3DBlob**, ID3DBlob**);
typedef HRESULT(WINAPI* PFN_D3DCreateBlob_)(SIZE_T, ID3DBlob**);
typedef HRESULT(WINAPI* PFN_SerializeRootSignature_)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
                                                     ID3DBlob**, ID3DBlob**);

static PFN_D3D12_CREATE_DEVICE pD3D12CreateDevice;
static PFN_SerializeRootSignature_ pD3D12SerializeRootSignature;
static PFN_D3D12_GET_DEBUG_INTERFACE pD3D12GetDebugInterface;
static PFN_CreateDXGIFactory2 pCreateDXGIFactory2;
static PFN_CreateDXGIFactory1 pCreateDXGIFactory1;
static PFN_D3DCompile_ s_D3DCompile;
static PFN_D3DCreateBlob_ s_D3DCreateBlob;

/* --- memory: heaps shared by many textures --- */

typedef struct { UINT64 off, size; } Range;

typedef struct {
    ID3D12Heap* heap;
    Range* free;
    int nfree, capfree;
} MemBlock;

typedef struct { int block; UINT64 off, size; } Alloc;

typedef struct {
    ID3D12Resource* res;
    Alloc mem;
    DXGI_FORMAT format;
    int w, h, levels;
    int srv;              /* its descriptor in the CPU heaps */
    uint64_t uid;         /* of this image (the table cache's key) */
    D3D12_RESOURCE_STATES state;
} DxTex;

typedef struct {
    ID3D12Resource* res;
    unsigned char* map;
    D3D12_GPU_VIRTUAL_ADDRESS gpu;
    UINT64 size, used;
} HostBuf;

typedef struct {
    uint64_t serial;
    ID3D12Resource* res;
    Alloc mem;
    int srv;
} Garbage;

typedef struct {
    ID3D12CommandAllocator* alloc;
    ID3D12CommandAllocator* upload_alloc;
    ID3D12GraphicsCommandList* cl;
    ID3D12GraphicsCommandList* upload;
    UINT64 fence_value;
    HostBuf stream, staging;
    HostBuf extra[8];
    int nextra, cur;
    uint64_t serial;
    int begun, upload_begun;
} Frame;

enum { KIND_UBER = 0, KIND_SPEC = 1, KIND_UI = 2 };

typedef struct {
    int kind;
    uint32_t rkey;
    PCGXShaderConfig config;
    uint32_t hash;
    ID3D12PipelineState* pso;
    SDL_atomic_t state; /* 0 empty, 1 queued, 2 ready, 3 failed */
} Pipe;

typedef struct {
    int n;                /* units: 5 (GX), 1 (UI); 0 = empty */
    uint64_t uid[5];
    int smp[5];
    int srv_table, smp_table;
    uint64_t used;
} TexSet;

typedef struct {
    uint64_t key;
    void* code;
    size_t size;
} Blob;

static struct {
    SDL_Window* window;
    HWND hwnd;
    HMODULE d3d12_dll, dxgi_dll, compiler_dll;
    IDXGIFactory4* factory;
    ID3D12Device* dev;
    ID3D12CommandQueue* queue;
    ID3D12Fence* fence;
    UINT64 fence_next;
    HANDLE fence_event;
    IDXGISwapChain3* swapchain;
    int tearing, vsync;
    UINT sc_flags;
    DXGI_FORMAT color_format;
    int width, height;
    ID3D12Resource* buffers[BUFFERS];
    ID3D12Resource *depth, *msaa;
    D3D12_RESOURCE_STATES bb_state;
    ID3D12DescriptorHeap *rtv_heap, *dsv_heap;
    UINT rtv_size, srv_size, smp_size;
    int samples, want_samples, swapchain_dirty;
    UINT bb;              /* the back buffer of the frame */

    Frame frames[FRAMES];
    int fi;
    uint64_t serial, done_serial;
    int need_clear;       /* the frame's picture is not cleared yet */

    MemBlock* blocks;
    int nblocks;
    Garbage* garbage;
    int ngarbage, capgarbage;

    /* descriptors */
    ID3D12DescriptorHeap* cpu_srv[64];
    int ncpu_srv;
    int* srv_free;
    int nsrv_free, capsrv_free, srv_next;
    ID3D12DescriptorHeap *srv_heap, *smp_heap, *smp_cpu;
    int srv_table_free[SRV_TABLES], nsrv_table_free;
    int smp_table_free[SMP_TABLES], nsmp_table_free;
    TexSet* texsets;
    int ntexsets;
    struct { int key[5]; int table; } smp_cache[SMP_TABLES];
    int nsmp_cache;

    ID3D12RootSignature *gx_root, *ui_root;
    ID3DBlob *gx_vs_uber, *gx_ps_uber, *ui_vs, *ui_ps;
    ID3D12Resource* quad_ib;
    ID3D12Resource* fan_ib;
    PCTex dummy, white;
    ID3D12Resource* readback;
    UINT64 readback_size;

    Pipe* pipes;
    int pipes_built;
    SDL_Thread* worker;
    SDL_mutex* lock;
    SDL_cond* wake;
    int* queue_jobs;
    int njobs, capjobs, quit;
    FILE* configs_out;
    FILE* cache_out;
    Blob* blobs;
    int nblobs, capblobs;
    uint64_t source_hash;

    /* what the command list has bound */
    ID3D12PipelineState* bound_pso;
    ID3D12RootSignature* bound_root;
    D3D12_VIEWPORT bound_vp;
    D3D12_RECT bound_scissor;
    D3D12_GPU_VIRTUAL_ADDRESS bound_cbv[3];
    int bound_srv_table, bound_smp_table;
    D3D_PRIMITIVE_TOPOLOGY bound_topo;
    D3D12_GPU_VIRTUAL_ADDRESS bound_ib;
    D3D12_GPU_VIRTUAL_ADDRESS blk_gpu[3]; /* where each block was written this frame (0: not yet) */
    uint64_t last_uid[5];
    int last_smp[5];
    TexSet* last_set;
    int rebind;
    int lost;

    /* the last RGBA upload, for the mipmaps that follow it */
    PCGfxTex* mip_src_tex;
    unsigned char* mip_src;
    size_t mip_src_cap;
} dx;

static PCGfxBackend s_backend;
static uint64_t s_next_uid;

static void dx_fail(const char* what, HRESULT hr);

#define DX_CHECK(call, what) do { HRESULT hr_ = (call); if (FAILED(hr_)) dx_fail(what, hr_); } while (0)

/* --- memory --- */

static void range_insert(MemBlock* b, UINT64 off, UINT64 size) {
    int i = 0;

    while (i < b->nfree && b->free[i].off < off) i++;
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

static int block_alloc(MemBlock* b, UINT64 size, UINT64 align, UINT64* out) {
    for (int i = 0; i < b->nfree; i++) {
        UINT64 start = (b->free[i].off + align - 1) / align * align;
        UINT64 end = b->free[i].off + b->free[i].size;

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

/* a texture in one of the shared heaps (or committed, when big) */
static ID3D12Resource* texture_resource(const D3D12_RESOURCE_DESC* desc_in, D3D12_RESOURCE_STATES state, Alloc* a) {
    D3D12_RESOURCE_DESC desc = *desc_in;
    D3D12_RESOURCE_ALLOCATION_INFO info;
    ID3D12Resource* res = NULL;

    memset(a, 0, sizeof(*a));
    a->block = -1;
    desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT; /* 4 KB, when the texture is small enough */
    info = ID3D12Device_GetResourceAllocationInfo(dx.dev, 0, 1, &desc);
    if (info.Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
        desc.Alignment = 0;
        info = ID3D12Device_GetResourceAllocationInfo(dx.dev, 0, 1, &desc);
    }
    if (info.SizeInBytes == UINT64_MAX) return NULL;
    if (info.SizeInBytes > BLOCK_SIZE / 4) {
        D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };

        desc.Alignment = 0;
        if (FAILED(ID3D12Device_CreateCommittedResource(dx.dev, &hp, D3D12_HEAP_FLAG_NONE, &desc, state, NULL,
                                                        &IID_ID3D12Resource, (void**)&res))) {
            return NULL;
        }
        return res;
    }
    for (int i = 0; i <= dx.nblocks; i++) {
        MemBlock* b;

        if (i == dx.nblocks) {
            D3D12_HEAP_DESC hd = { 0 };
            ID3D12Heap* heap = NULL;

            hd.SizeInBytes = BLOCK_SIZE;
            hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            hd.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
            if (FAILED(ID3D12Device_CreateHeap(dx.dev, &hd, &IID_ID3D12Heap, (void**)&heap))) return NULL;
            dx.blocks = (MemBlock*)realloc(dx.blocks, (size_t)(dx.nblocks + 1) * sizeof(MemBlock));
            b = &dx.blocks[dx.nblocks++];
            memset(b, 0, sizeof(*b));
            b->heap = heap;
            range_insert(b, 0, BLOCK_SIZE);
        }
        b = &dx.blocks[i];
        if (block_alloc(b, info.SizeInBytes, info.Alignment, &a->off)) {
            if (FAILED(ID3D12Device_CreatePlacedResource(dx.dev, b->heap, a->off, &desc, state, NULL,
                                                         &IID_ID3D12Resource, (void**)&res))) {
                range_insert(b, a->off, info.SizeInBytes);
                return NULL;
            }
            a->block = i;
            a->size = info.SizeInBytes;
            return res;
        }
    }
    return NULL;
}

static void alloc_free(Alloc* a) {
    if (a->block >= 0 && a->size > 0) range_insert(&dx.blocks[a->block], a->off, a->size);
    memset(a, 0, sizeof(*a));
    a->block = -1;
}

static int host_buffer(HostBuf* hb, UINT64 size, D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES hp = { type };
    D3D12_RESOURCE_DESC rd = { 0 };
    D3D12_RANGE none = { 0, 0 };
    void* map = NULL;

    memset(hb, 0, sizeof(*hb));
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(ID3D12Device_CreateCommittedResource(dx.dev, &hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                    type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST
                                                                                     : D3D12_RESOURCE_STATE_GENERIC_READ,
                                                    NULL, &IID_ID3D12Resource, (void**)&hb->res))) {
        return 0;
    }
    if (FAILED(ID3D12Resource_Map(hb->res, 0, type == D3D12_HEAP_TYPE_READBACK ? NULL : &none, &map))) {
        ID3D12Resource_Release(hb->res);
        hb->res = NULL;
        return 0;
    }
    hb->map = (unsigned char*)map;
    hb->gpu = ID3D12Resource_GetGPUVirtualAddress(hb->res);
    hb->size = size;
    return 1;
}

static void host_buffer_free(HostBuf* hb) {
    if (hb->res != NULL) ID3D12Resource_Release(hb->res);
    memset(hb, 0, sizeof(*hb));
}

/* --- descriptors of the textures (CPU heaps) --- */

static D3D12_CPU_DESCRIPTOR_HANDLE cpu_srv(int i) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dx.cpu_srv[i / CPU_SRV_CHUNK]);

    h.ptr += (SIZE_T)(i % CPU_SRV_CHUNK) * dx.srv_size;
    return h;
}

static int srv_alloc(void) {
    if (dx.nsrv_free > 0) return dx.srv_free[--dx.nsrv_free];
    if (dx.srv_next == dx.ncpu_srv * CPU_SRV_CHUNK) {
        D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CPU_SRV_CHUNK,
                                          D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };

        if (dx.ncpu_srv == 64 || FAILED(ID3D12Device_CreateDescriptorHeap(dx.dev, &hd, &IID_ID3D12DescriptorHeap,
                                                                          (void**)&dx.cpu_srv[dx.ncpu_srv]))) {
            return -1;
        }
        dx.ncpu_srv++;
    }
    return dx.srv_next++;
}

static void srv_release(int i) {
    if (i < 0) return;
    if (dx.nsrv_free == dx.capsrv_free) {
        dx.capsrv_free = dx.capsrv_free ? dx.capsrv_free * 2 : 1024;
        dx.srv_free = (int*)realloc(dx.srv_free, (size_t)dx.capsrv_free * sizeof(int));
    }
    dx.srv_free[dx.nsrv_free++] = i;
}

static void garbage_add(ID3D12Resource* res, Alloc mem, int srv) {
    if (dx.ngarbage == dx.capgarbage) {
        dx.capgarbage = dx.capgarbage ? dx.capgarbage * 2 : 64;
        dx.garbage = (Garbage*)realloc(dx.garbage, (size_t)dx.capgarbage * sizeof(Garbage));
    }
    dx.garbage[dx.ngarbage].serial = dx.serial;
    dx.garbage[dx.ngarbage].res = res;
    dx.garbage[dx.ngarbage].mem = mem;
    dx.garbage[dx.ngarbage].srv = srv;
    dx.ngarbage++;
}

static void garbage_collect(int all) {
    int j = 0;

    for (int i = 0; i < dx.ngarbage; i++) {
        Garbage* g = &dx.garbage[i];

        if (all || g->serial <= dx.done_serial) {
            if (g->res != NULL) ID3D12Resource_Release(g->res);
            alloc_free(&g->mem);
            srv_release(g->srv);
        } else {
            dx.garbage[j++] = *g;
        }
    }
    dx.ngarbage = j;
}

/* --- frames and command lists --- */

static void bound_reset(void) {
    dx.bound_pso = NULL;
    dx.bound_root = NULL;
    memset(&dx.bound_vp, 0, sizeof(dx.bound_vp));
    memset(&dx.bound_scissor, 0, sizeof(dx.bound_scissor));
    memset(dx.bound_cbv, 0, sizeof(dx.bound_cbv));
    dx.bound_srv_table = dx.bound_smp_table = -1;
    dx.bound_topo = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    dx.bound_ib = 0;
}

static D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle(void) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dx.rtv_heap);

    h.ptr += (SIZE_T)(dx.samples > 1 ? BUFFERS : dx.bb) * dx.rtv_size;
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle(void) {
    return ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dx.dsv_heap);
}

static void barrier(ID3D12GraphicsCommandList* cl, ID3D12Resource* res, D3D12_RESOURCE_STATES from,
                    D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b = { 0 };

    if (from == to) return;
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier(cl, 1, &b);
}

/* what a command list needs first: the heaps and the render target */
static void list_setup(ID3D12GraphicsCommandList* cl) {
    ID3D12DescriptorHeap* heaps[2] = { dx.srv_heap, dx.smp_heap };
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_handle(), dsv = dsv_handle();

    ID3D12GraphicsCommandList_SetDescriptorHeaps(cl, 2, heaps);
    ID3D12GraphicsCommandList_OMSetRenderTargets(cl, 1, &rtv, FALSE, &dsv);
    bound_reset();
}

static void swapchain_resize(void);

static void frame_begin(void) {
    Frame* f = &dx.frames[dx.fi];

    if (f->begun) return;
    if (dx.swapchain_dirty) swapchain_resize();
    if (ID3D12Fence_GetCompletedValue(dx.fence) < f->fence_value) {
        ID3D12Fence_SetEventOnCompletion(dx.fence, f->fence_value, dx.fence_event);
        WaitForSingleObject(dx.fence_event, INFINITE);
    }
    if (f->serial > dx.done_serial) dx.done_serial = f->serial;
    garbage_collect(0);
    DX_CHECK(ID3D12CommandAllocator_Reset(f->alloc), "CommandAllocator.Reset");
    DX_CHECK(ID3D12CommandAllocator_Reset(f->upload_alloc), "CommandAllocator.Reset");
    DX_CHECK(ID3D12GraphicsCommandList_Reset(f->cl, f->alloc, NULL), "CommandList.Reset");
    f->stream.used = 0;
    for (int i = 0; i < f->nextra; i++) f->extra[i].used = 0;
    f->cur = 0;
    f->staging.used = 0;
    f->serial = ++dx.serial;
    f->begun = 1;
    f->upload_begun = 0;
    dx.bb = IDXGISwapChain3_GetCurrentBackBufferIndex(dx.swapchain);
    if (dx.samples <= 1) {
        barrier(f->cl, dx.buffers[dx.bb], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        dx.bb_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    } else {
        dx.bb_state = D3D12_RESOURCE_STATE_PRESENT;
    }
    list_setup(f->cl);
    dx.need_clear = 1;
    memset(dx.blk_gpu, 0, sizeof(dx.blk_gpu));
    dx.last_set = NULL;
}

static ID3D12GraphicsCommandList* upload_list(void) {
    Frame* f;

    frame_begin();
    f = &dx.frames[dx.fi];
    if (!f->upload_begun) {
        DX_CHECK(ID3D12GraphicsCommandList_Reset(f->upload, f->upload_alloc, NULL), "CommandList.Reset");
        f->upload_begun = 1;
    }
    return f->upload;
}

static void wait_idle(void) {
    UINT64 v = ++dx.fence_next;

    DX_CHECK(ID3D12CommandQueue_Signal(dx.queue, dx.fence, v), "Queue.Signal");
    if (ID3D12Fence_GetCompletedValue(dx.fence) < v) {
        ID3D12Fence_SetEventOnCompletion(dx.fence, v, dx.fence_event);
        WaitForSingleObject(dx.fence_event, INFINITE);
    }
}

/* the frame's lists so far go to the GPU and are waited for (glReadPixels, a
 * full buffer); recording goes on in the same lists, reset */
static void submit(int last);

static void flush_and_wait(void) {
    Frame* f = &dx.frames[dx.fi];

    if (!f->begun) return;
    dx.rebind = 1;
    submit(0);
    wait_idle();
    DX_CHECK(ID3D12CommandAllocator_Reset(f->alloc), "CommandAllocator.Reset");
    DX_CHECK(ID3D12CommandAllocator_Reset(f->upload_alloc), "CommandAllocator.Reset");
    DX_CHECK(ID3D12GraphicsCommandList_Reset(f->cl, f->alloc, NULL), "CommandList.Reset");
    f->upload_begun = 0;
    f->staging.used = 0;
    list_setup(f->cl);
}

static void submit(int last) {
    Frame* f = &dx.frames[dx.fi];
    ID3D12CommandList* lists[2];
    UINT n = 0;

    (void)last;
    if (f->upload_begun) {
        DX_CHECK(ID3D12GraphicsCommandList_Close(f->upload), "CommandList.Close");
        lists[n++] = (ID3D12CommandList*)f->upload;
        f->upload_begun = 0;
    }
    DX_CHECK(ID3D12GraphicsCommandList_Close(f->cl), "CommandList.Close");
    lists[n++] = (ID3D12CommandList*)f->cl;
    ID3D12CommandQueue_ExecuteCommandLists(dx.queue, n, lists);
}

static HostBuf* frame_buf(Frame* f, int i) {
    return i == 0 ? &f->stream : &f->extra[i - 1];
}

/* n bytes of the frame's streaming buffers */
static unsigned char* stream_alloc(UINT64 n, UINT64 align, D3D12_GPU_VIRTUAL_ADDRESS* gpu) {
    Frame* f = &dx.frames[dx.fi];
    HostBuf* b = frame_buf(f, f->cur);
    UINT64 start = (b->used + align - 1) / align * align;

    while (start + n > b->size) {
        if (f->cur < f->nextra) {
            b = frame_buf(f, ++f->cur);
        } else if (f->nextra < 8) {
            if (!host_buffer(&f->extra[f->nextra], n > STREAM_SIZE ? n * 2 : STREAM_SIZE, D3D12_HEAP_TYPE_UPLOAD)) {
                dx_fail("streaming buffer", E_OUTOFMEMORY);
                return NULL;
            }
            f->nextra++;
            b = frame_buf(f, ++f->cur);
        } else {
            flush_and_wait();
            f->stream.used = 0;
            for (int i = 0; i < f->nextra; i++) f->extra[i].used = 0;
            f->cur = 0;
            b = &f->stream;
            memset(dx.blk_gpu, 0, sizeof(dx.blk_gpu));
            if (n > b->size) return NULL;
        }
        start = 0;
    }
    b->used = start + n;
    *gpu = b->gpu + start;
    return b->map + start;
}

/* staging space for an upload: the frame's, or a buffer of its own */
static unsigned char* staging_alloc(UINT64 n, UINT64 align, ID3D12Resource** res, UINT64* off) {
    Frame* f;

    frame_begin();
    f = &dx.frames[dx.fi];
    if (n > STAGING_SIZE / 2) {
        HostBuf hb;
        Alloc none = { -1, 0, 0 };

        if (!host_buffer(&hb, n, D3D12_HEAP_TYPE_UPLOAD)) return NULL;
        garbage_add(hb.res, none, -1); /* freed after this frame */
        *res = hb.res;
        *off = 0;
        return hb.map;
    }
    if (((f->staging.used + align - 1) & ~(align - 1)) + n > f->staging.size) flush_and_wait();
    *off = (f->staging.used + align - 1) & ~(align - 1);
    f->staging.used = *off + n;
    *res = f->staging.res;
    return f->staging.map + *off;
}

/* --- swapchain and render targets --- */

static int targets_create(void) {
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd = { 0 };
    D3D12_CLEAR_VALUE cv = { 0 };
    D3D12_DEPTH_STENCIL_VIEW_DESC dv = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dx.rtv_heap);

    for (UINT i = 0; i < BUFFERS; i++) {
        D3D12_CPU_DESCRIPTOR_HANDLE hi = h;

        if (FAILED(IDXGISwapChain3_GetBuffer(dx.swapchain, i, &IID_ID3D12Resource, (void**)&dx.buffers[i]))) return 0;
        hi.ptr += (SIZE_T)i * dx.rtv_size;
        ID3D12Device_CreateRenderTargetView(dx.dev, dx.buffers[i], NULL, hi);
    }
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)dx.width;
    rd.Height = (UINT)dx.height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = (UINT)(dx.samples > 1 ? dx.samples : 1);
    if (dx.samples > 1) {
        D3D12_CPU_DESCRIPTOR_HANDLE hm = h;

        rd.Format = dx.color_format;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        cv.Format = dx.color_format;
        if (FAILED(ID3D12Device_CreateCommittedResource(dx.dev, &hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                        D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, &IID_ID3D12Resource,
                                                        (void**)&dx.msaa))) {
            return 0;
        }
        hm.ptr += (SIZE_T)BUFFERS * dx.rtv_size;
        ID3D12Device_CreateRenderTargetView(dx.dev, dx.msaa, NULL, hm);
    }
    rd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    memset(&cv, 0, sizeof(cv));
    cv.Format = rd.Format;
    cv.DepthStencil.Depth = 1.0f;
    if (FAILED(ID3D12Device_CreateCommittedResource(dx.dev, &hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, &IID_ID3D12Resource,
                                                    (void**)&dx.depth))) {
        return 0;
    }
    dv.Format = rd.Format;
    dv.ViewDimension = dx.samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateDepthStencilView(dx.dev, dx.depth, &dv, dsv_handle());
    return 1;
}

static void targets_release(void) {
    for (int i = 0; i < BUFFERS; i++) {
        if (dx.buffers[i] != NULL) ID3D12Resource_Release(dx.buffers[i]);
        dx.buffers[i] = NULL;
    }
    if (dx.depth != NULL) ID3D12Resource_Release(dx.depth);
    if (dx.msaa != NULL) ID3D12Resource_Release(dx.msaa);
    dx.depth = dx.msaa = NULL;
}

static void window_pixels(int* w, int* h) {
    SDL_GetWindowSizeInPixels(dx.window, w, h);
}

static void swapchain_resize(void) {
    int w = 0, h = 0;

    dx.swapchain_dirty = 0;
    window_pixels(&w, &h);
    if (w <= 0 || h <= 0 || (w == dx.width && h == dx.height)) return; /* (minimized) */
    wait_idle();
    targets_release();
    dx.width = w;
    dx.height = h;
    DX_CHECK(IDXGISwapChain3_ResizeBuffers(dx.swapchain, BUFFERS, (UINT)w, (UINT)h, dx.color_format, dx.sc_flags),
             "SwapChain.ResizeBuffers");
    if (!targets_create()) dx_fail("render targets", E_FAIL);
}

static void clear_now(int color, int depth, const float rgba[4], float depth_value, const D3D12_RECT* rect) {
    Frame* f = &dx.frames[dx.fi];

    if (color) ID3D12GraphicsCommandList_ClearRenderTargetView(f->cl, rtv_handle(), rgba, rect ? 1 : 0, rect);
    if (depth) {
        ID3D12GraphicsCommandList_ClearDepthStencilView(f->cl, dsv_handle(), D3D12_CLEAR_FLAG_DEPTH, depth_value, 0,
                                                        rect ? 1 : 0, rect);
    }
}

/* the frame's picture starts black (OpenGL leaves it undefined after a swap) */
static void initial_clear(void) {
    static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    if (dx.need_clear) {
        clear_now(1, 1, black, 1.0f, NULL);
        dx.need_clear = 0;
    }
}

/* the MSAA picture resolved into the back buffer, which is then in state 'to' */
static void resolve(ID3D12GraphicsCommandList* cl, D3D12_RESOURCE_STATES to) {
    barrier(cl, dx.msaa, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    barrier(cl, dx.buffers[dx.bb], dx.bb_state, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    ID3D12GraphicsCommandList_ResolveSubresource(cl, dx.buffers[dx.bb], 0, dx.msaa, 0, dx.color_format);
    barrier(cl, dx.msaa, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barrier(cl, dx.buffers[dx.bb], D3D12_RESOURCE_STATE_RESOLVE_DEST, to);
    dx.bb_state = to;
}

static void dx_present(void) {
    Frame* f = &dx.frames[dx.fi];
    HRESULT hr;
    UINT flags = 0;

    frame_begin();
    initial_clear(); /* (a frame that drew nothing shows black) */
    if (dx.samples > 1) {
        resolve(f->cl, D3D12_RESOURCE_STATE_PRESENT);
    } else {
        barrier(f->cl, dx.buffers[dx.bb], dx.bb_state, D3D12_RESOURCE_STATE_PRESENT);
        dx.bb_state = D3D12_RESOURCE_STATE_PRESENT;
    }
    submit(1);
    if (!dx.vsync && dx.tearing) flags |= DXGI_PRESENT_ALLOW_TEARING;
    hr = IDXGISwapChain3_Present(dx.swapchain, dx.vsync ? 1 : 0, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) dx_fail("SwapChain.Present", hr);
    f->fence_value = ++dx.fence_next;
    DX_CHECK(ID3D12CommandQueue_Signal(dx.queue, dx.fence, f->fence_value), "Queue.Signal");
    f->begun = 0;
    dx.fi = (dx.fi + 1) % FRAMES;
    {
        int w = 0, h = 0;

        window_pixels(&w, &h);
        if (w != dx.width || h != dx.height) dx.swapchain_dirty = 1;
    }
}

/* --- textures --- */

static DXGI_FORMAT tex_format(int format) {
    switch (format) {
        case PC_TEXF_BC1: return DXGI_FORMAT_BC1_UNORM;
        case PC_TEXF_BC3: return DXGI_FORMAT_BC3_UNORM;
        case PC_TEXF_BC7: return DXGI_FORMAT_BC7_UNORM;
        default:          return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

static void tex_release(DxTex* t) {
    if (t->res != NULL) garbage_add(t->res, t->mem, t->srv);
    t->res = NULL;
    t->srv = -1;
    memset(&t->mem, 0, sizeof(t->mem));
    t->mem.block = -1;
}

static int is_block_format(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_BC1_UNORM || f == DXGI_FORMAT_BC3_UNORM || f == DXGI_FORMAT_BC7_UNORM;
}

/* a new image with its levels' data: levels[i] rows of (w >> i) pixels */
static int tex_make(DxTex* t, DXGI_FORMAT format, int w, int h, int nlevels, const unsigned char* const* levels,
                    const int* level_rows_bytes) {
    D3D12_RESOURCE_DESC rd = { 0 };
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[16];
    UINT rows[16];
    UINT64 row_bytes[16], total = 0;
    ID3D12Resource* src = NULL;
    UINT64 off = 0;
    unsigned char* dst;
    ID3D12GraphicsCommandList* cl;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = { 0 };

    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)w;
    rd.Height = (UINT)h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = (UINT16)nlevels;
    rd.Format = format;
    rd.SampleDesc.Count = 1;
    t->res = texture_resource(&rd, D3D12_RESOURCE_STATE_COPY_DEST, &t->mem);
    if (t->res == NULL) return 0;
    t->srv = srv_alloc();
    if (t->srv < 0) {
        tex_release(t);
        return 0;
    }
    sv.Format = format;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = (UINT)nlevels;
    ID3D12Device_CreateShaderResourceView(dx.dev, t->res, &sv, cpu_srv(t->srv));
    ID3D12Device_GetCopyableFootprints(dx.dev, &rd, 0, (UINT)nlevels, 0, fp, rows, row_bytes, &total);
    dst = staging_alloc(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &src, &off);
    if (dst == NULL) {
        tex_release(t);
        return 0;
    }
    cl = upload_list();
    for (int l = 0; l < nlevels; l++) {
        D3D12_TEXTURE_COPY_LOCATION to = { t->res, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX };
        D3D12_TEXTURE_COPY_LOCATION from = { src, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT };
        size_t n = (size_t)row_bytes[l];

        if (level_rows_bytes != NULL && (size_t)level_rows_bytes[l] < n) n = (size_t)level_rows_bytes[l];
        for (UINT r = 0; r < rows[l]; r++) {
            unsigned char* d = dst + fp[l].Offset + (UINT64)r * fp[l].Footprint.RowPitch;

            if (levels != NULL && levels[l] != NULL) memcpy(d, levels[l] + (size_t)r * n, n);
            else memset(d, 0, (size_t)row_bytes[l]);
        }
        to.SubresourceIndex = (UINT)l;
        from.PlacedFootprint = fp[l];
        from.PlacedFootprint.Offset += off;
        ID3D12GraphicsCommandList_CopyTextureRegion(cl, &to, 0, 0, 0, &from, NULL);
    }
    barrier(cl, t->res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    t->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    t->format = format;
    t->w = w;
    t->h = h;
    t->levels = nlevels;
    t->uid = ++s_next_uid;
    return 1;
}

static int dx_tex_image(PCGfxTex* o, int w, int h, int format, const void* data, size_t size) {
    DxTex* t = (DxTex*)o->impl;
    DXGI_FORMAT df = tex_format(format);
    const unsigned char* lv[1];
    int lb[1];
    unsigned char* conv = NULL;

    if (w <= 0 || h <= 0) return 0;
    if (t == NULL) {
        t = (DxTex*)calloc(1, sizeof(DxTex));
        if (t == NULL) return 0;
        t->srv = -1;
        t->mem.block = -1;
        o->impl = t;
    }
    if (t->res != NULL) tex_release(t);
    if (format == PC_TEXF_RGB565) {
        conv = (unsigned char*)malloc((size_t)w * h * 4);
        if (conv == NULL) return 0;
        if (data != NULL) pc_gfx_rgb565_to_rgba((const unsigned short*)data, conv, w * h);
        else memset(conv, 0, (size_t)w * h * 4);
        data = conv;
    }
    lv[0] = (const unsigned char*)data;
    lb[0] = is_block_format(df) ? ((w + 3) / 4) * (df == DXGI_FORMAT_BC1_UNORM ? 8 : 16) : w * 4;
    (void)size;
    if (!tex_make(t, df, w, h, 1, lv, lb)) {
        free(conv);
        return 0;
    }
    o->w = w;
    o->h = h;
    o->format = format;
    o->levels = 1;
    /* kept for the mipmaps that may follow (the HD textures) */
    dx.mip_src_tex = NULL;
    if (df == DXGI_FORMAT_R8G8B8A8_UNORM && data != NULL) {
        size_t n = (size_t)w * h * 4;

        if (dx.mip_src_cap < n) {
            unsigned char* p = (unsigned char*)realloc(dx.mip_src, n);

            if (p != NULL) {
                dx.mip_src = p;
                dx.mip_src_cap = n;
            }
        }
        if (dx.mip_src_cap >= n) {
            memcpy(dx.mip_src, data, n);
            dx.mip_src_tex = o;
        }
    }
    free(conv);
    return 1;
}

static void dx_tex_subimage(PCGfxTex* o, int x, int y, int w, int h, int row_length, const void* rgba) {
    DxTex* t = (DxTex*)o->impl;
    UINT64 pitch = ((UINT64)w * 4 + 255) & ~(UINT64)255, off;
    ID3D12Resource* src;
    unsigned char* dst;
    ID3D12GraphicsCommandList* cl;
    D3D12_TEXTURE_COPY_LOCATION to, from;

    if (t == NULL || t->res == NULL || t->format != DXGI_FORMAT_R8G8B8A8_UNORM || w <= 0 || h <= 0) return;
    dst = staging_alloc(pitch * (UINT64)h, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &src, &off);
    if (dst == NULL) return;
    for (int r = 0; r < h; r++) {
        memcpy(dst + (size_t)r * pitch, (const unsigned char*)rgba + (size_t)r * row_length * 4, (size_t)w * 4);
    }
    cl = upload_list();
    barrier(cl, t->res, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    memset(&to, 0, sizeof(to));
    memset(&from, 0, sizeof(from));
    to.pResource = t->res;
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource = src;
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint.Offset = off;
    from.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    from.PlacedFootprint.Footprint.Width = (UINT)w;
    from.PlacedFootprint.Footprint.Height = (UINT)h;
    from.PlacedFootprint.Footprint.Depth = 1;
    from.PlacedFootprint.Footprint.RowPitch = (UINT)pitch;
    ID3D12GraphicsCommandList_CopyTextureRegion(cl, &to, (UINT)x, (UINT)y, 0, &from, NULL);
    barrier(cl, t->res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (dx.mip_src_tex == o) dx.mip_src_tex = NULL;
}

/* mipmaps (as glGenerateMipmap): each level the average of 2x2 of the one
 * before, made on the CPU from the image just uploaded */
static void dx_tex_mipmap(PCGfxTex* o) {
    DxTex* t = (DxTex*)o->impl;
    DxTex n;
    const unsigned char* lv[16];
    int lb[16];
    unsigned char* buf;
    size_t total = 0;
    int levels = 1, w, h;

    if (t == NULL || t->res == NULL || t->format != DXGI_FORMAT_R8G8B8A8_UNORM || dx.mip_src_tex != o) return;
    while (levels < 16 && ((t->w >> levels) > 0 || (t->h >> levels) > 0)) levels++;
    if (levels == 1) return;
    for (int l = 1; l < levels; l++) {
        int lw = (t->w >> l) > 0 ? t->w >> l : 1, lh = (t->h >> l) > 0 ? t->h >> l : 1;

        total += (size_t)lw * lh * 4;
    }
    buf = (unsigned char*)malloc(total);
    if (buf == NULL) return;
    lv[0] = dx.mip_src;
    lb[0] = t->w * 4;
    w = t->w;
    h = t->h;
    {
        unsigned char* p = buf;

        for (int l = 1; l < levels; l++) {
            int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
            const unsigned char* s = lv[l - 1];

            for (int y = 0; y < nh; y++) {
                int y0 = h > 1 ? y * 2 : 0, y1 = h > 1 ? y * 2 + 1 : 0;

                for (int x = 0; x < nw; x++) {
                    int x0 = w > 1 ? x * 2 : 0, x1 = w > 1 ? x * 2 + 1 : 0;

                    for (int c = 0; c < 4; c++) {
                        unsigned v = (unsigned)s[((size_t)y0 * w + x0) * 4 + c] + s[((size_t)y0 * w + x1) * 4 + c] +
                                     s[((size_t)y1 * w + x0) * 4 + c] + s[((size_t)y1 * w + x1) * 4 + c];

                        p[((size_t)y * nw + x) * 4 + c] = (unsigned char)((v + 2) / 4);
                    }
                }
            }
            lv[l] = p;
            lb[l] = nw * 4;
            p += (size_t)nw * nh * 4;
            w = nw;
            h = nh;
        }
    }
    memset(&n, 0, sizeof(n));
    n.srv = -1;
    n.mem.block = -1;
    if (tex_make(&n, DXGI_FORMAT_R8G8B8A8_UNORM, t->w, t->h, levels, lv, lb)) {
        tex_release(t);
        *t = n;
        o->levels = levels;
    }
    free(buf);
    dx.mip_src_tex = NULL;
}

static void dx_tex_free(PCGfxTex* o) {
    DxTex* t = (DxTex*)o->impl;

    if (t == NULL) return;
    if (t->res != NULL) tex_release(t);
    if (dx.mip_src_tex == o) dx.mip_src_tex = NULL;
    free(t);
    o->impl = NULL;
}

/* the sampler of a texture's modes: one of 3 x 3 x 3 x 2 */
static int sampler_index(const PCGfxTex* o) {
    return ((o->wrap_s % 3) * 3 + (o->wrap_t % 3)) * 6 + (o->min_filter % 3) * 2 + (o->mag_filter ? 1 : 0);
}

static void samplers_create(void) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dx.smp_cpu);
    static const D3D12_TEXTURE_ADDRESS_MODE modes[3] = { D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                                                         D3D12_TEXTURE_ADDRESS_MODE_MIRROR };

    for (int ws = 0; ws < 3; ws++)
        for (int wt = 0; wt < 3; wt++)
            for (int mn = 0; mn < 3; mn++)
                for (int mg = 0; mg < 2; mg++) {
                    D3D12_SAMPLER_DESC sd = { 0 };
                    D3D12_CPU_DESCRIPTOR_HANDLE hi = h;
                    int mip = mn == PC_FILTER_LINEAR_MIPMAP_LINEAR;
                    int min_lin = mn != PC_FILTER_NEAREST;

                    /* D3D12_FILTER: min (bit 4), mag (bit 2), mip (bit 0) linear */
                    sd.Filter = (D3D12_FILTER)((min_lin ? 0x10 : 0) | (mg ? 0x4 : 0) | (mip ? 0x1 : 0));
                    sd.AddressU = modes[ws];
                    sd.AddressV = modes[wt];
                    sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    sd.MaxAnisotropy = 1;
                    sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
                    sd.MaxLOD = mip ? D3D12_FLOAT32_MAX : 0.0f;
                    hi.ptr += (SIZE_T)(((ws * 3 + wt) * 6) + mn * 2 + mg) * dx.smp_size;
                    ID3D12Device_CreateSampler(dx.dev, &sd, hi);
                }
}

/* what a unit samples: the texture, or black for none; returns its uid */
static uint64_t unit_image(PCTex name, int* srv, int* smp) {
    PCGfxTex* o = pc_gfx_tex_obj(name);
    DxTex* t = o != NULL ? (DxTex*)o->impl : NULL;

    if (t == NULL || t->res == NULL) {
        o = pc_gfx_tex_obj(dx.dummy);
        t = (DxTex*)o->impl;
    }
    *srv = t->srv;
    *smp = sampler_index(o);
    return t->uid;
}

/* --- descriptor tables, kept across frames --- */

static D3D12_GPU_DESCRIPTOR_HANDLE table_gpu(ID3D12DescriptorHeap* heap, int table, UINT size) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);

    h.ptr += (UINT64)table * 5 * size;
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE table_cpu(ID3D12DescriptorHeap* heap, int table, UINT size) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);

    h.ptr += (SIZE_T)table * 5 * size;
    return h;
}

/* the sampler table of a combination (few: kept for good) */
static int smp_table(int n, const int* smp) {
    int key[5] = { 0, 0, 0, 0, 0 };
    int t;

    memcpy(key, smp, (size_t)n * sizeof(int));
    for (int i = 0; i < dx.nsmp_cache; i++) {
        if (memcmp(dx.smp_cache[i].key, key, sizeof(key)) == 0) return dx.smp_cache[i].table;
    }
    if (dx.nsmp_cache == SMP_TABLES) {
        /* (more combinations than ever seen: start over once the GPU is done) */
        flush_and_wait();
        dx.nsmp_cache = 0;
        dx.last_set = NULL;
        for (int i = 0; i < TEXSETS; i++) dx.texsets[i].smp_table = -1;
    }
    t = dx.nsmp_cache;
    for (int i = 0; i < 5; i++) {
        D3D12_CPU_DESCRIPTOR_HANDLE d = table_cpu(dx.smp_heap, t, dx.smp_size);
        D3D12_CPU_DESCRIPTOR_HANDLE s = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dx.smp_cpu);

        d.ptr += (SIZE_T)i * dx.smp_size;
        s.ptr += (SIZE_T)key[i] * dx.smp_size;
        ID3D12Device_CopyDescriptorsSimple(dx.dev, 1, d, s, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }
    memcpy(dx.smp_cache[t].key, key, sizeof(key));
    dx.smp_cache[t].table = t;
    dx.nsmp_cache++;
    return t;
}

static uint32_t texset_hash(int n, const uint64_t* uid, const int* smp) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t)n;

    for (int i = 0; i < n; i++) {
        h = (h ^ uid[i]) * 1099511628211ull;
        h = (h ^ (uint64_t)smp[i]) * 1099511628211ull;
    }
    return (uint32_t)(h ^ (h >> 32));
}

static TexSet* texset_slot(int n, const uint64_t* uid, const int* smp, int* found) {
    uint32_t h = texset_hash(n, uid, smp);

    for (uint32_t i = 0; i < TEXSETS; i++) {
        TexSet* e = &dx.texsets[(h + i) & (TEXSETS - 1)];

        if (e->n == 0) {
            *found = 0;
            return e;
        }
        if (e->n == n && memcmp(e->uid, uid, (size_t)n * sizeof(uint64_t)) == 0 &&
            memcmp(e->smp, smp, (size_t)n * sizeof(int)) == 0) {
            *found = 1;
            return e;
        }
    }
    *found = 0;
    return NULL;
}

/* frees the tables no frame in flight uses and none drew with lately (or,
 * when that is none, every one the GPU is done with) */
static int texset_sweep(void) {
    TexSet* keep = (TexSet*)malloc(TEXSETS * sizeof(TexSet));
    int nkeep = 0, ndrop = 0;

    if (keep == NULL) return 0;
    for (int age = 120; age >= 0 && ndrop == 0; age = age > 0 ? 0 : -1) {
        ndrop = 0;
        for (int i = 0; i < TEXSETS; i++) {
            TexSet* e = &dx.texsets[i];

            if (e->n != 0 && e->used <= dx.done_serial && e->used + (uint64_t)age < dx.serial) ndrop++;
        }
        if (ndrop == 0) continue;
        nkeep = 0;
        for (int i = 0; i < TEXSETS; i++) {
            TexSet* e = &dx.texsets[i];

            if (e->n == 0) continue;
            if (e->used <= dx.done_serial && e->used + (uint64_t)age < dx.serial) {
                dx.srv_table_free[dx.nsrv_table_free++] = e->srv_table;
            } else {
                keep[nkeep++] = *e;
            }
        }
    }
    if (ndrop > 0) {
        memset(dx.texsets, 0, TEXSETS * sizeof(TexSet));
        for (int i = 0; i < nkeep; i++) {
            int found;
            TexSet* e = texset_slot(keep[i].n, keep[i].uid, keep[i].smp, &found);

            *e = keep[i];
        }
        dx.ntexsets = nkeep;
        dx.last_set = NULL;
    }
    free(keep);
    return ndrop;
}

static TexSet* tex_set(int n, const uint64_t* uid, const int* srv, const int* smp) {
    TexSet* e;
    int found;

    e = texset_slot(n, uid, smp, &found);
    if (found) {
        e->used = dx.serial;
        if (e->smp_table < 0) e->smp_table = smp_table(n, smp);
        return e;
    }
    if (dx.ntexsets >= TEXSETS * 3 / 4 || dx.nsrv_table_free == 0) {
        if (texset_sweep() == 0) {
            /* all in use lately: wait for the GPU and start over */
            flush_and_wait();
            dx.done_serial = dx.serial;
            texset_sweep();
        }
        e = texset_slot(n, uid, smp, &found);
    }
    if (e == NULL || dx.nsrv_table_free == 0) return NULL;
    e->n = n;
    memcpy(e->uid, uid, (size_t)n * sizeof(uint64_t));
    memcpy(e->smp, smp, (size_t)n * sizeof(int));
    e->srv_table = dx.srv_table_free[--dx.nsrv_table_free];
    e->smp_table = smp_table(n, smp);
    e->used = dx.serial;
    dx.ntexsets++;
    for (int i = 0; i < n; i++) {
        D3D12_CPU_DESCRIPTOR_HANDLE d = table_cpu(dx.srv_heap, e->srv_table, dx.srv_size);

        d.ptr += (SIZE_T)i * dx.srv_size;
        ID3D12Device_CopyDescriptorsSimple(dx.dev, 1, d, cpu_srv(srv[i]), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    return e;
}

/* --- shaders and pipelines --- */

static uint64_t hash64(const void* p, size_t n, uint64_t h) {
    const unsigned char* b = (const unsigned char*)p;

    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

static uint64_t blob_key(int stage, int uber, const PCGXShaderConfig* c) {
    uint64_t h = hash64(&stage, sizeof(stage), 1469598103934665603ull);

    h = hash64(&uber, sizeof(uber), h);
    if (c != NULL) h = hash64(c, sizeof(*c), h);
    return h;
}

static const Blob* blob_find(uint64_t key) {
    for (int i = 0; i < dx.nblobs; i++) {
        if (dx.blobs[i].key == key) return &dx.blobs[i];
    }
    return NULL;
}

static void blob_add(uint64_t key, const void* code, size_t size, int save) {
    Blob* b;

    if (dx.nblobs == dx.capblobs) {
        dx.capblobs = dx.capblobs ? dx.capblobs * 2 : 256;
        dx.blobs = (Blob*)realloc(dx.blobs, (size_t)dx.capblobs * sizeof(Blob));
    }
    b = &dx.blobs[dx.nblobs];
    b->code = malloc(size);
    if (b->code == NULL) return;
    memcpy(b->code, code, size);
    b->size = size;
    b->key = key;
    dx.nblobs++;
    if (save && dx.cache_out != NULL) {
        uint32_t n = (uint32_t)size;

        fwrite(&key, sizeof(key), 1, dx.cache_out);
        fwrite(&n, sizeof(n), 1, dx.cache_out);
        fwrite(code, 1, size, dx.cache_out);
        fflush(dx.cache_out);
    }
}

static void cache_load(void) {
    FILE* f = fopen(CACHE_FILE, "rb");
    uint64_t head[2] = { 0x31424344u /* "DCB1" */, dx.source_hash }, got[2];
    int good = 0;

    if (f != NULL) {
        good = fread(got, sizeof(got), 1, f) == 1 && got[0] == head[0] && got[1] == head[1];
        while (good) {
            uint64_t key;
            uint32_t n;
            void* code;

            if (fread(&key, sizeof(key), 1, f) != 1 || fread(&n, sizeof(n), 1, f) != 1 || n > (16u << 20)) break;
            code = malloc(n);
            if (code == NULL || fread(code, 1, n, f) != n) {
                free(code);
                break;
            }
            blob_add(key, code, n, 0);
            free(code);
        }
        fclose(f);
    }
    dx.cache_out = fopen(CACHE_FILE, good ? "ab" : "wb");
    if (dx.cache_out != NULL && !good) {
        fwrite(head, sizeof(head), 1, dx.cache_out);
        fflush(dx.cache_out);
    }
}

/* the bytecode of a shader: from the cache, or compiled now (and kept) */
static int shader_code(int stage, int uber, const PCGXShaderConfig* c, const void** code, size_t* size) {
    static char names[NCONST][32];
    static int named = 0;
    D3D_SHADER_MACRO defs[NCONST + 1];
    char values[NCONST][16];
    uint64_t key = blob_key(stage, uber, c);
    const Blob* b;
    ID3DBlob *out = NULL, *err = NULL;
    const char* src = stage == 0 ? pc_shader_gx_vert_hlsl : pc_shader_gx_frag_hlsl;
    HRESULT hr;

    SDL_LockMutex(dx.lock);
    b = blob_find(key);
    if (b != NULL) {
        *code = b->code;
        *size = b->size;
        SDL_UnlockMutex(dx.lock);
        return 1;
    }
    if (!named) {
        for (int i = 0; i < NCONST; i++) snprintf(names[i], sizeof(names[i]), "SPIRV_CROSS_CONSTANT_ID_%d", i);
        named = 1;
    }
    SDL_UnlockMutex(dx.lock);
    for (int i = 0; i < NCONST; i++) {
        snprintf(values[i], sizeof(values[i]), "%d", i == 0 ? uber : c != NULL ? c->v[i - 1] : 0);
        defs[i].Name = names[i];
        defs[i].Definition = values[i];
    }
    defs[NCONST].Name = NULL;
    defs[NCONST].Definition = NULL;
    hr = s_D3DCompile(src, strlen(src), stage == 0 ? "gx.vert" : "gx.frag", defs, NULL, "main",
                     stage == 0 ? "vs_5_0" : "ps_5_0",
                     uber ? D3DCOMPILE_OPTIMIZATION_LEVEL1 : D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &err);
    if (FAILED(hr) || out == NULL) {
        if (err != NULL) {
            fprintf(stderr, "[PC/D3D12] shader: %.1500s\n", (const char*)ID3D10Blob_GetBufferPointer(err));
            ID3D10Blob_Release(err);
        }
        return 0;
    }
    if (err != NULL) ID3D10Blob_Release(err);
    SDL_LockMutex(dx.lock);
    blob_add(key, ID3D10Blob_GetBufferPointer(out), ID3D10Blob_GetBufferSize(out), 1);
    b = blob_find(key);
    if (b != NULL) {
        *code = b->code; /* (the code stays where it is; the table may move) */
        *size = b->size;
    }
    SDL_UnlockMutex(dx.lock);
    ID3D10Blob_Release(out);
    return b != NULL;
}

static ID3DBlob* compile_text(const char* src, const char* name, const char* target) {
    ID3DBlob *out = NULL, *err = NULL;

    if (FAILED(s_D3DCompile(src, strlen(src), name, NULL, NULL, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out,
                           &err))) {
        if (err != NULL) fprintf(stderr, "[PC/D3D12] %s: %.1500s\n", name, (const char*)ID3D10Blob_GetBufferPointer(err));
        out = NULL;
    }
    if (err != NULL) ID3D10Blob_Release(err);
    return out;
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE topo_type(int prim) {
    switch (prim) {
        case PC_PRIM_LINES:
        case PC_PRIM_LINE_STRIP: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case PC_PRIM_POINTS:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        default:                 return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

static D3D_PRIMITIVE_TOPOLOGY topology(int prim) {
    switch (prim) {
        case PC_PRIM_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case PC_PRIM_LINES:          return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case PC_PRIM_LINE_STRIP:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case PC_PRIM_POINTS:         return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        default:                     return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; /* (quads and fans: indexed) */
    }
}

/* the fixed-function state of the moment as a key */
static uint32_t raster_key(D3D12_PRIMITIVE_TOPOLOGY_TYPE type) {
    const PCGfxState* s = &g_gfx_state;
    uint32_t k = (uint32_t)type & 7;

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

static D3D12_BLEND blend_factor(uint32_t f, int alpha) {
    static const D3D12_BLEND color[10] = { D3D12_BLEND_ZERO, D3D12_BLEND_ONE, D3D12_BLEND_SRC_COLOR,
                                           D3D12_BLEND_INV_SRC_COLOR, D3D12_BLEND_DEST_COLOR, D3D12_BLEND_INV_DEST_COLOR,
                                           D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_DEST_ALPHA,
                                           D3D12_BLEND_INV_DEST_ALPHA };
    /* (for alpha a color factor is its alpha, as in OpenGL) */
    static const D3D12_BLEND alphas[10] = { D3D12_BLEND_ZERO, D3D12_BLEND_ONE, D3D12_BLEND_SRC_ALPHA,
                                            D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA,
                                            D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_DEST_ALPHA,
                                            D3D12_BLEND_INV_DEST_ALPHA };
    return f < 10 ? (alpha ? alphas[f] : color[f]) : D3D12_BLEND_ONE;
}

static ID3D12PipelineState* build_pipeline(int kind, uint32_t rkey, const PCGXShaderConfig* config) {
    static const D3D12_INPUT_ELEMENT_DESC gx_in[4] = {
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(PCGXVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(PCGXVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(PCGXVertex, color0), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(PCGXVertex, texcoord), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    static const D3D12_INPUT_ELEMENT_DESC ui_in[5] = {
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(PCGfxUIVertex, x), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(PCGfxUIVertex, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(PCGfxUIVertex, lx), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 3, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(PCGfxUIVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(PCGfxUIVertex, kind), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d;
    D3D12_RENDER_TARGET_BLEND_DESC* rt;
    ID3D12PipelineState* pso = NULL;
    const void *vs, *ps;
    size_t vs_n, ps_n;

    memset(&d, 0, sizeof(d));
    if (kind == KIND_UI) {
        vs = ID3D10Blob_GetBufferPointer(dx.ui_vs);
        vs_n = ID3D10Blob_GetBufferSize(dx.ui_vs);
        ps = ID3D10Blob_GetBufferPointer(dx.ui_ps);
        ps_n = ID3D10Blob_GetBufferSize(dx.ui_ps);
        d.pRootSignature = dx.ui_root;
        d.InputLayout.pInputElementDescs = ui_in;
        d.InputLayout.NumElements = 5;
    } else if (kind == KIND_UBER) {
        vs = ID3D10Blob_GetBufferPointer(dx.gx_vs_uber);
        vs_n = ID3D10Blob_GetBufferSize(dx.gx_vs_uber);
        ps = ID3D10Blob_GetBufferPointer(dx.gx_ps_uber);
        ps_n = ID3D10Blob_GetBufferSize(dx.gx_ps_uber);
        d.pRootSignature = dx.gx_root;
        d.InputLayout.pInputElementDescs = gx_in;
        d.InputLayout.NumElements = 4;
    } else {
        if (!shader_code(0, 0, config, &vs, &vs_n) || !shader_code(1, 0, config, &ps, &ps_n)) return NULL;
        d.pRootSignature = dx.gx_root;
        d.InputLayout.pInputElementDescs = gx_in;
        d.InputLayout.NumElements = 4;
    }
    d.VS.pShaderBytecode = vs;
    d.VS.BytecodeLength = vs_n;
    d.PS.pShaderBytecode = ps;
    d.PS.BytecodeLength = ps_n;
    rt = &d.BlendState.RenderTarget[0];
    if (rkey & (1u << 3)) {
        D3D12_BLEND_OP op = (rkey >> 20) & 1 ? D3D12_BLEND_OP_REV_SUBTRACT : D3D12_BLEND_OP_ADD;

        rt->BlendEnable = TRUE;
        rt->SrcBlend = blend_factor((rkey >> 4) & 15, 0);
        rt->DestBlend = blend_factor((rkey >> 8) & 15, 0);
        rt->SrcBlendAlpha = blend_factor((rkey >> 12) & 15, 1);
        rt->DestBlendAlpha = blend_factor((rkey >> 16) & 15, 1);
        rt->BlendOp = op;
        rt->BlendOpAlpha = op;
    } else {
        rt->SrcBlend = rt->SrcBlendAlpha = D3D12_BLEND_ONE;
        rt->DestBlend = rt->DestBlendAlpha = D3D12_BLEND_ZERO;
        rt->BlendOp = rt->BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    rt->LogicOp = D3D12_LOGIC_OP_NOOP;
    rt->RenderTargetWriteMask = (UINT8)(((rkey >> 26) & 1 ? D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
                                                                 D3D12_COLOR_WRITE_ENABLE_BLUE : 0) |
                                        ((rkey >> 27) & 1 ? D3D12_COLOR_WRITE_ENABLE_ALPHA : 0));
    d.SampleMask = 0xFFFFFFFFu;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    switch ((rkey >> 28) & 3) {
        case PC_CULL_FRONT: d.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; break;
        case PC_CULL_BACK:  d.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; break;
        default:            d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; break; /* (all: not drawn at all) */
    }
    d.RasterizerState.FrontCounterClockwise = TRUE; /* OpenGL's */
    d.RasterizerState.DepthClipEnable = TRUE;
    d.RasterizerState.MultisampleEnable = dx.samples > 1;
    if (rkey & (1u << 21)) {
        d.DepthStencilState.DepthEnable = TRUE;
        d.DepthStencilState.DepthWriteMask = (rkey >> 22) & 1 ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        d.DepthStencilState.DepthFunc = (D3D12_COMPARISON_FUNC)(D3D12_COMPARISON_FUNC_NEVER + ((rkey >> 23) & 7));
    } else {
        d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    }
    d.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    d.PrimitiveTopologyType = (D3D12_PRIMITIVE_TOPOLOGY_TYPE)(rkey & 7);
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = dx.color_format;
    d.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.SampleDesc.Count = (UINT)(dx.samples > 1 ? dx.samples : 1);
    if (FAILED(ID3D12Device_CreateGraphicsPipelineState(dx.dev, &d, &IID_ID3D12PipelineState, (void**)&pso))) {
        return NULL;
    }
    return pso;
}

static uint32_t pipe_hash(int kind, uint32_t rkey, const PCGXShaderConfig* config) {
    uint32_t h = 2166136261u ^ (uint32_t)kind;

    h = (h ^ rkey) * 16777619u;
    if (config != NULL) {
        for (int i = 0; i < PC_GX_CONFIG_COUNT; i++) h = (h ^ (uint32_t)config->v[i]) * 16777619u;
    }
    return h;
}

static Pipe* pipe_find(int kind, uint32_t rkey, const PCGXShaderConfig* config, int create) {
    uint32_t h = pipe_hash(kind, rkey, config);

    for (uint32_t i = 0; i < PIPELINES; i++) {
        Pipe* p = &dx.pipes[(h + i) & (PIPELINES - 1)];
        int state = SDL_AtomicGet(&p->state);

        if (state == 0) {
            if (!create || dx.pipes_built >= PIPELINES * 3 / 4) return NULL;
            p->kind = kind;
            p->rkey = rkey;
            p->hash = h;
            if (config != NULL) p->config = *config;
            dx.pipes_built++;
            return p;
        }
        if (p->hash == h && p->kind == kind && p->rkey == rkey &&
            (config == NULL || memcmp(&p->config, config, sizeof(*config)) == 0)) {
            return p;
        }
    }
    return NULL;
}

static int worker_main(void* arg) {
    (void)arg;
    SDL_LockMutex(dx.lock);
    for (;;) {
        Pipe* p;

        while (dx.njobs == 0 && !dx.quit) SDL_CondWait(dx.wake, dx.lock);
        if (dx.quit) break;
        p = &dx.pipes[dx.queue_jobs[--dx.njobs]];
        SDL_UnlockMutex(dx.lock);
        p->pso = build_pipeline(KIND_SPEC, p->rkey, &p->config);
        SDL_MemoryBarrierRelease();
        SDL_AtomicSet(&p->state, p->pso != NULL ? 2 : 3);
        SDL_LockMutex(dx.lock);
        if (dx.configs_out != NULL && p->pso != NULL) {
            fwrite(&p->rkey, sizeof(p->rkey), 1, dx.configs_out);
            fwrite(&p->config, sizeof(p->config), 1, dx.configs_out);
            fflush(dx.configs_out);
        }
    }
    SDL_UnlockMutex(dx.lock);
    return 0;
}

static void queue_spec(Pipe* p) {
    SDL_AtomicSet(&p->state, 1);
    SDL_LockMutex(dx.lock);
    if (dx.njobs == dx.capjobs) {
        dx.capjobs = dx.capjobs ? dx.capjobs * 2 : 64;
        dx.queue_jobs = (int*)realloc(dx.queue_jobs, (size_t)dx.capjobs * sizeof(int));
    }
    dx.queue_jobs[dx.njobs++] = (int)(p - dx.pipes);
    SDL_CondSignal(dx.wake);
    SDL_UnlockMutex(dx.lock);
}

static ID3D12PipelineState* now_pipeline(int kind, uint32_t rkey) {
    Pipe* p = pipe_find(kind, rkey, NULL, 1);

    if (p == NULL) return NULL;
    if (SDL_AtomicGet(&p->state) == 0) {
        p->pso = build_pipeline(kind, rkey, NULL);
        SDL_AtomicSet(&p->state, p->pso != NULL ? 2 : 3);
    }
    return SDL_AtomicGet(&p->state) == 2 ? p->pso : NULL;
}

static ID3D12PipelineState* gx_pipeline(uint32_t rkey) {
    if (g_pc_settings.specialized_shaders && dx.worker != NULL) {
        Pipe* p = pipe_find(KIND_SPEC, rkey, &g_gx.shader_config, 1);

        if (p != NULL) {
            int state = SDL_AtomicGet(&p->state);

            if (state == 2) {
                SDL_MemoryBarrierAcquire();
                return p->pso;
            }
            if (state == 0) queue_spec(p);
        }
    }
    return now_pipeline(KIND_UBER, rkey);
}

static void load_configs(void) {
    FILE* f = fopen(CONFIGS_FILE, "rb");
    unsigned head[2] = { 0x31435844u /* "DXC1" */, PC_GX_CONFIG_COUNT }, got[2];
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
    dx.configs_out = fopen(CONFIGS_FILE, good ? "ab" : "wb");
    if (dx.configs_out != NULL && !good) {
        fwrite(head, sizeof(head), 1, dx.configs_out);
        fflush(dx.configs_out);
    }
    if (queued) printf("[PC/D3D12] %d pipelines of earlier runs queued\n", queued);
}

/* --- drawing --- */

static int set_view(ID3D12GraphicsCommandList* cl) {
    const PCGfxState* s = &g_gfx_state;
    int fb_h = dx.height, fb_w = dx.width;
    D3D12_VIEWPORT vp;
    D3D12_RECT sc;

    if (s->vp[2] <= 0 || s->vp[3] <= 0) return 0;
    vp.TopLeftX = (float)s->vp[0];
    vp.TopLeftY = (float)(fb_h - (s->vp[1] + s->vp[3]));
    vp.Width = (float)s->vp[2];
    vp.Height = (float)s->vp[3];
    vp.MinDepth = s->depth_near < 0.0f ? 0.0f : s->depth_near > 1.0f ? 1.0f : s->depth_near;
    vp.MaxDepth = s->depth_far < 0.0f ? 0.0f : s->depth_far > 1.0f ? 1.0f : s->depth_far;
    if (s->scissor_test) {
        int x0 = s->scissor[0], y0 = fb_h - (s->scissor[1] + s->scissor[3]);
        int x1 = x0 + s->scissor[2], y1 = y0 + s->scissor[3];

        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > fb_w) x1 = fb_w;
        if (y1 > fb_h) y1 = fb_h;
        if (x1 <= x0 || y1 <= y0) return 0;
        sc.left = x0;
        sc.top = y0;
        sc.right = x1;
        sc.bottom = y1;
    } else {
        sc.left = sc.top = 0;
        sc.right = fb_w;
        sc.bottom = fb_h;
    }
    if (memcmp(&vp, &dx.bound_vp, sizeof(vp)) != 0) {
        ID3D12GraphicsCommandList_RSSetViewports(cl, 1, &vp);
        dx.bound_vp = vp;
    }
    if (memcmp(&sc, &dx.bound_scissor, sizeof(sc)) != 0) {
        ID3D12GraphicsCommandList_RSSetScissorRects(cl, 1, &sc);
        dx.bound_scissor = sc;
    }
    return 1;
}

static void bind_pso(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso, ID3D12RootSignature* root,
                     D3D_PRIMITIVE_TOPOLOGY topo) {
    if (root != dx.bound_root) {
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(cl, root);
        dx.bound_root = root;
        memset(dx.bound_cbv, 0, sizeof(dx.bound_cbv));
        dx.bound_srv_table = dx.bound_smp_table = -1;
    }
    if (pso != dx.bound_pso) {
        ID3D12GraphicsCommandList_SetPipelineState(cl, pso);
        dx.bound_pso = pso;
    }
    if (topo != dx.bound_topo) {
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(cl, topo);
        dx.bound_topo = topo;
    }
}

static void bind_tables(ID3D12GraphicsCommandList* cl, const TexSet* e, UINT srv_param, UINT smp_param) {
    if (e->srv_table != dx.bound_srv_table) {
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(cl, srv_param, table_gpu(dx.srv_heap, e->srv_table, dx.srv_size));
        dx.bound_srv_table = e->srv_table;
    }
    if (e->smp_table != dx.bound_smp_table) {
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(cl, smp_param, table_gpu(dx.smp_heap, e->smp_table, dx.smp_size));
        dx.bound_smp_table = e->smp_table;
    }
}

static void bind_ib(ID3D12GraphicsCommandList* cl, D3D12_GPU_VIRTUAL_ADDRESS gpu, UINT size) {
    D3D12_INDEX_BUFFER_VIEW ib = { gpu, size, DXGI_FORMAT_R16_UINT };

    if (gpu != dx.bound_ib) {
        ID3D12GraphicsCommandList_IASetIndexBuffer(cl, &ib);
        dx.bound_ib = gpu;
    }
}

static void dx_draw_gx(int prim, const PCGXVertex* v, int count, unsigned changed) {
    static const UINT64 sizes[3] = { sizeof(PCGXVertBlock), sizeof(PCGXFragBlock), sizeof(PCGXShaderConfig) };
    static const unsigned bits[3] = { PC_GX_BLOCK_VERT, PC_GX_BLOCK_FRAG, PC_GX_BLOCK_CONFIG };

    if (count <= 0 || dx.lost || g_gfx_state.cull == PC_CULL_ALL) return;
    if (prim == PC_PRIM_TRIANGLE_FAN && count < 3) return;
    frame_begin();
    for (int attempt = 0; attempt < 3; attempt++) {
        Frame* f = &dx.frames[dx.fi];
        ID3D12GraphicsCommandList* cl = f->cl;
        ID3D12PipelineState* pso;
        const void* data[3] = { &g_gx.vert_block, &g_gx.frag_block, &g_gx.shader_config };
        uint64_t uid[5];
        int srv[5], smp[5];
        D3D12_VERTEX_BUFFER_VIEW vb;
        D3D12_GPU_VIRTUAL_ADDRESS gpu;
        unsigned char* p;

        dx.rebind = 0;
        initial_clear();
        if (!set_view(cl)) return;
        pso = gx_pipeline(raster_key(topo_type(prim)));
        if (pso == NULL) return;
        bind_pso(cl, pso, dx.gx_root, topology(prim));
        for (int i = 0; i < 3; i++) {
            if ((changed & bits[i]) || dx.blk_gpu[i] == 0) {
                p = stream_alloc(sizes[i], D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, &gpu);
                if (p == NULL) return;
                memcpy(p, data[i], (size_t)sizes[i]);
                dx.blk_gpu[i] = gpu;
            }
        }
        if (dx.rebind) {
            changed = ~0u;
            continue;
        }
        for (int i = 0; i < 3; i++) {
            if (dx.blk_gpu[i] != dx.bound_cbv[i]) {
                ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(cl, (UINT)i, dx.blk_gpu[i]);
                dx.bound_cbv[i] = dx.blk_gpu[i];
            }
        }
        for (int u = 0; u < 5; u++) uid[u] = unit_image(g_gfx_state.bound[u], &srv[u], &smp[u]);
        if (dx.last_set == NULL || memcmp(uid, dx.last_uid, sizeof(uid)) != 0 || memcmp(smp, dx.last_smp, sizeof(smp)) != 0) {
            dx.last_set = tex_set(5, uid, srv, smp);
            memcpy(dx.last_uid, uid, sizeof(uid));
            memcpy(dx.last_smp, smp, sizeof(smp));
        }
        if (dx.rebind) continue;
        if (dx.last_set == NULL) return;
        dx.last_set->used = dx.serial;
        bind_tables(cl, dx.last_set, 3, 4);
        p = stream_alloc((UINT64)count * sizeof(PCGXVertex), 16, &gpu);
        if (p == NULL) return;
        if (dx.rebind) continue;
        memcpy(p, v, (size_t)count * sizeof(PCGXVertex));
        vb.BufferLocation = gpu;
        vb.SizeInBytes = (UINT)(count * sizeof(PCGXVertex));
        vb.StrideInBytes = sizeof(PCGXVertex);
        ID3D12GraphicsCommandList_IASetVertexBuffers(cl, 0, 1, &vb);
        if (prim == PC_PRIM_QUADS) {
            bind_ib(cl, ID3D12Resource_GetGPUVirtualAddress(dx.quad_ib), PC_GX_MAX_VERTS / 4 * 6 * 2);
            ID3D12GraphicsCommandList_DrawIndexedInstanced(cl, (UINT)(count / 4 * 6), 1, 0, 0, 0);
        } else if (prim == PC_PRIM_TRIANGLE_FAN) {
            bind_ib(cl, ID3D12Resource_GetGPUVirtualAddress(dx.fan_ib), (PC_GX_MAX_VERTS - 2) * 3 * 2);
            ID3D12GraphicsCommandList_DrawIndexedInstanced(cl, (UINT)((count - 2) * 3), 1, 0, 0, 0);
        } else {
            ID3D12GraphicsCommandList_DrawInstanced(cl, (UINT)count, 1, 0, 0);
        }
        return;
    }
}

static void dx_draw_ui(PCTex tex, const PCGfxUIVertex* v, int count, const unsigned short* indices, int index_count,
                       float space_w, float space_h) {
    float space[4] = { space_w, space_h, 0.0f, 0.0f };

    if (count <= 0 || dx.lost) return;
    frame_begin();
    for (int attempt = 0; attempt < 3; attempt++) {
        ID3D12GraphicsCommandList* cl = dx.frames[dx.fi].cl;
        ID3D12PipelineState* pso;
        uint64_t uid;
        int srv, smp;
        TexSet* e;
        D3D12_VERTEX_BUFFER_VIEW vb;
        D3D12_GPU_VIRTUAL_ADDRESS gpu, igpu = 0;
        unsigned char *p, *ip = NULL;

        dx.rebind = 0;
        initial_clear();
        if (!set_view(cl)) return;
        pso = now_pipeline(KIND_UI, raster_key(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE));
        if (pso == NULL) return;
        bind_pso(cl, pso, dx.ui_root, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        uid = unit_image(tex != 0 ? tex : dx.white, &srv, &smp);
        e = tex_set(1, &uid, &srv, &smp);
        if (dx.rebind) continue;
        if (e == NULL) return;
        p = stream_alloc((UINT64)count * sizeof(PCGfxUIVertex), 16, &gpu);
        if (p == NULL) return;
        if (dx.rebind) continue;
        if (indices != NULL) {
            ip = stream_alloc((UINT64)index_count * 2, 4, &igpu);
            if (ip == NULL) return;
            if (dx.rebind) continue;
        }
        bind_tables(cl, e, 1, 2);
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(cl, 0, 4, space, 0);
        memcpy(p, v, (size_t)count * sizeof(PCGfxUIVertex));
        vb.BufferLocation = gpu;
        vb.SizeInBytes = (UINT)(count * sizeof(PCGfxUIVertex));
        vb.StrideInBytes = sizeof(PCGfxUIVertex);
        ID3D12GraphicsCommandList_IASetVertexBuffers(cl, 0, 1, &vb);
        if (indices != NULL) {
            memcpy(ip, indices, (size_t)index_count * 2);
            bind_ib(cl, igpu, (UINT)(index_count * 2));
            ID3D12GraphicsCommandList_DrawIndexedInstanced(cl, (UINT)index_count, 1, 0, 0, 0);
        } else {
            ID3D12GraphicsCommandList_DrawInstanced(cl, (UINT)count, 1, 0, 0);
        }
        return;
    }
}

static void dx_clear(int color, int depth, const float rgba[4], float depth_value) {
    const PCGfxState* s = &g_gfx_state;
    D3D12_RECT r;
    int full = !s->scissor_test || (s->scissor[0] <= 0 && s->scissor[1] <= 0 && s->scissor[0] + s->scissor[2] >= dx.width &&
                                    s->scissor[1] + s->scissor[3] >= dx.height);

    if (dx.lost) return;
    color = color && (s->color_write || s->alpha_write);
    depth = depth && s->depth_write;
    if (!color && !depth) return;
    frame_begin();
    if (full && color && depth) {
        dx.need_clear = 0; /* (this is the frame's clear) */
    } else {
        initial_clear();
    }
    if (!full) {
        int x0 = s->scissor[0], y0 = dx.height - (s->scissor[1] + s->scissor[3]);
        int x1 = x0 + s->scissor[2], y1 = y0 + s->scissor[3];

        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > dx.width) x1 = dx.width;
        if (y1 > dx.height) y1 = dx.height;
        if (x1 <= x0 || y1 <= y0) return;
        r.left = x0;
        r.top = y0;
        r.right = x1;
        r.bottom = y1;
    }
    clear_now(color, depth, rgba, depth_value, full ? NULL : &r);
}

/* glReadPixels: GL rows (bottom up) of the picture so far */
static void dx_read_pixels(int x, int y, int w, int h, int rgba, void* out) {
    Frame* f;
    D3D12_TEXTURE_COPY_LOCATION to, from;
    D3D12_BOX box;
    UINT64 pitch, need;
    unsigned char* dst = (unsigned char*)out;
    unsigned char* map = NULL;
    int bgra = dx.color_format == DXGI_FORMAT_B8G8R8A8_UNORM;
    D3D12_RESOURCE_STATES back;

    memset(out, 0, (size_t)w * h * (rgba ? 4 : 3));
    if (w <= 0 || h <= 0 || dx.lost) return;
    frame_begin();
    initial_clear();
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > dx.width) w = dx.width - x;
    if (y + h > dx.height) h = dx.height - y;
    if (w <= 0 || h <= 0) return;
    pitch = ((UINT64)w * 4 + 255) & ~(UINT64)255;
    need = pitch * (UINT64)h;
    if (dx.readback_size < need) {
        HostBuf hb;

        if (dx.readback != NULL) ID3D12Resource_Release(dx.readback);
        dx.readback = NULL;
        dx.readback_size = 0;
        if (!host_buffer(&hb, need, D3D12_HEAP_TYPE_READBACK)) return;
        ID3D12Resource_Unmap(hb.res, 0, NULL);
        dx.readback = hb.res;
        dx.readback_size = need;
    }
    f = &dx.frames[dx.fi];
    back = dx.bb_state;
    if (dx.samples > 1) {
        resolve(f->cl, D3D12_RESOURCE_STATE_COPY_SOURCE);
    } else {
        barrier(f->cl, dx.buffers[dx.bb], dx.bb_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dx.bb_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    memset(&to, 0, sizeof(to));
    memset(&from, 0, sizeof(from));
    from.pResource = dx.buffers[dx.bb];
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = dx.readback;
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint.Format = dx.color_format;
    to.PlacedFootprint.Footprint.Width = (UINT)w;
    to.PlacedFootprint.Footprint.Height = (UINT)h;
    to.PlacedFootprint.Footprint.Depth = 1;
    to.PlacedFootprint.Footprint.RowPitch = (UINT)pitch;
    box.left = (UINT)x;
    box.top = (UINT)(dx.height - (y + h));
    box.right = (UINT)(x + w);
    box.bottom = (UINT)(dx.height - y);
    box.front = 0;
    box.back = 1;
    ID3D12GraphicsCommandList_CopyTextureRegion(f->cl, &to, 0, 0, 0, &from, &box);
    barrier(f->cl, dx.buffers[dx.bb], D3D12_RESOURCE_STATE_COPY_SOURCE, back);
    dx.bb_state = back;
    flush_and_wait();
    {
        D3D12_RANGE range = { 0, (SIZE_T)need };
        D3D12_RANGE none = { 0, 0 };
        int bpp = rgba ? 4 : 3;

        if (FAILED(ID3D12Resource_Map(dx.readback, 0, &range, (void**)&map))) return;
        for (int r = 0; r < h; r++) {
            const unsigned char* s = map + (size_t)(h - 1 - r) * pitch; /* (the image's rows go down) */
            unsigned char* d = dst + (size_t)r * w * bpp;

            for (int i = 0; i < w; i++) {
                d[i * bpp + 0] = s[i * 4 + (bgra ? 2 : 0)];
                d[i * bpp + 1] = s[i * 4 + 1];
                d[i * bpp + 2] = s[i * 4 + (bgra ? 0 : 2)];
                if (rgba) d[i * bpp + 3] = s[i * 4 + 3];
            }
        }
        ID3D12Resource_Unmap(dx.readback, 0, &none);
    }
}

/* --- setup and teardown --- */

static void dx_fail(const char* what, HRESULT hr) {
    HRESULT why = hr;

    if (dx.dev != NULL && (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)) {
        why = ID3D12Device_GetDeviceRemovedReason(dx.dev);
    }
    fprintf(stderr, "[PC/D3D12] %s failed (0x%08lx, 0x%08lx)\n", what, (unsigned long)hr, (unsigned long)why);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
        hr == E_OUTOFMEMORY) {
        g_pc_settings.renderer = PC_GFX_OPENGL;
        pc_settings_save();
        pc_fatal("The graphics driver stopped (Direct3D 12: %s, 0x%08lx). The game will use OpenGL from the next "
                 "start (Settings > Video > Graphics API).", what, (unsigned long)why);
    }
    dx.lost = 1;
}

static ID3D12RootSignature* root_signature(int ui) {
    D3D12_DESCRIPTOR_RANGE srv = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, ui ? 1u : 5u, 0, 0, 0 };
    D3D12_DESCRIPTOR_RANGE smp = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, ui ? 1u : 5u, 0, 0, 0 };
    D3D12_ROOT_PARAMETER p[5];
    D3D12_ROOT_SIGNATURE_DESC d = { 0 };
    ID3DBlob *blob = NULL, *err = NULL;
    ID3D12RootSignature* rs = NULL;
    UINT n = 0;

    memset(p, 0, sizeof(p));
    if (ui) {
        p[n].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p[n].Constants.ShaderRegister = 0;
        p[n].Constants.Num32BitValues = 4;
        p[n].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        n++;
    } else {
        for (int i = 0; i < 3; i++) {
            p[n].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            p[n].Descriptor.ShaderRegister = (UINT)i;
            p[n].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            n++;
        }
    }
    p[n].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[n].DescriptorTable.NumDescriptorRanges = 1;
    p[n].DescriptorTable.pDescriptorRanges = &srv;
    p[n].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    n++;
    p[n].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[n].DescriptorTable.NumDescriptorRanges = 1;
    p[n].DescriptorTable.pDescriptorRanges = &smp;
    p[n].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    n++;
    d.NumParameters = n;
    d.pParameters = p;
    d.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (FAILED(pD3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        if (err != NULL) {
            fprintf(stderr, "[PC/D3D12] root signature: %s\n", (const char*)ID3D10Blob_GetBufferPointer(err));
            ID3D10Blob_Release(err);
        }
        return NULL;
    }
    ID3D12Device_CreateRootSignature(dx.dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                     &IID_ID3D12RootSignature, (void**)&rs);
    ID3D10Blob_Release(blob);
    if (err != NULL) ID3D10Blob_Release(err);
    return rs;
}

/* an index buffer the GPU reads for good: copied to its memory by the first
 * frame's uploads */
static ID3D12Resource* static_indices(const unsigned short* idx, UINT64 bytes) {
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd = { 0 };
    ID3D12Resource *res = NULL, *src = NULL;
    ID3D12GraphicsCommandList* cl;
    UINT64 off = 0;
    unsigned char* dst;

    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = bytes;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(ID3D12Device_CreateCommittedResource(dx.dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                    NULL, &IID_ID3D12Resource, (void**)&res))) {
        return NULL;
    }
    dst = staging_alloc(bytes, 256, &src, &off);
    if (dst == NULL) {
        ID3D12Resource_Release(res);
        return NULL;
    }
    memcpy(dst, idx, (size_t)bytes);
    cl = upload_list();
    ID3D12GraphicsCommandList_CopyBufferRegion(cl, res, 0, src, off, bytes);
    barrier(cl, res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    return res;
}

static int frames_create(void) {
    for (int i = 0; i < FRAMES; i++) {
        Frame* f = &dx.frames[i];

        if (FAILED(ID3D12Device_CreateCommandAllocator(dx.dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                       (void**)&f->alloc)) ||
            FAILED(ID3D12Device_CreateCommandAllocator(dx.dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                       (void**)&f->upload_alloc)) ||
            FAILED(ID3D12Device_CreateCommandList(dx.dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, f->alloc, NULL,
                                                  &IID_ID3D12GraphicsCommandList, (void**)&f->cl)) ||
            FAILED(ID3D12Device_CreateCommandList(dx.dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, f->upload_alloc, NULL,
                                                  &IID_ID3D12GraphicsCommandList, (void**)&f->upload))) {
            return 0;
        }
        ID3D12GraphicsCommandList_Close(f->cl);
        ID3D12GraphicsCommandList_Close(f->upload);
        if (!host_buffer(&f->stream, STREAM_SIZE, D3D12_HEAP_TYPE_UPLOAD) ||
            !host_buffer(&f->staging, STAGING_SIZE, D3D12_HEAP_TYPE_UPLOAD)) {
            return 0;
        }
    }
    return 1;
}

static int heaps_create(void) {
    D3D12_DESCRIPTOR_HEAP_DESC hd = { 0 };

    dx.rtv_size = ID3D12Device_GetDescriptorHandleIncrementSize(dx.dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dx.srv_size = ID3D12Device_GetDescriptorHandleIncrementSize(dx.dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dx.smp_size = ID3D12Device_GetDescriptorHandleIncrementSize(dx.dev, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = BUFFERS + 1;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(dx.dev, &hd, &IID_ID3D12DescriptorHeap, (void**)&dx.rtv_heap))) return 0;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = 1;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(dx.dev, &hd, &IID_ID3D12DescriptorHeap, (void**)&dx.dsv_heap))) return 0;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = SRV_TABLES * 5;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(dx.dev, &hd, &IID_ID3D12DescriptorHeap, (void**)&dx.srv_heap))) return 0;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    hd.NumDescriptors = SMP_TABLES * 5;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(dx.dev, &hd, &IID_ID3D12DescriptorHeap, (void**)&dx.smp_heap))) return 0;
    hd.NumDescriptors = 3 * 3 * 3 * 2;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(dx.dev, &hd, &IID_ID3D12DescriptorHeap, (void**)&dx.smp_cpu))) return 0;
    for (int i = 0; i < SRV_TABLES; i++) dx.srv_table_free[i] = SRV_TABLES - 1 - i;
    dx.nsrv_table_free = SRV_TABLES;
    dx.texsets = (TexSet*)calloc(TEXSETS, sizeof(TexSet));
    samplers_create();
    return dx.texsets != NULL;
}

static IDXGIAdapter1* pick_adapter(void) {
    IDXGIFactory6* f6 = NULL;
    IDXGIAdapter1* best = NULL;

    if (SUCCEEDED(IDXGIFactory4_QueryInterface(dx.factory, &IID_IDXGIFactory6, (void**)&f6))) {
        for (UINT i = 0; best == NULL; i++) {
            IDXGIAdapter1* a = NULL;
            DXGI_ADAPTER_DESC1 d;

            if (FAILED(IDXGIFactory6_EnumAdapterByGpuPreference(f6, i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                &IID_IDXGIAdapter1, (void**)&a))) {
                break;
            }
            IDXGIAdapter1_GetDesc1(a, &d);
            if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                SUCCEEDED(pD3D12CreateDevice((IUnknown*)a, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL))) {
                best = a;
            } else {
                IDXGIAdapter1_Release(a);
            }
        }
        IDXGIFactory6_Release(f6);
    }
    for (UINT i = 0; best == NULL; i++) {
        IDXGIAdapter1* a = NULL;
        DXGI_ADAPTER_DESC1 d;

        if (FAILED(IDXGIFactory4_EnumAdapters1(dx.factory, i, &a))) break;
        IDXGIAdapter1_GetDesc1(a, &d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            SUCCEEDED(pD3D12CreateDevice((IUnknown*)a, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL))) {
            best = a;
        } else {
            IDXGIAdapter1_Release(a);
        }
    }
    return best;
}

static int pick_samples(void) {
    dx.samples = 1;
    for (int s = 8; s >= 2; s /= 2) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q = { 0 };
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qd = { 0 };

        if (s > dx.want_samples) continue;
        q.Format = dx.color_format;
        q.SampleCount = (UINT)s;
        qd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        qd.SampleCount = (UINT)s;
        if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(dx.dev, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) &&
            SUCCEEDED(ID3D12Device_CheckFeatureSupport(dx.dev, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qd, sizeof(qd))) &&
            q.NumQualityLevels > 0 && qd.NumQualityLevels > 0) {
            dx.samples = s;
            break;
        }
    }
    if (dx.samples != (dx.want_samples > 1 ? dx.want_samples : 1)) {
        printf("[PC] MSAA %dx is not available, using %dx\n", dx.want_samples, dx.samples > 1 ? dx.samples : 0);
    }
    return 1;
}

static void dx_shutdown(void) {
    if (dx.dev != NULL) {
        if (dx.queue != NULL && dx.fence != NULL) wait_idle();
        if (dx.worker != NULL) {
            SDL_LockMutex(dx.lock);
            dx.quit = 1;
            SDL_CondSignal(dx.wake);
            SDL_UnlockMutex(dx.lock);
            SDL_WaitThread(dx.worker, NULL);
            dx.worker = NULL;
        }
        for (int i = 0; dx.pipes != NULL && i < PIPELINES; i++) {
            if (dx.pipes[i].pso != NULL) ID3D12PipelineState_Release(dx.pipes[i].pso);
        }
        free(dx.pipes);
        garbage_collect(1);
        targets_release();
        for (int i = 0; i < FRAMES; i++) {
            Frame* f = &dx.frames[i];

            if (f->cl) ID3D12GraphicsCommandList_Release(f->cl);
            if (f->upload) ID3D12GraphicsCommandList_Release(f->upload);
            if (f->alloc) ID3D12CommandAllocator_Release(f->alloc);
            if (f->upload_alloc) ID3D12CommandAllocator_Release(f->upload_alloc);
            host_buffer_free(&f->stream);
            host_buffer_free(&f->staging);
            for (int k = 0; k < f->nextra; k++) host_buffer_free(&f->extra[k]);
        }
        if (dx.readback) ID3D12Resource_Release(dx.readback);
        if (dx.quad_ib) ID3D12Resource_Release(dx.quad_ib);
        if (dx.fan_ib) ID3D12Resource_Release(dx.fan_ib);
        if (dx.gx_root) ID3D12RootSignature_Release(dx.gx_root);
        if (dx.ui_root) ID3D12RootSignature_Release(dx.ui_root);
        if (dx.gx_vs_uber) ID3D10Blob_Release(dx.gx_vs_uber);
        if (dx.gx_ps_uber) ID3D10Blob_Release(dx.gx_ps_uber);
        if (dx.ui_vs) ID3D10Blob_Release(dx.ui_vs);
        if (dx.ui_ps) ID3D10Blob_Release(dx.ui_ps);
        if (dx.rtv_heap) ID3D12DescriptorHeap_Release(dx.rtv_heap);
        if (dx.dsv_heap) ID3D12DescriptorHeap_Release(dx.dsv_heap);
        if (dx.srv_heap) ID3D12DescriptorHeap_Release(dx.srv_heap);
        if (dx.smp_heap) ID3D12DescriptorHeap_Release(dx.smp_heap);
        if (dx.smp_cpu) ID3D12DescriptorHeap_Release(dx.smp_cpu);
        for (int i = 0; i < dx.ncpu_srv; i++) ID3D12DescriptorHeap_Release(dx.cpu_srv[i]);
        for (int i = 0; i < dx.nblocks; i++) {
            ID3D12Heap_Release(dx.blocks[i].heap);
            free(dx.blocks[i].free);
        }
        free(dx.blocks);
        free(dx.garbage);
        free(dx.srv_free);
        free(dx.texsets);
        free(dx.queue_jobs);
        free(dx.mip_src);
        for (int i = 0; i < dx.nblobs; i++) free(dx.blobs[i].code);
        free(dx.blobs);
        if (dx.swapchain) IDXGISwapChain3_Release(dx.swapchain);
        if (dx.fence) ID3D12Fence_Release(dx.fence);
        if (dx.queue) ID3D12CommandQueue_Release(dx.queue);
        ID3D12Device_Release(dx.dev);
    }
    if (dx.configs_out != NULL) fclose(dx.configs_out);
    if (dx.cache_out != NULL) fclose(dx.cache_out);
    if (dx.fence_event != NULL) CloseHandle(dx.fence_event);
    if (dx.factory != NULL) IDXGIFactory4_Release(dx.factory);
    if (dx.lock) SDL_DestroyMutex(dx.lock);
    if (dx.wake) SDL_DestroyCond(dx.wake);
    if (dx.compiler_dll) FreeLibrary(dx.compiler_dll);
    if (dx.d3d12_dll) FreeLibrary(dx.d3d12_dll);
    if (dx.dxgi_dll) FreeLibrary(dx.dxgi_dll);
    memset(&dx, 0, sizeof(dx));
}

static void dx_drawable_size(int* w, int* h) {
    window_pixels(w, h);
    if (*w != dx.width || *h != dx.height) dx.swapchain_dirty = 1;
}

static void dx_set_vsync(int on) {
    dx.vsync = on;
}

static int load_dlls(void) {
    dx.d3d12_dll = LoadLibraryA("d3d12.dll");
    dx.dxgi_dll = LoadLibraryA("dxgi.dll");
    dx.compiler_dll = LoadLibraryA("d3dcompiler_47.dll");
    if (dx.d3d12_dll == NULL || dx.dxgi_dll == NULL || dx.compiler_dll == NULL) return 0;
    pD3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(dx.d3d12_dll, "D3D12CreateDevice");
    pD3D12SerializeRootSignature =
        (PFN_SerializeRootSignature_)(void*)GetProcAddress(dx.d3d12_dll, "D3D12SerializeRootSignature");
    pD3D12GetDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)(void*)GetProcAddress(dx.d3d12_dll, "D3D12GetDebugInterface");
    pCreateDXGIFactory2 = (PFN_CreateDXGIFactory2)(void*)GetProcAddress(dx.dxgi_dll, "CreateDXGIFactory2");
    pCreateDXGIFactory1 = (PFN_CreateDXGIFactory1)(void*)GetProcAddress(dx.dxgi_dll, "CreateDXGIFactory1");
    s_D3DCompile = (PFN_D3DCompile_)(void*)GetProcAddress(dx.compiler_dll, "D3DCompile");
    s_D3DCreateBlob = (PFN_D3DCreateBlob_)(void*)GetProcAddress(dx.compiler_dll, "D3DCreateBlob");
    return pD3D12CreateDevice != NULL && pD3D12SerializeRootSignature != NULL && s_D3DCompile != NULL &&
           s_D3DCreateBlob != NULL &&
           (pCreateDXGIFactory2 != NULL || pCreateDXGIFactory1 != NULL);
}

/* the uber and UI shaders, from the cache when it has them */
static int base_shaders(void) {
    const void* code;
    size_t size;

    if (!shader_code(0, 1, NULL, &code, &size) || FAILED(s_D3DCreateBlob(size, &dx.gx_vs_uber))) return 0;
    memcpy(ID3D10Blob_GetBufferPointer(dx.gx_vs_uber), code, size);
    if (!shader_code(1, 1, NULL, &code, &size) || FAILED(s_D3DCreateBlob(size, &dx.gx_ps_uber))) return 0;
    memcpy(ID3D10Blob_GetBufferPointer(dx.gx_ps_uber), code, size);
    dx.ui_vs = compile_text(pc_shader_ui_vert_hlsl, "ui.vert", "vs_5_0");
    dx.ui_ps = compile_text(pc_shader_ui_frag_hlsl, "ui.frag", "ps_5_0");
    return dx.ui_vs != NULL && dx.ui_ps != NULL;
}

int pc_gfx_d3d12_open(const char* title, int w, int h, Uint32 sdl_flags, int msaa) {
    SDL_SysWMinfo wm;
    IDXGIAdapter1* adapter = NULL;
    DXGI_ADAPTER_DESC1 ad;
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    DXGI_SWAP_CHAIN_DESC1 sd = { 0 };
    IDXGISwapChain1* sc1 = NULL;
    char name[128];

    memset(&dx, 0, sizeof(dx));
    if (!load_dlls()) {
        printf("[PC/D3D12] no Direct3D 12 here\n");
        goto fail;
    }
    if (getenv("AFE_D3D12_DEBUG") != NULL && pD3D12GetDebugInterface != NULL) {
        ID3D12Debug* dbg = NULL;

        if (SUCCEEDED(pD3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&dbg))) {
            ID3D12Debug_EnableDebugLayer(dbg);
            ID3D12Debug_Release(dbg);
        }
    }
    if ((pCreateDXGIFactory2 != NULL ? pCreateDXGIFactory2(0, &IID_IDXGIFactory4, (void**)&dx.factory)
                                     : pCreateDXGIFactory1(&IID_IDXGIFactory4, (void**)&dx.factory)) != S_OK) {
        goto fail;
    }
    adapter = pick_adapter();
    if (adapter == NULL ||
        FAILED(pD3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dx.dev))) {
        printf("[PC/D3D12] no GPU here takes Direct3D 12\n");
        if (adapter != NULL) IDXGIAdapter1_Release(adapter);
        goto fail;
    }
    IDXGIAdapter1_GetDesc1(adapter, &ad);
    IDXGIAdapter1_Release(adapter);
    WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof(name), NULL, NULL);
    if (FAILED(ID3D12Device_CreateCommandQueue(dx.dev, &qd, &IID_ID3D12CommandQueue, (void**)&dx.queue)) ||
        FAILED(ID3D12Device_CreateFence(dx.dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&dx.fence))) {
        goto fail;
    }
    dx.fence_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    dx.window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, sdl_flags);
    if (dx.window == NULL) goto fail;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(dx.window, &wm)) goto fail;
    dx.hwnd = wm.info.win.window;
    {
        IDXGIFactory5* f5 = NULL;
        BOOL allow = FALSE;

        if (SUCCEEDED(IDXGIFactory4_QueryInterface(dx.factory, &IID_IDXGIFactory5, (void**)&f5))) {
            if (SUCCEEDED(IDXGIFactory5_CheckFeatureSupport(f5, DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) {
                dx.tearing = allow != FALSE;
            }
            IDXGIFactory5_Release(f5);
        }
    }
    dx.color_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dx.sc_flags = dx.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    window_pixels(&dx.width, &dx.height);
    if (dx.width <= 0) dx.width = w;
    if (dx.height <= 0) dx.height = h;
    sd.Width = (UINT)dx.width;
    sd.Height = (UINT)dx.height;
    sd.Format = dx.color_format;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = BUFFERS;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = dx.sc_flags;
    if (FAILED(IDXGIFactory4_CreateSwapChainForHwnd(dx.factory, (IUnknown*)dx.queue, dx.hwnd, &sd, NULL, NULL, &sc1)) ||
        FAILED(IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void**)&dx.swapchain))) {
        if (sc1 != NULL) IDXGISwapChain1_Release(sc1);
        goto fail;
    }
    IDXGISwapChain1_Release(sc1);
    IDXGIFactory4_MakeWindowAssociation(dx.factory, dx.hwnd, DXGI_MWA_NO_ALT_ENTER); /* (SDL does fullscreen) */
    dx.want_samples = msaa;
    dx.vsync = g_pc_settings.vsync;
    dx.lock = SDL_CreateMutex();
    dx.wake = SDL_CreateCond();
    {
        uint64_t hsrc = hash64(pc_shader_gx_vert_hlsl, strlen(pc_shader_gx_vert_hlsl), 1469598103934665603ull);

        dx.source_hash = hash64(pc_shader_gx_frag_hlsl, strlen(pc_shader_gx_frag_hlsl), hsrc);
    }
    _mkdir(CACHE_DIR);
    cache_load();
    if (dx.lock == NULL || !pick_samples() || !heaps_create() || !targets_create() || !frames_create()) goto fail;
    dx.gx_root = root_signature(0);
    dx.ui_root = root_signature(1);
    if (dx.gx_root == NULL || dx.ui_root == NULL || !base_shaders()) goto fail;
    dx.quad_ib = static_indices(pc_gfx_quad_indices(), PC_GX_MAX_VERTS / 4 * 6 * 2);
    {
        unsigned short* fan = (unsigned short*)malloc((size_t)(PC_GX_MAX_VERTS - 2) * 3 * 2);

        if (fan == NULL) goto fail;
        for (int i = 0; i < PC_GX_MAX_VERTS - 2; i++) {
            fan[i * 3 + 0] = 0;
            fan[i * 3 + 1] = (unsigned short)(i + 1);
            fan[i * 3 + 2] = (unsigned short)(i + 2);
        }
        dx.fan_ib = static_indices(fan, (UINT64)(PC_GX_MAX_VERTS - 2) * 3 * 2);
        free(fan);
    }
    if (dx.quad_ib == NULL || dx.fan_ib == NULL) goto fail;
    dx.pipes = (Pipe*)calloc(PIPELINES, sizeof(Pipe));
    if (dx.pipes == NULL) goto fail;
    g_pc_window = dx.window;

    snprintf(s_backend.device, sizeof(s_backend.device), "%s, Direct3D 12", name);
    s_backend.bc7 = s_backend.s3tc = 1; /* (every Direct3D 12 GPU) */
    s_backend.shutdown = dx_shutdown;
    s_backend.drawable_size = dx_drawable_size;
    s_backend.set_vsync = dx_set_vsync;
    s_backend.present = dx_present;
    s_backend.tex_image = dx_tex_image;
    s_backend.tex_subimage = dx_tex_subimage;
    s_backend.tex_mipmap = dx_tex_mipmap;
    s_backend.tex_free = dx_tex_free;
    s_backend.clear = dx_clear;
    s_backend.draw_gx = dx_draw_gx;
    s_backend.draw_ui = dx_draw_ui;
    s_backend.read_pixels = dx_read_pixels;
    g_gfx_backend = &s_backend;
    {
        static const unsigned char black[4] = { 0, 0, 0, 255 }, white[4] = { 255, 255, 255, 255 };

        dx.dummy = pc_gfx_tex_new();
        pc_gfx_tex_upload(dx.dummy, 1, 1, PC_TEXF_RGBA8, black, 4, PC_FILTER_NEAREST);
        dx.white = pc_gfx_tex_new();
        pc_gfx_tex_upload(dx.white, 1, 1, PC_TEXF_RGBA8, white, 4, PC_FILTER_NEAREST);
    }
    if (g_pc_settings.specialized_shaders) {
        dx.worker = SDL_CreateThread(worker_main, "D3D12Pipelines", NULL);
        if (dx.worker != NULL) load_configs();
    }
    printf("[PC/D3D12] %s; %dx MSAA%s, specialized pipelines %s\n", s_backend.device, dx.samples > 1 ? dx.samples : 0,
           dx.tearing ? ", tearing allowed" : "", dx.worker != NULL ? "on (built on a thread)" : "off");
    return 1;

fail:
    printf("[PC/D3D12] cannot start Direct3D 12 here\n");
    {
        SDL_Window* win = dx.window;

        g_gfx_backend = NULL;
        dx_shutdown();
        if (win != NULL) SDL_DestroyWindow(win);
    }
    return 0;
}

#endif /* PC_GFX_D3D12_BUILT */
