#include "render/backend/renderbackend_c.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QPointF>
#include <QVariant>

#include "render/job/acceleratedjob.h"
#include "render/shadercode.h"
#include "render/texture.h"
#include "render/videoparams.h"
#include "render/vulkan/vulkanrenderer.h"

namespace {

class BackendVulkanRenderer : public olive::VulkanRenderer {
public:
	using olive::VulkanRenderer::VulkanRenderer;
	using olive::VulkanRenderer::Blit;
	using olive::VulkanRenderer::CreateNativeTexture;
	using olive::VulkanRenderer::DestroyInternal;
	using olive::VulkanRenderer::DestroyNativeTexture;
};

BackendVulkanRenderer *Renderer(OakRenderBackendHandle handle)
{
	return static_cast<BackendVulkanRenderer *>(handle);
}

const QVariant &VariantRef(const void *variant)
{
	return *static_cast<const QVariant *>(variant);
}

} // namespace

OAK_RENDER_BACKEND_EXPORT OakRenderBackendHandle oak_renderer_create(void *parent)
{
	return new BackendVulkanRenderer(static_cast<QObject *>(parent));
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_destroy(OakRenderBackendHandle handle)
{
	delete Renderer(handle);
}

OAK_RENDER_BACKEND_EXPORT bool oak_renderer_get_info(
	OakRenderBackendHandle handle, OakRenderBackendInfo *out_info)
{
	if (!handle || !out_info) {
		return false;
	}
	out_info->abi_version = 1;
	out_info->kind = OAK_RENDER_BACKEND_VULKAN;
	out_info->capabilities = OAK_RENDER_BACKEND_CAP_TEXTURES |
		OAK_RENDER_BACKEND_CAP_SHADERS | OAK_RENDER_BACKEND_CAP_BLIT |
		OAK_RENDER_BACKEND_CAP_READBACK;
	out_info->name = "vulkan";
	out_info->status = Renderer(handle)->IsAvailable() ? "available" : "unavailable";
	return true;
}

OAK_RENDER_BACKEND_EXPORT bool oak_renderer_is_available(
	OakRenderBackendHandle handle)
{
	auto *r = Renderer(handle);
	if (!r || r->IsAvailable()) {
		return r && r->IsAvailable();
	}
	// Try to initialize if not already available
	if (r->Init()) {
		r->PostInit();
	}
	return r->IsAvailable();
}

OAK_RENDER_BACKEND_EXPORT bool oak_renderer_init(OakRenderBackendHandle handle)
{
	return Renderer(handle)->Init();
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_init_with_context(
	OakRenderBackendHandle handle, void *context)
{
	Q_UNUSED(context)
	Renderer(handle)->Init();
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_post_init(
	OakRenderBackendHandle handle)
{
	Renderer(handle)->PostInit();
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_post_destroy(
	OakRenderBackendHandle handle)
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
	Q_UNUSED(handle)
	return nullptr;
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_attach_output_texture(
	OakRenderBackendHandle handle, const void *texture_id)
{
	Q_UNUSED(handle)
	Q_UNUSED(texture_id)
	// Vulkan does not support OFX OpenGL render output attachment.
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_detach_output_texture(
	OakRenderBackendHandle handle)
{
	Q_UNUSED(handle)
	// Vulkan does not support OFX OpenGL render output attachment.
}
