#include "render/backend/renderbackend_c.h"

#include "mathtypes.h"
#include "variant.h"

#include "render/job/acceleratedjob.h"
#include "render/opengl/openglrenderer.h"
#include "render/shadercode.h"
#include "render/texture.h"
#include "videoparams.h"

namespace
{

class BackendOpenGLRenderer : public olive::OpenGLRenderer {
public:
	using olive::OpenGLRenderer::OpenGLRenderer;
	using olive::OpenGLRenderer::blit;
	using olive::OpenGLRenderer::create_native_texture;
	using olive::OpenGLRenderer::destroy_internal;
	using olive::OpenGLRenderer::destroy_native_texture;
	using olive::OpenGLRenderer::attach_texture_as_destination;
	using olive::OpenGLRenderer::detach_texture_as_destination;
};

// Converts the opaque C ABI handle back to the C++ renderer used internally.
BackendOpenGLRenderer *renderer(OakRenderBackendHandle handle)
{
	return static_cast<BackendOpenGLRenderer *>(handle);
}

// Interprets ABI Variant payloads without copying; both modules are built
// against the same C++ ABI in this first-generation dynamic backend.
const olive::Variant &variant_ref(const void *variant)
{
	return *static_cast<const olive::Variant *>(variant);
}

} // namespace

// Creates the backend object and returns it as an opaque C handle. The
// QObject-style parent argument is retained for ABI parity and ignored.
OAK_RENDER_BACKEND_EXPORT OakRenderBackendHandle
oak_renderer_create(void *parent)
{
	(void) parent;
	return new BackendOpenGLRenderer();
}

// Destroys the opaque backend object created by oak_renderer_create().
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_destroy(OakRenderBackendHandle handle)
{
	delete renderer(handle);
}

// Reports static OpenGL backend capabilities to the adapter.
OAK_RENDER_BACKEND_EXPORT bool
oak_renderer_get_info(OakRenderBackendHandle handle,
					  OakRenderBackendInfo *out_info)
{
	if (!handle || !out_info) {
		return false;
	}
	out_info->abi_version = 1;
	out_info->kind = oak_render_backend_opengl;
	out_info->capabilities =
		oak_render_backend_cap_textures | oak_render_backend_cap_shaders |
		oak_render_backend_cap_blit | oak_render_backend_cap_readback |
		oak_render_backend_cap_viewer_context;
	out_info->name = "opengl";
	out_info->status = "available";
	return true;
}

// OpenGL availability is context-dependent, so object creation is the minimum
// availability signal for this backend.
OAK_RENDER_BACKEND_EXPORT bool
oak_renderer_is_available(OakRenderBackendHandle handle)
{
	return handle != nullptr;
}

// Initializes an offscreen OpenGL context for non-viewer users.
OAK_RENDER_BACKEND_EXPORT bool oak_renderer_init(OakRenderBackendHandle handle)
{
	return renderer(handle)->init();
}

// Initializes the backend against a caller-owned viewer OpenGL context.
// `context` is an olive::OpenGLContext * adopted from the app layer.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_init_with_context(OakRenderBackendHandle handle, void *context)
{
	renderer(handle)->init(static_cast<olive::OpenGLContext *>(context));
}

// Runs renderer post-initialization once the GL context is available.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_post_init(OakRenderBackendHandle handle)
{
	renderer(handle)->post_init();
}

// Releases post-init OpenGL surface/context state.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_post_destroy(OakRenderBackendHandle handle)
{
	renderer(handle)->post_destroy();
}

// Releases renderer-owned GL resources before object destruction.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_destroy_internal(OakRenderBackendHandle handle)
{
	renderer(handle)->destroy_internal();
}

// Clears either the widget framebuffer or a texture destination.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_clear_destination(OakRenderBackendHandle handle, void *texture,
							   double r, double g, double b, double a)
{
	renderer(handle)->clear_destination(static_cast<olive::Texture *>(texture),
									   r, g, b, a);
}

