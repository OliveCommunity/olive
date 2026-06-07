#include "render/backend/renderbackend_c.h"

#include <QOpenGLContext>
#include <QPointF>
#include <QVariant>

#include "render/job/acceleratedjob.h"
#include "render/opengl/openglrenderer.h"
#include "render/shadercode.h"
#include "render/texture.h"
#include "render/videoparams.h"

namespace {

class BackendOpenGLRenderer : public olive::OpenGLRenderer {
public:
	using olive::OpenGLRenderer::OpenGLRenderer;
	using olive::OpenGLRenderer::Blit;
	using olive::OpenGLRenderer::CreateNativeTexture;
	using olive::OpenGLRenderer::DestroyInternal;
	using olive::OpenGLRenderer::DestroyNativeTexture;
};

BackendOpenGLRenderer *Renderer(OakRenderBackendHandle handle)
{
	return static_cast<BackendOpenGLRenderer *>(handle);
}

const QVariant &VariantRef(const void *variant)
{
	return *static_cast<const QVariant *>(variant);
}

} // namespace

OAK_RENDER_BACKEND_EXPORT OakRenderBackendHandle oak_renderer_create(void *parent)
{
	return new BackendOpenGLRenderer(static_cast<QObject *>(parent));
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_destroy(OakRenderBackendHandle handle)
{
	delete Renderer(handle);
}

OAK_RENDER_BACKEND_EXPORT bool oak_renderer_is_available(
	OakRenderBackendHandle handle)
{
	return handle != nullptr;
}

OAK_RENDER_BACKEND_EXPORT bool oak_renderer_init(OakRenderBackendHandle handle)
{
	return Renderer(handle)->Init();
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_init_with_context(
	OakRenderBackendHandle handle, void *context)
{
	Renderer(handle)->Init(static_cast<QOpenGLContext *>(context));
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_post_init(OakRenderBackendHandle handle)
{
	Renderer(handle)->PostInit();
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_post_destroy(OakRenderBackendHandle handle)
{
	Renderer(handle)->PostDestroy();
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_destroy_internal(
	OakRenderBackendHandle handle)
{
	Renderer(handle)->DestroyInternal();
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_clear_destination(
	OakRenderBackendHandle handle, void *texture, double r, double g, double b,
	double a)
{
	Renderer(handle)->ClearDestination(static_cast<olive::Texture *>(texture),
									 r, g, b, a);
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_create_native_texture(
	OakRenderBackendHandle handle, int width, int height, int depth, int format,
	int channel_count, const void *data, int linesize, void *out_variant)
{
	*static_cast<QVariant *>(out_variant) = Renderer(handle)->CreateNativeTexture(
		width, height, depth, static_cast<olive::PixelFormat::Format>(format),
		channel_count, data, linesize);
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_destroy_native_texture(
	OakRenderBackendHandle handle, const void *variant)
{
	Renderer(handle)->DestroyNativeTexture(VariantRef(variant));
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_create_native_shader(
	OakRenderBackendHandle handle, const void *shader_code, void *out_variant)
{
	*static_cast<QVariant *>(out_variant) = Renderer(handle)->CreateNativeShader(
		*static_cast<const olive::ShaderCode *>(shader_code));
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_destroy_native_shader(
	OakRenderBackendHandle handle, const void *variant)
{
	Renderer(handle)->DestroyNativeShader(VariantRef(variant));
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_upload_to_texture(
	OakRenderBackendHandle handle, const void *variant, const void *video_params,
	const void *data, int linesize)
{
	Renderer(handle)->UploadToTexture(
		VariantRef(variant), *static_cast<const olive::VideoParams *>(video_params),
		data, linesize);
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_download_from_texture(
	OakRenderBackendHandle handle, const void *variant, const void *video_params,
	void *data, int linesize)
{
	Renderer(handle)->DownloadFromTexture(
		VariantRef(variant), *static_cast<const olive::VideoParams *>(video_params),
		data, linesize);
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_flush(OakRenderBackendHandle handle)
{
	Renderer(handle)->Flush();
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_get_pixel_from_texture(
	OakRenderBackendHandle handle, void *texture, const void *point,
	void *out_color)
{
	*static_cast<olive::Color *>(out_color) = Renderer(handle)->GetPixelFromTexture(
		static_cast<olive::Texture *>(texture), *static_cast<const QPointF *>(point));
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_blit(
	OakRenderBackendHandle handle, const void *shader, void *job,
	void *destination, const void *destination_params, bool clear_destination)
{
	Renderer(handle)->Blit(
		VariantRef(shader), *static_cast<olive::AcceleratedJob *>(job),
		static_cast<olive::Texture *>(destination),
		*static_cast<const olive::VideoParams *>(destination_params),
		clear_destination);
}

OAK_RENDER_BACKEND_EXPORT void *oak_renderer_opengl_context(
	OakRenderBackendHandle handle)
{
	return Renderer(handle)->context();
}
