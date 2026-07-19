#ifndef OAK_DYNAMICRENDERER_H
#define OAK_DYNAMICRENDERER_H

#include <QLibrary>
#include <QString>

#include "render/backend/renderbackend_c.h"
#include "render/opengl/openglcontextprovider.h"
#include "render/renderer.h"

namespace olive
{

// C++ Renderer adapter that loads an Oak render backend shared library and
// forwards Renderer calls through the backend's C ABI.
class DynamicRenderer : public Renderer, public OpenGLContextProvider {
	Q_OBJECT
public:
	// Stores the requested backend name; Load() may change it after fallback.
	explicit DynamicRenderer(const QString &backend, QObject *parent = nullptr);
	// Destroys backend resources and unloads the dynamic library.
	virtual ~DynamicRenderer() override;

	using Renderer::blit;

	// Loads the backend library, resolves C ABI symbols, and creates the handle.
	bool load();
	// Initializes an OpenGL backend with a caller-owned viewer context.
	bool init_with_open_gl_context(QOpenGLContext *context);
	// Retrieves backend metadata through the optional info entry point.
	bool get_backend_info(OakRenderBackendInfo *out_info) const;
	// Returns the effective backend after any load-time fallback.
	QString backend_name() const
	{
		return backend_;
	}

	// Initializes the backend using its default device/context path.
	virtual bool init() override;
	// Runs backend post-destroy cleanup.
	virtual void post_destroy() override;
	// Runs backend post-init setup.
	virtual void post_init() override;
	// Clears either a native texture destination or the backend output target.
	virtual void clear_destination(Texture *texture = nullptr, double r = 0.0,
								  double g = 0.0, double b = 0.0,
								  double a = 0.0) override;
	// Creates a native shader through the dynamic backend.
	virtual QVariant create_native_shader(ShaderCode code) override;
	// Destroys a native shader through the dynamic backend.
	virtual void destroy_native_shader(QVariant shader) override;
	// Uploads CPU pixels to a backend texture.
	virtual void upload_to_texture(const QVariant &handle,
								 const VideoParams &params, const void *data,
								 int linesize) override;
	// Downloads backend texture pixels to CPU memory.
	virtual void download_from_texture(const QVariant &handle,
									 const VideoParams &params, void *data,
									 int linesize) override;
	// Waits for backend work to complete.
	virtual void flush() override;
	// Reads one pixel from a backend texture.
	virtual Color get_pixel_from_texture(Texture *texture,
									  const QPointF &pt) override;
	// Returns the wrapped OpenGL context for OpenGL backends.
	virtual QOpenGLContext *open_gl_context() const override;

	// Reports whether the effective backend is OpenGL.
	virtual bool is_open_gl() const override;
	// Reports whether the effective backend is Vulkan.
	virtual bool is_vulkan() const override;

	// Attaches a texture for OFX OpenGL output when supported.
	virtual void attach_output_texture(Texture *texture) override;

	// Detaches any OFX output texture binding when supported.
	virtual void detach_output_texture() override;

protected:
	// Dispatches a shader blit through the dynamic backend.
	virtual void blit(QVariant shader, AcceleratedJob &job,
					  Texture *destination, VideoParams destination_params,
					  bool clear_destination) override;
	// Allocates a native texture through the dynamic backend.
	virtual QVariant create_native_texture(int width, int height, int depth,
										 PixelFormat format, int channel_count,
										 const void *data = nullptr,
										 int linesize = 0) override;
	// Releases a native texture through the dynamic backend.
	virtual void destroy_native_texture(QVariant texture) override;
	// Releases backend-owned renderer resources.
	virtual void destroy_internal() override;

private:
	// Resolves required backend C ABI symbols.
	bool resolve_functions();
	// Replaces a failed Vulkan backend with OpenGL.
	bool fallback_to_open_gl();
	// Clears all cached function pointers.
	void reset_functions();
	// Resolves the private backend library path.
	QString library_filename() const;

	QString backend_;
	QLibrary library_;
	OakRenderBackendHandle handle_ = nullptr;

	OakBackendCreateFn create_ = nullptr;
	OakBackendDestroyFn destroy_ = nullptr;
	OakBackendGetInfoFn get_info_ = nullptr;
	OakBackendIsAvailableFn is_available_ = nullptr;
	OakBackendInitFn init_ = nullptr;
	OakBackendInitWithContextFn init_with_context_ = nullptr;
	OakBackendPostInitFn post_init_ = nullptr;
	OakBackendPostDestroyFn post_destroy_ = nullptr;
	OakBackendDestroyInternalFn destroy_internal_ = nullptr;
	OakBackendClearDestinationFn clear_destination_ = nullptr;
	OakBackendCreateNativeTextureFn create_native_texture_ = nullptr;
	OakBackendDestroyNativeTextureFn destroy_native_texture_ = nullptr;
	OakBackendCreateNativeShaderFn create_native_shader_ = nullptr;
	OakBackendDestroyNativeShaderFn destroy_native_shader_ = nullptr;
	OakBackendUploadToTextureFn upload_to_texture_ = nullptr;
	OakBackendDownloadFromTextureFn download_from_texture_ = nullptr;
	OakBackendFlushFn flush_ = nullptr;
	OakBackendGetPixelFromTextureFn get_pixel_from_texture_ = nullptr;
	OakBackendBlitFn blit_ = nullptr;
	OakBackendAttachOutputTextureFn attach_output_texture_ = nullptr;
	OakBackendDetachOutputTextureFn detach_output_texture_ = nullptr;
	OakBackendOpenGLContextFn opengl_context_ = nullptr;
};

}

#endif // OAK_DYNAMICRENDERER_H
