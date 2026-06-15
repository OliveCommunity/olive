#ifndef DYNAMICRENDERER_H
#define DYNAMICRENDERER_H

#include <QLibrary>
#include <QString>

#include "render/backend/renderbackend_c.h"
#include "render/opengl/openglcontextprovider.h"
#include "render/renderer.h"

namespace olive
{

class DynamicRenderer : public Renderer, public OpenGLContextProvider {
	Q_OBJECT
public:
	explicit DynamicRenderer(const QString &backend, QObject *parent = nullptr);
	virtual ~DynamicRenderer() override;

	using Renderer::Blit;

	bool Load();
	bool InitWithOpenGLContext(QOpenGLContext *context);
	bool GetBackendInfo(OakRenderBackendInfo *out_info) const;
	QString backend_name() const
	{
		return backend_;
	}

	virtual bool Init() override;
	virtual void PostDestroy() override;
	virtual void PostInit() override;
	virtual void ClearDestination(Texture *texture = nullptr,
							  double r = 0.0, double g = 0.0,
							  double b = 0.0, double a = 0.0) override;
	virtual QVariant CreateNativeShader(ShaderCode code) override;
	virtual void DestroyNativeShader(QVariant shader) override;
	virtual void UploadToTexture(const QVariant &handle,
							 const VideoParams &params, const void *data,
							 int linesize) override;
	virtual void DownloadFromTexture(const QVariant &handle,
							   const VideoParams &params, void *data,
							   int linesize) override;
	virtual void Flush() override;
	virtual Color GetPixelFromTexture(Texture *texture,
								  const QPointF &pt) override;
	virtual QOpenGLContext *OpenGLContext() const override;

	virtual bool IsOpenGL() const override;

	virtual void AttachOutputTexture(Texture *texture) override;

	virtual void DetachOutputTexture() override;

protected:
	virtual void Blit(QVariant shader, AcceleratedJob &job,
				  Texture *destination, VideoParams destination_params,
				  bool clear_destination) override;
	virtual QVariant CreateNativeTexture(int width, int height, int depth,
								 PixelFormat format, int channel_count,
								 const void *data = nullptr,
								 int linesize = 0) override;
	virtual void DestroyNativeTexture(QVariant texture) override;
	virtual void DestroyInternal() override;

private:
	bool ResolveFunctions();
	bool FallbackToOpenGL();
	void ResetFunctions();
	QString LibraryFilename() const;

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

#endif // DYNAMICRENDERER_H
