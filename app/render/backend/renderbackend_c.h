#ifndef RENDERBACKEND_C_H
#define RENDERBACKEND_C_H

#include <stdbool.h>

#ifdef _WIN32
#define OAK_RENDER_BACKEND_EXPORT extern "C" __declspec(dllexport)
#else
#define OAK_RENDER_BACKEND_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void *OakRenderBackendHandle;

typedef OakRenderBackendHandle (*OakBackendCreateFn)(void *parent);
typedef void (*OakBackendDestroyFn)(OakRenderBackendHandle handle);
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
