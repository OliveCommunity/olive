#ifndef RENDERBACKEND_C_H
#define RENDERBACKEND_C_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#define OAK_RENDER_BACKEND_EXPORT extern "C" __declspec(dllexport)
#else
#define OAK_RENDER_BACKEND_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void *OakRenderBackendHandle;

enum OakRenderBackendKind {
	OAK_RENDER_BACKEND_UNKNOWN = 0,
	OAK_RENDER_BACKEND_OPENGL = 1,
	OAK_RENDER_BACKEND_VULKAN = 2
};

enum OakRenderBackendCapability {
	OAK_RENDER_BACKEND_CAP_TEXTURES = 1ULL << 0,
	OAK_RENDER_BACKEND_CAP_SHADERS = 1ULL << 1,
	OAK_RENDER_BACKEND_CAP_BLIT = 1ULL << 2,
	OAK_RENDER_BACKEND_CAP_READBACK = 1ULL << 3,
	OAK_RENDER_BACKEND_CAP_VIEWER_CONTEXT = 1ULL << 4
};

struct OakRenderBackendInfo {
	uint32_t abi_version;
	uint32_t kind;
	uint64_t capabilities;
	const char *name;
	const char *status;
};

typedef OakRenderBackendHandle (*OakBackendCreateFn)(void *parent);
typedef void (*OakBackendDestroyFn)(OakRenderBackendHandle handle);
typedef bool (*OakBackendGetInfoFn)(OakRenderBackendHandle handle,
								struct OakRenderBackendInfo *out_info);
typedef bool (*OakBackendIsAvailableFn)(OakRenderBackendHandle handle);
typedef bool (*OakBackendInitFn)(OakRenderBackendHandle handle);
typedef void (*OakBackendInitWithContextFn)(OakRenderBackendHandle handle,
										void *context);
typedef void (*OakBackendPostInitFn)(OakRenderBackendHandle handle);
typedef void (*OakBackendPostDestroyFn)(OakRenderBackendHandle handle);
typedef void (*OakBackendDestroyInternalFn)(OakRenderBackendHandle handle);
typedef void (*OakBackendClearDestinationFn)(OakRenderBackendHandle handle,
										 void *texture, double r, double g,
										 double b, double a);
typedef void (*OakBackendCreateNativeTextureFn)(OakRenderBackendHandle handle,
									   int width, int height, int depth,
									   int format, int channel_count,
									   const void *data, int linesize,
									   void *out_variant);
typedef void (*OakBackendDestroyNativeTextureFn)(OakRenderBackendHandle handle,
										 const void *variant);
typedef void (*OakBackendCreateNativeShaderFn)(OakRenderBackendHandle handle,
									  const void *shader_code,
									  void *out_variant);
typedef void (*OakBackendDestroyNativeShaderFn)(OakRenderBackendHandle handle,
										const void *variant);
typedef void (*OakBackendUploadToTextureFn)(OakRenderBackendHandle handle,
									const void *variant,
									const void *video_params,
									const void *data, int linesize);
typedef void (*OakBackendDownloadFromTextureFn)(OakRenderBackendHandle handle,
									  const void *variant,
									  const void *video_params,
									  void *data, int linesize);
typedef void (*OakBackendFlushFn)(OakRenderBackendHandle handle);
typedef void (*OakBackendGetPixelFromTextureFn)(OakRenderBackendHandle handle,
									   void *texture, const void *point,
									   void *out_color);
typedef void (*OakBackendBlitFn)(OakRenderBackendHandle handle,
								 const void *shader, void *job,
								 void *destination,
								 const void *destination_params,
								 bool clear_destination);
typedef void *(*OakBackendOpenGLContextFn)(OakRenderBackendHandle handle);

#ifdef __cplusplus
}
#endif

#endif // RENDERBACKEND_C_H