// Creates an OpenGL texture and writes its Variant handle to out_variant.
OAK_RENDER_BACKEND_EXPORT void oak_renderer_create_native_texture(
	OakRenderBackendHandle handle, int width, int height, int depth, int format,
	int channel_count, const void *data, int linesize, void *out_variant)
{
	*static_cast<olive::Variant *>(out_variant) =
		renderer(handle)->create_native_texture(
			width, height, depth,
			static_cast<olive::PixelFormat::Format>(format), channel_count,
			data, linesize);
}

// Destroys an OpenGL texture represented by a Variant handle.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_destroy_native_texture(OakRenderBackendHandle handle,
									const void *variant)
{
	renderer(handle)->destroy_native_texture(variant_ref(variant));
}

// Compiles an OpenGL shader program and returns its Variant handle.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_create_native_shader(OakRenderBackendHandle handle,
								  const void *shader_code, void *out_variant)
{
	*static_cast<olive::Variant *>(out_variant) =
		renderer(handle)->create_native_shader(
			*static_cast<const olive::ShaderCode *>(shader_code));
}

// Destroys an OpenGL shader program represented by a Variant handle.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_destroy_native_shader(OakRenderBackendHandle handle,
								   const void *variant)
{
	renderer(handle)->destroy_native_shader(variant_ref(variant));
}

// Uploads CPU pixel data into an OpenGL texture.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_upload_to_texture(OakRenderBackendHandle handle,
							   const void *variant, const void *video_params,
							   const void *data, int linesize)
{
	renderer(handle)->upload_to_texture(
		variant_ref(variant),
		*static_cast<const olive::VideoParams *>(video_params), data, linesize);
}

// Reads an OpenGL texture back to CPU memory.
OAK_RENDER_BACKEND_EXPORT void oak_renderer_download_from_texture(
	OakRenderBackendHandle handle, const void *variant,
	const void *video_params, void *data, int linesize)
{
	renderer(handle)->download_from_texture(
		variant_ref(variant),
		*static_cast<const olive::VideoParams *>(video_params), data, linesize);
}

// Flushes/waits for pending OpenGL work as required by the renderer.
OAK_RENDER_BACKEND_EXPORT void oak_renderer_flush(OakRenderBackendHandle handle)
{
	renderer(handle)->flush();
}

// Reads one pixel from an OpenGL texture.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_get_pixel_from_texture(OakRenderBackendHandle handle,
									void *texture, const void *point,
									void *out_color)
{
	*static_cast<olive::Color *>(out_color) =
		renderer(handle)->get_pixel_from_texture(
			static_cast<olive::Texture *>(texture),
			*static_cast<const olive::PointF *>(point));
}

// Executes a shader blit through the wrapped C++ OpenGL renderer.
OAK_RENDER_BACKEND_EXPORT void oak_renderer_blit(OakRenderBackendHandle handle,
												 const void *shader, void *job,
												 void *destination,
												 const void *destination_params,
												 bool clear_destination)
{
	renderer(handle)->blit(
		variant_ref(shader), *static_cast<olive::AcceleratedJob *>(job),
		static_cast<olive::Texture *>(destination),
		*static_cast<const olive::VideoParams *>(destination_params),
		clear_destination);
}

// Exposes the wrapped OpenGL context for GL-specific integrations.
OAK_RENDER_BACKEND_EXPORT void *
oak_renderer_opengl_context(OakRenderBackendHandle handle)
{
	return renderer(handle)->context();
}

// Binds an output texture for OFX OpenGL rendering.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_attach_output_texture(OakRenderBackendHandle handle,
								   const void *texture_id)
{
	renderer(handle)->attach_texture_as_destination(variant_ref(texture_id));
}

// Detaches any OFX OpenGL output texture binding.
OAK_RENDER_BACKEND_EXPORT void
oak_renderer_detach_output_texture(OakRenderBackendHandle handle)
{
	renderer(handle)->detach_texture_as_destination();
}
