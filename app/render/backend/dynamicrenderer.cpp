#include "dynamicrenderer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QOpenGLContext>

namespace olive
{

DynamicRenderer::DynamicRenderer(const QString &backend, QObject *parent)
	: Renderer(parent)
	, backend_(backend.toLower())
{
}

DynamicRenderer::~DynamicRenderer()
{
	Destroy();
	PostDestroy();
	if (handle_ && destroy_) {
		destroy_(handle_);
		handle_ = nullptr;
	}
	if (library_.isLoaded()) {
		library_.unload();
	}
}

QString DynamicRenderer::LibraryFilename() const
{
	const QString base = backend_ == QStringLiteral("vulkan")
		? QStringLiteral("oakvulkan")
		: QStringLiteral("oakgl");
#if defined(Q_OS_WIN)
	const QString filename = base + QStringLiteral(".dll");
#elif defined(Q_OS_MAC)
	const QString filename = QStringLiteral("lib") + base + QStringLiteral(".dylib");
#else
	const QString filename = QStringLiteral("lib") + base + QStringLiteral(".so");
#endif

	const QDir app_dir(QCoreApplication::applicationDirPath());
	const QStringList candidates = {
		app_dir.filePath(filename),
		app_dir.filePath(QDir(QStringLiteral("render_backends")).filePath(filename)),
		app_dir.filePath(QDir(QStringLiteral("../app")).filePath(filename)),
		app_dir.filePath(QDir(QStringLiteral("../../app")).filePath(filename))
	};
	for (const QString &candidate : candidates) {
		if (QFileInfo::exists(candidate)) {
			return candidate;
		}
	}
	return candidates.first();
}

bool DynamicRenderer::Load()
{
	if (handle_) {
		return true;
	}

	library_.setFileName(LibraryFilename());
	if (!library_.load()) {
		if (backend_ == QStringLiteral("vulkan")) {
			qWarning() << "Failed to load Vulkan render backend"
					   << library_.fileName() << library_.errorString()
					   << "falling back to OpenGL backend";
			backend_ = QStringLiteral("opengl");
			library_.setFileName(LibraryFilename());
		}

		if (!library_.load()) {
			qWarning() << "Failed to load render backend" << backend_
					   << library_.fileName() << library_.errorString();
			return false;
		}
	}

	if (!ResolveFunctions()) {
		qWarning() << "Render backend is missing required symbols" << backend_;
		library_.unload();
		return false;
	}

	handle_ = create_(this->parent());
	if (!handle_) {
		library_.unload();
		return false;
	}
	if (is_available_ && !is_available_(handle_)) {
		qWarning() << "Render backend is not available" << backend_
				   << library_.fileName();
		if (backend_ == QStringLiteral("vulkan")) {
			return FallbackToOpenGL();
		}
		destroy_(handle_);
		handle_ = nullptr;
		library_.unload();
		return false;
	}
	return handle_ != nullptr;
}

bool DynamicRenderer::ResolveFunctions()
{
	ResetFunctions();
#define RESOLVE(member, type, symbol) \
	member = reinterpret_cast<type>(library_.resolve(symbol)); \
	if (!member) return false

	RESOLVE(create_, OakBackendCreateFn, "oak_renderer_create");
	RESOLVE(destroy_, OakBackendDestroyFn, "oak_renderer_destroy");
	RESOLVE(init_, OakBackendInitFn, "oak_renderer_init");
	RESOLVE(init_with_context_, OakBackendInitWithContextFn,
			"oak_renderer_init_with_context");
	RESOLVE(post_init_, OakBackendPostInitFn, "oak_renderer_post_init");
	RESOLVE(post_destroy_, OakBackendPostDestroyFn,
			"oak_renderer_post_destroy");
	RESOLVE(destroy_internal_, OakBackendDestroyInternalFn,
			"oak_renderer_destroy_internal");
	RESOLVE(clear_destination_, OakBackendClearDestinationFn,
			"oak_renderer_clear_destination");
	RESOLVE(create_native_texture_, OakBackendCreateNativeTextureFn,
			"oak_renderer_create_native_texture");
	RESOLVE(destroy_native_texture_, OakBackendDestroyNativeTextureFn,
			"oak_renderer_destroy_native_texture");
	RESOLVE(create_native_shader_, OakBackendCreateNativeShaderFn,
			"oak_renderer_create_native_shader");
	RESOLVE(destroy_native_shader_, OakBackendDestroyNativeShaderFn,
			"oak_renderer_destroy_native_shader");
	RESOLVE(upload_to_texture_, OakBackendUploadToTextureFn,
			"oak_renderer_upload_to_texture");
	RESOLVE(download_from_texture_, OakBackendDownloadFromTextureFn,
			"oak_renderer_download_from_texture");
	RESOLVE(flush_, OakBackendFlushFn, "oak_renderer_flush");
	RESOLVE(get_pixel_from_texture_, OakBackendGetPixelFromTextureFn,
			"oak_renderer_get_pixel_from_texture");
	RESOLVE(blit_, OakBackendBlitFn, "oak_renderer_blit");
	RESOLVE(opengl_context_, OakBackendOpenGLContextFn,
			"oak_renderer_opengl_context");
#undef RESOLVE
	is_available_ = reinterpret_cast<OakBackendIsAvailableFn>(
		library_.resolve("oak_renderer_is_available"));
	return true;
}

bool DynamicRenderer::FallbackToOpenGL()
{
	if (handle_ && destroy_) {
		destroy_(handle_);
		handle_ = nullptr;
	}
	if (library_.isLoaded()) {
		library_.unload();
	}
	ResetFunctions();
	backend_ = QStringLiteral("opengl");
	return Load();
}

void DynamicRenderer::ResetFunctions()
{
	create_ = nullptr;
	destroy_ = nullptr;
	is_available_ = nullptr;
	init_ = nullptr;
	init_with_context_ = nullptr;
	post_init_ = nullptr;
	post_destroy_ = nullptr;
	destroy_internal_ = nullptr;
	clear_destination_ = nullptr;
	create_native_texture_ = nullptr;
	destroy_native_texture_ = nullptr;
	create_native_shader_ = nullptr;
	destroy_native_shader_ = nullptr;
	upload_to_texture_ = nullptr;
	download_from_texture_ = nullptr;
	flush_ = nullptr;
	get_pixel_from_texture_ = nullptr;
	blit_ = nullptr;
	opengl_context_ = nullptr;
}

bool DynamicRenderer::Init()
{
	return Load() && init_(handle_);
}

bool DynamicRenderer::InitWithOpenGLContext(QOpenGLContext *context)
{
	if (!Load()) {
		return false;
	}
	init_with_context_(handle_, context);
	return true;
}

void DynamicRenderer::PostDestroy()
{
	if (handle_ && post_destroy_) {
		post_destroy_(handle_);
	}
}

void DynamicRenderer::PostInit()
{
	if (handle_) {
		post_init_(handle_);
	}
}

void DynamicRenderer::ClearDestination(Texture *texture, double r, double g,
								   double b, double a)
{
	clear_destination_(handle_, texture, r, g, b, a);
}

QVariant DynamicRenderer::CreateNativeShader(ShaderCode code)
{
	QVariant out;
	create_native_shader_(handle_, &code, &out);
	return out;
}

void DynamicRenderer::DestroyNativeShader(QVariant shader)
{
	destroy_native_shader_(handle_, &shader);
}

void DynamicRenderer::UploadToTexture(const QVariant &handle,
								  const VideoParams &params, const void *data,
								  int linesize)
{
	upload_to_texture_(handle_, &handle, &params, data, linesize);
}

void DynamicRenderer::DownloadFromTexture(const QVariant &handle,
									const VideoParams &params, void *data,
									int linesize)
{
	download_from_texture_(handle_, &handle, &params, data, linesize);
}

void DynamicRenderer::Flush()
{
	flush_(handle_);
}

Color DynamicRenderer::GetPixelFromTexture(Texture *texture, const QPointF &pt)
{
	Color out;
	get_pixel_from_texture_(handle_, texture, &pt, &out);
	return out;
}

QOpenGLContext *DynamicRenderer::OpenGLContext() const
{
	return opengl_context_ && handle_
		? static_cast<QOpenGLContext *>(opengl_context_(handle_))
		: nullptr;
}

void DynamicRenderer::Blit(QVariant shader, AcceleratedJob &job,
					   Texture *destination, VideoParams destination_params,
					   bool clear_destination)
{
	blit_(handle_, &shader, &job, destination, &destination_params,
		  clear_destination);
}

QVariant DynamicRenderer::CreateNativeTexture(int width, int height, int depth,
									 PixelFormat format, int channel_count,
									 const void *data, int linesize)
{
	QVariant out;
	create_native_texture_(handle_, width, height, depth, format, channel_count,
					   data, linesize, &out);
	return out;
}

void DynamicRenderer::DestroyNativeTexture(QVariant texture)
{
	destroy_native_texture_(handle_, &texture);
}

void DynamicRenderer::DestroyInternal()
{
	if (handle_) {
		destroy_internal_(handle_);
	}
}

}
