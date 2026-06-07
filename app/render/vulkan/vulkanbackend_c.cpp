#include "render/backend/renderbackend_c.h"

#include <QDebug>
#include <QVariant>

namespace {

struct VulkanBackend {
	bool warned = false;
};

VulkanBackend *Backend(OakRenderBackendHandle handle)
{
	return static_cast<VulkanBackend *>(handle);
}

void WarnUnavailable(OakRenderBackendHandle handle, const char *function)
{
	auto *backend = Backend(handle);
	if (!backend || backend->warned) {
		return;
	}
	backend->warned = true;
	qWarning() << "Vulkan render backend is a C ABI placeholder; function"
			   << function << "is not implemented yet";
}

} // namespace

OAK_RENDER_BACKEND_EXPORT OakRenderBackendHandle oak_renderer_create(void *parent)
{
	Q_UNUSED(parent)
	return new VulkanBackend();
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_destroy(OakRenderBackendHandle handle)
{
	delete Backend(handle);
}

OAK_RENDER_BACKEND_EXPORT bool oak_renderer_is_available(
	OakRenderBackendHandle handle)
{
	Q_UNUSED(handle)
	return false;
}

OAK_RENDER_BACKEND_EXPORT bool oak_renderer_init(OakRenderBackendHandle handle)
{
	WarnUnavailable(handle, "init");
	return false;
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_init_with_context(
	OakRenderBackendHandle handle, void *context)
{
	Q_UNUSED(context)
	WarnUnavailable(handle, "init_with_context");
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_post_init(
	OakRenderBackendHandle handle)
{
	Q_UNUSED(handle)
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_post_destroy(
	OakRenderBackendHandle handle)
{
	Q_UNUSED(handle)
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_destroy_internal(
	OakRenderBackendHandle handle)
{
	Q_UNUSED(handle)
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_clear_destination(
	OakRenderBackendHandle handle, void *texture, double r, double g, double b,
	double a)
{
	Q_UNUSED(texture)
	Q_UNUSED(r)
	Q_UNUSED(g)
	Q_UNUSED(b)
	Q_UNUSED(a)
	WarnUnavailable(handle, "clear_destination");
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_create_native_texture(
	OakRenderBackendHandle handle, int width, int height, int depth, int format,
	int channel_count, const void *data, int linesize, void *out_variant)
{
	Q_UNUSED(width)
	Q_UNUSED(height)
	Q_UNUSED(depth)
	Q_UNUSED(format)
	Q_UNUSED(channel_count)
	Q_UNUSED(data)
	Q_UNUSED(linesize)
	static_cast<QVariant *>(out_variant)->clear();
	WarnUnavailable(handle, "create_native_texture");
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_destroy_native_texture(
	OakRenderBackendHandle handle, const void *variant)
{
	Q_UNUSED(variant)
	WarnUnavailable(handle, "destroy_native_texture");
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_create_native_shader(
	OakRenderBackendHandle handle, const void *shader_code, void *out_variant)
{
	Q_UNUSED(shader_code)
	static_cast<QVariant *>(out_variant)->clear();
	WarnUnavailable(handle, "create_native_shader");
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_destroy_native_shader(
	OakRenderBackendHandle handle, const void *variant)
{
	Q_UNUSED(variant)
	WarnUnavailable(handle, "destroy_native_shader");
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_upload_to_texture(
	OakRenderBackendHandle handle, const void *variant, const void *video_params,
	const void *data, int linesize)
{
	Q_UNUSED(variant)
	Q_UNUSED(video_params)
	Q_UNUSED(data)
	Q_UNUSED(linesize)
	WarnUnavailable(handle, "upload_to_texture");
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_download_from_texture(
	OakRenderBackendHandle handle, const void *variant, const void *video_params,
	void *data, int linesize)
{
	Q_UNUSED(variant)
	Q_UNUSED(video_params)
	Q_UNUSED(data)
	Q_UNUSED(linesize)
	WarnUnavailable(handle, "download_from_texture");
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_flush(OakRenderBackendHandle handle)
{
	Q_UNUSED(handle)
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_get_pixel_from_texture(
	OakRenderBackendHandle handle, void *texture, const void *point,
	void *out_color)
{
	Q_UNUSED(texture)
	Q_UNUSED(point)
	Q_UNUSED(out_color)
	WarnUnavailable(handle, "get_pixel_from_texture");
}

OAK_RENDER_BACKEND_EXPORT void oak_renderer_blit(
	OakRenderBackendHandle handle, const void *shader, void *job,
	void *destination, const void *destination_params, bool clear_destination)
{
	Q_UNUSED(shader)
	Q_UNUSED(job)
	Q_UNUSED(destination)
	Q_UNUSED(destination_params)
	Q_UNUSED(clear_destination)
	WarnUnavailable(handle, "blit");
}

OAK_RENDER_BACKEND_EXPORT void *oak_renderer_opengl_context(
	OakRenderBackendHandle handle)
{
	Q_UNUSED(handle)
	return nullptr;
}
