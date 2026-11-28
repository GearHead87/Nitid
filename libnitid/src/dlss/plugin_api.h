/* The boundary between libnitid and its optional DLSS module, libnitid-dlss.so.
 *
 * NVIDIA's DLSS SDK (NGX) is under NVIDIA's own license, which must not be mixed into an open-source-licensed
 * program. So everything that touches the SDK lives in a separate module (libnitid/dlss-module/), and libnitid
 * only loads it at run time (dlopen) through this small C table. Without the module Nitid runs as before, just
 * without DLSS. Plain C types only: no SDK headers on this side.
 *
 * Images are Vulkan images on libnitid's device, created and synchronised by libnitid. Every create / evaluate call
 * records into the command buffer it is given; libnitid submits it.
 */
#ifndef NITID_DLSS_PLUGIN_API_H
#define NITID_DLSS_PLUGIN_API_H

#include <vulkan/vulkan.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NITID_DLSS_ABI 1
#define NITID_DLSS_ENTRY "nitid_dlss_api"

typedef struct NitidDlssImage {
    VkImage image;
    VkImageView view;
    VkFormat format;
    uint32_t width, height;
} NitidDlssImage;

typedef struct NitidDlssInitInfo {
    VkInstance instance;
    VkPhysicalDevice phys;
    VkDevice device;
    PFN_vkGetInstanceProcAddr get_instance_proc;
    PFN_vkGetDeviceProcAddr get_device_proc;
    const char* const* runtime_dirs; /* folders searched for libnvidia-ngx-dlss*.so */
    uint32_t runtime_dir_count;
    const char* data_dir;            /* NGX's own logs / cache */
    const char* app_version;
    void (*log)(int level, const char* msg); /* 0 info, 1 warning, 2 error */
} NitidDlssInitInfo;

typedef struct NitidDlssCaps {
    int super_res;      /* DLSS Super Resolution can be created */
    int frame_gen;      /* DLSS Frame Generation can be created (RTX 40 and newer) */
    int max_generated;  /* frames DLSS-G can make between two real ones (1 = 2x, 5 = 6x) */
} NitidDlssCaps;

typedef struct NitidDlssSuperResCreate {
    uint32_t in_width, in_height;    /* source frame (= motion vectors, depth) */
    uint32_t out_width, out_height;  /* same as the input = DLAA */
    int preset;                      /* 0 = NVIDIA's default for the mode, else NVSDK_NGX_DLSS_Hint_Render_Preset */
} NitidDlssSuperResCreate;

typedef struct NitidDlssSuperResEval {
    NitidDlssImage color;   /* RGBA8, input size */
    NitidDlssImage motion;  /* R16G16_SFLOAT, input size: current -> previous frame, in pixels */
    NitidDlssImage depth;   /* R32_SFLOAT, input size */
    NitidDlssImage output;  /* RGBA8, output size, GENERAL layout */
    int reset;              /* first frame, scene cut or seek: forget the history */
} NitidDlssSuperResEval;

typedef struct NitidDlssFrameGenCreate {
    uint32_t width, height;        /* frames (backbuffer, output) */
    uint32_t mv_width, mv_height;  /* motion vectors and depth (source size) */
    VkFormat format;               /* frame format (RGBA8) */
    int lite;                      /* DLSS-G "lite candidate blending": much cheaper, needs real motion vectors */
} NitidDlssFrameGenCreate;

typedef struct NitidDlssFrameGenEval {
    NitidDlssImage backbuffer;  /* the newer real frame */
    NitidDlssImage motion;      /* newer -> older real frame, in pixels at the motion image's size (zeros = none) */
    NitidDlssImage depth;
    NitidDlssImage output;      /* generated frame, GENERAL layout */
    uint32_t count;             /* frames generated between this pair (1..max_generated) */
    uint32_t index;             /* 1..count, called in order for every new backbuffer */
    uint64_t frame_id;          /* increases by one per backbuffer */
    int reset;
} NitidDlssFrameGenEval;

typedef struct NitidDlssApi {
    uint32_t abi;           /* NITID_DLSS_ABI */
    const char* sdk_version;
    /* Vulkan extensions NGX needs, to enable when creating the instance / device. */
    const char* const* (*instance_extensions)(uint32_t* count);
    const char* const* (*device_extensions)(uint32_t* count);
    /* 0 on failure (error filled in). Call once, after the device exists. */
    int (*init)(const NitidDlssInitInfo* info, NitidDlssCaps* caps, char* error, size_t error_size);
    void (*shutdown)(void);
    /* Features: NULL / 0 on failure (error filled in). */
    void* (*super_res_create)(VkCommandBuffer cb, const NitidDlssSuperResCreate* c, char* error, size_t error_size);
    int (*super_res_evaluate)(VkCommandBuffer cb, void* feature, const NitidDlssSuperResEval* e, char* error, size_t error_size);
    void* (*frame_gen_create)(VkCommandBuffer cb, const NitidDlssFrameGenCreate* c, char* error, size_t error_size);
    int (*frame_gen_evaluate)(VkCommandBuffer cb, void* feature, const NitidDlssFrameGenEval* e, char* error, size_t error_size);
    /* After the GPU is done with it. */
    void (*release)(void* feature);
} NitidDlssApi;

typedef const NitidDlssApi* (*NitidDlssGetApi)(void);

#ifdef __cplusplus
}
#endif

#endif
