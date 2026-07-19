#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QVariant>
#include <QVector2D>

#include "codec/conformmanager.h"
#include "config/config.h"
#include "node/color/colormanager/colormanager.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "render/backend/dynamicrenderer.h"
#include "render/colorprocessor.h"
#include "render/diskmanager.h"
#include "render/job/colortransformjob.h"
#include "render/job/shaderjob.h"
#include "render/previewautocacher.h"
#include "render/renderer.h"
#include "render/rendermanager.h"
#include "render/texture.h"
#include "render/videoparams.h"

namespace
{

// CPU-only olive::Renderer implementation that records every call so the
// renderer-core code paths (texture cache, shader cache, color management
// fallback) can be verified without a GL/Vulkan backend.
class StubRenderer : public olive::Renderer {
public:
	StubRenderer()
		: create_texture_count(0)
		, destroy_texture_count(0)
		, create_shader_count(0)
		, destroy_shader_count(0)
		, upload_count(0)
		, download_count(0)
		, flush_count(0)
		, blit_count(0)
		, destroy_internal_count(0)
		, next_handle(0)
		, fail_create_texture(false)
		, fail_create_shader(false)
		, last_create_width(0)
		, last_create_height(0)
		, last_create_depth(0)
		, last_create_channel_count(0)
		, last_create_data(nullptr)
		, last_create_linesize(0)
		, last_upload_linesize(0)
		, last_download_linesize(0)
		, last_blit_destination(nullptr)
		, last_blit_clear(false)
	{
	}

	bool init() override
	{
		return true;
	}

	void post_destroy() override
	{
	}

	void post_init() override
	{
	}

	void clear_destination(olive::Texture *texture, double r, double g, double b,
						  double a) override
	{
	}

	QVariant create_native_shader(olive::ShaderCode code) override
	{
		create_shader_count++;
		if (fail_create_shader) {
			return QVariant();
		}
		return QVariant(QStringLiteral("shader%1").arg(create_shader_count));
	}

	void destroy_native_shader(QVariant shader) override
	{
		destroy_shader_count++;
	}

	void upload_to_texture(const QVariant &handle, const olive::VideoParams &params,
						 const void *data, int linesize) override
	{
		upload_count++;
		last_upload_handle = handle;
		last_upload_linesize = linesize;
	}

	void download_from_texture(const QVariant &handle,
							 const olive::VideoParams &params, void *data,
							 int linesize) override
	{
		download_count++;
		last_download_handle = handle;
		last_download_linesize = linesize;
	}

	void flush() override
	{
		flush_count++;
	}

	olive::Color get_pixel_from_texture(olive::Texture *texture,
									 const QPointF &pt) override
	{
		return olive::Color();
	}

	int create_texture_count;
	int destroy_texture_count;
	int create_shader_count;
	int destroy_shader_count;
	int upload_count;
	int download_count;
	int flush_count;
	int blit_count;
	int destroy_internal_count;

	int next_handle;

	bool fail_create_texture;
	bool fail_create_shader;

	int last_create_width;
	int last_create_height;
	int last_create_depth;
	int last_create_channel_count;
	const void *last_create_data;
	int last_create_linesize;

	QVariant last_upload_handle;
	int last_upload_linesize;
	QVariant last_download_handle;
	int last_download_linesize;

	QVariant last_blit_shader;
	olive::Texture *last_blit_destination;
	olive::VideoParams last_blit_params;
	bool last_blit_clear;

protected:
	void blit(QVariant shader, olive::AcceleratedJob &job,
			  olive::Texture *destination,
			  olive::VideoParams destination_params,
			  bool clear_destination) override
	{
		blit_count++;
		last_blit_shader = shader;
		last_blit_destination = destination;
		last_blit_params = destination_params;
		last_blit_clear = clear_destination;
	}

	QVariant create_native_texture(int width, int height, int depth,
								 olive::PixelFormat format, int channel_count,
								 const void *data, int linesize) override
	{
		create_texture_count++;
		last_create_width = width;
		last_create_height = height;
		last_create_depth = depth;
		last_create_channel_count = channel_count;
		last_create_data = data;
		last_create_linesize = linesize;
		if (fail_create_texture) {
			return QVariant();
		}
		return QVariant(++next_handle);
	}

	void destroy_native_texture(QVariant texture) override
	{
		destroy_texture_count++;
	}

	void destroy_internal() override
	{
		destroy_internal_count++;
	}
};

olive::ColorProcessorPtr create_identity_processor()
{
	olive::ColorManager::set_up_default_config();

	ocio::MatrixTransformRcPtr transform = ocio::MatrixTransform::Create();
	transform->setDirection(ocio::TRANSFORM_DIR_FORWARD);

	return olive::ColorProcessor::create(
		olive::ColorManager::get_default_config()->getProcessor(transform));
}

} // namespace

// Verifies the DynamicRenderer constructor stores the requested backend name
// lowercased and that IsOpenGL()/IsVulkan() reflect it before any Load().
TEST(DynamicRenderer, ConstructorNormalizesBackendName)
{
	olive::DynamicRenderer gl(QStringLiteral("OpenGL"));
	EXPECT_EQ(gl.backend_name(), QStringLiteral("opengl"));
	EXPECT_TRUE(gl.is_open_gl());
	EXPECT_FALSE(gl.is_vulkan());

	olive::DynamicRenderer vk(QStringLiteral("VULKAN"));
	EXPECT_EQ(vk.backend_name(), QStringLiteral("vulkan"));
	EXPECT_TRUE(vk.is_vulkan());
	EXPECT_FALSE(vk.is_open_gl());
}

// Documents that an unrecognized backend name is kept verbatim (and
// LibraryFilename() uses that verbatim name as the library basename, so Load()
// fails and the OpenGL fallback engages), so the IsOpenGL()/IsVulkan()
// predicates both report false for it.
TEST(DynamicRenderer, UnknownBackendNameIsReportedVerbatim)
{
	olive::DynamicRenderer renderer(QStringLiteral("Metal"));
	EXPECT_EQ(renderer.backend_name(), QStringLiteral("metal"));
	EXPECT_FALSE(renderer.is_open_gl());
	EXPECT_FALSE(renderer.is_vulkan());
}

// Before Load() succeeds there is no backend handle, so the metadata and
// context accessors must return safe defaults instead of dereferencing null
// function pointers.
TEST(DynamicRenderer, AccessorsBeforeLoadReturnDefaults)
{
	olive::DynamicRenderer renderer(QStringLiteral("opengl"));

	EXPECT_EQ(renderer.open_gl_context(), nullptr);

	OakRenderBackendInfo info = {};
	EXPECT_FALSE(renderer.get_backend_info(&info));
	EXPECT_FALSE(renderer.get_backend_info(nullptr));
}

// Lifecycle entry points must tolerate being called without a loaded backend:
// each one guards on the null handle/function table and does nothing.
TEST(DynamicRenderer, PreLoadLifecycleCallsAreSafeNoOps)
{
	olive::DynamicRenderer renderer(QStringLiteral("opengl"));

	renderer.post_init();
	renderer.post_destroy();
	renderer.attach_output_texture(nullptr);
	renderer.detach_output_texture();
	renderer.destroy();
	// Destruction after an explicit Destroy() must also be safe.
}

// Once a backend is loaded, Load() must short-circuit on the existing handle
// instead of reloading the library a second time.
TEST(DynamicRenderer, SecondLoadReturnsImmediately)
{
#ifndef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	GTEST_SKIP() << "Dynamic render backend is not enabled in this build";
#else
	olive::DynamicRenderer renderer(QStringLiteral("opengl"));
	if (!renderer.load()) {
		GTEST_SKIP()
			<< "opengl backend library could not be loaded in this environment";
	}

	EXPECT_TRUE(renderer.load());
	EXPECT_TRUE(renderer.is_open_gl());
	EXPECT_EQ(renderer.backend_name(), QStringLiteral("opengl"));
#endif
}

// Renderer::CreateTexture must wrap the native handle produced by
// CreateNativeTexture into a live Texture that mirrors the requested params.
TEST(RendererTextureCache, CreateTextureWrapsNativeHandle)
{
	StubRenderer renderer;
	const olive::VideoParams params(64, 32, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);

	olive::TexturePtr texture = renderer.create_texture(params);
	ASSERT_NE(texture, nullptr);
	EXPECT_FALSE(texture->is_dummy());
	EXPECT_EQ(texture->id(), QVariant(1));
	EXPECT_EQ(texture->width(), 64);
	EXPECT_EQ(texture->height(), 32);
	EXPECT_EQ(texture->format(), olive::PixelFormat::u8);
	EXPECT_EQ(texture->channel_count(),
			  int(olive::VideoParams::k_rgba_channel_count));
	EXPECT_EQ(renderer.create_texture_count, 1);

	renderer.destroy();
}

// A null native handle (backend allocation failure) must propagate as a null
// TexturePtr rather than a dummy texture.
TEST(RendererTextureCache, FailedNativeTextureCreateReturnsNull)
{
	StubRenderer renderer;
	renderer.fail_create_texture = true;

	const olive::VideoParams params(64, 64, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);
	EXPECT_EQ(renderer.create_texture(params), nullptr);
	EXPECT_EQ(renderer.create_texture_count, 1);
}

// Destroying a texture returns its native handle to the cache; a matching
// CreateTexture must reuse it without calling into the backend again, and
// flush so pending backend work is visible to the recycled texture.
TEST(RendererTextureCache, DestroyedTextureIsReusedFromCache)
{
	StubRenderer renderer;
	const olive::VideoParams params(64, 64, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);

	{
		olive::TexturePtr texture = renderer.create_texture(params);
		ASSERT_NE(texture, nullptr);
		EXPECT_EQ(texture->id(), QVariant(1));
	}
	EXPECT_EQ(renderer.create_texture_count, 1);

	olive::TexturePtr reused = renderer.create_texture(params);
	ASSERT_NE(reused, nullptr);
	EXPECT_EQ(reused->id(), QVariant(1));
	EXPECT_EQ(renderer.create_texture_count, 1);
	EXPECT_EQ(renderer.flush_count, 1);

	// A different size must miss the cache and allocate natively again.
	const olive::VideoParams other(32, 32, olive::PixelFormat::u8,
								   olive::VideoParams::k_rgba_channel_count);
	olive::TexturePtr fresh = renderer.create_texture(other);
	ASSERT_NE(fresh, nullptr);
	EXPECT_EQ(fresh->id(), QVariant(2));
	EXPECT_EQ(renderer.create_texture_count, 2);

	reused.reset();
	fresh.reset();
	renderer.destroy();
	EXPECT_EQ(renderer.destroy_texture_count, 2);
}

// When pixel data is supplied for a cache hit, the renderer must upload it
// into the recycled native handle rather than reallocating.
TEST(RendererTextureCache, CachedTextureReusedWithDataTriggersUpload)
{
	StubRenderer renderer;
	const olive::VideoParams params(16, 16, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);

	{
		olive::TexturePtr texture = renderer.create_texture(params);
		ASSERT_NE(texture, nullptr);
	}

	char data[16 * 16 * 4] = {};
	olive::TexturePtr texture =
		renderer.create_texture(params, data, 16 * 4);
	ASSERT_NE(texture, nullptr);
	EXPECT_EQ(renderer.create_texture_count, 1);
	EXPECT_EQ(renderer.upload_count, 1);
	EXPECT_EQ(renderer.last_upload_handle, QVariant(1));
	EXPECT_EQ(renderer.last_upload_linesize, 16 * 4);

	texture.reset();
	renderer.destroy();
}

// On a cache miss with initial data, the data pointer and linesize must be
// forwarded to CreateNativeTexture untouched.
TEST(RendererTextureCache, CreateWithDataForwardsToNativeCreate)
{
	StubRenderer renderer;
	const olive::VideoParams params(8, 8, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);

	char data[8 * 8 * 4] = {};
	olive::TexturePtr texture = renderer.create_texture(params, data, 8 * 4);
	ASSERT_NE(texture, nullptr);
	EXPECT_EQ(renderer.create_texture_count, 1);
	EXPECT_EQ(renderer.last_create_data, static_cast<const void *>(data));
	EXPECT_EQ(renderer.last_create_linesize, 8 * 4);
	EXPECT_EQ(renderer.last_create_width, 8);
	EXPECT_EQ(renderer.last_create_height, 8);
	EXPECT_EQ(renderer.last_create_channel_count,
			  int(olive::VideoParams::k_rgba_channel_count));
	EXPECT_EQ(renderer.upload_count, 0);

	texture.reset();
	renderer.destroy();
}

// GetDefaultShader() must compile the built-in shader once and cache it;
// Destroy() releases it exactly once through DestroyNativeShader.
TEST(RendererShaderCache, DefaultShaderCreatedOnceAndReleasedOnDestroy)
{
	StubRenderer renderer;

	const QVariant first = renderer.get_default_shader();
	const QVariant second = renderer.get_default_shader();
	EXPECT_FALSE(first.isNull());
	EXPECT_EQ(first, second);
	EXPECT_EQ(renderer.create_shader_count, 1);

	renderer.destroy();
	EXPECT_EQ(renderer.destroy_shader_count, 1);
	EXPECT_EQ(renderer.destroy_internal_count, 1);

	// A repeated Destroy() must not release the shader a second time.
	renderer.destroy();
	EXPECT_EQ(renderer.destroy_shader_count, 1);
	EXPECT_EQ(renderer.destroy_internal_count, 2);
}

// Texture::Upload/Download must forward the native id, params, and linesize
// to the owning renderer while it is alive.
TEST(TextureIo, UploadDownloadForwardToRenderer)
{
	StubRenderer renderer;
	const olive::VideoParams params(16, 16, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);

	olive::TexturePtr texture = renderer.create_texture(params);
	ASSERT_NE(texture, nullptr);

	char data[16 * 16 * 4] = {};
	texture->upload(data, 16 * 4);
	EXPECT_EQ(renderer.upload_count, 1);
	EXPECT_EQ(renderer.last_upload_handle, texture->id());
	EXPECT_EQ(renderer.last_upload_linesize, 16 * 4);

	texture->download(data, 16 * 4);
	EXPECT_EQ(renderer.download_count, 1);
	EXPECT_EQ(renderer.last_download_handle, texture->id());
	EXPECT_EQ(renderer.last_download_linesize, 16 * 4);

	texture.reset();
	renderer.destroy();
}

// A dummy texture (no backend renderer) must expose its params, report
// IsDummy(), and make Upload/Download harmless no-ops.
TEST(TextureDummy, AccessorsAndNoOpIo)
{
	const olive::VideoParams params(128, 64, olive::PixelFormat::f16,
									olive::VideoParams::k_rgba_channel_count);
	olive::Texture texture(params);

	EXPECT_TRUE(texture.is_dummy());
	EXPECT_EQ(texture.renderer(), nullptr);
	EXPECT_TRUE(texture.id().isNull());
	EXPECT_EQ(texture.width(), 128);
	EXPECT_EQ(texture.height(), 64);
	EXPECT_EQ(texture.virtual_resolution(), QVector2D(128, 64));
	EXPECT_EQ(texture.format(), olive::PixelFormat::f16);
	EXPECT_EQ(texture.channel_count(),
			  int(olive::VideoParams::k_rgba_channel_count));
	EXPECT_EQ(texture.divider(), 1);
	EXPECT_EQ(texture.pixel_aspect_ratio(), olive::Rational(1));
	EXPECT_FALSE(texture.is_job());
	EXPECT_EQ(texture.job(), nullptr);

	char data[4] = {};
	texture.upload(data, 4);
	texture.download(data, 4);
}

// Textures can carry a CPU-side AcceleratedJob instead of a native handle;
// the job must be owned and retrievable through job().
TEST(TextureJob, JobTextureExposesJob)
{
	const olive::VideoParams params(32, 32, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);
	olive::TexturePtr texture =
		olive::Texture::job(params, olive::ShaderJob());

	ASSERT_NE(texture, nullptr);
	EXPECT_TRUE(texture->is_dummy());
	EXPECT_TRUE(texture->is_job());
	ASSERT_NE(texture->job(), nullptr);
	EXPECT_EQ(texture->params().width(), 32);
}

// When the color-management shader cannot be compiled, BlitColorManaged must
// fall back to a plain textured blit with the default shader instead of
// failing silently or crashing.
TEST(RendererColorManagement, FallsBackToDefaultShaderWhenCompilationFails)
{
	StubRenderer renderer;
	renderer.fail_create_shader = true;

	olive::ColorProcessorPtr processor = create_identity_processor();
	ASSERT_TRUE(processor);

	olive::ColorTransformJob job;
	job.set_color_processor(processor);

	const olive::VideoParams params(64, 64, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);
	renderer.blit_color_managed(job, params);

	// One blit to the null destination overload, with the (failed) default
	// shader handle and the job's clear-destination flag preserved.
	EXPECT_EQ(renderer.blit_count, 1);
	EXPECT_EQ(renderer.last_blit_destination, nullptr);
	EXPECT_TRUE(renderer.last_blit_shader.isNull());
	EXPECT_TRUE(renderer.last_blit_clear);
	EXPECT_EQ(renderer.last_blit_params.width(), 64);

	renderer.destroy();
}

// GetColorContext must cache the compiled color pipeline per processor id:
// repeating the same job reuses the context, while an override id forces a
// second compilation.
TEST(RendererColorManagement, CachesColorContextPerProcessorId)
{
	StubRenderer renderer;

	olive::ColorProcessorPtr processor = create_identity_processor();
	ASSERT_TRUE(processor);

	olive::ColorTransformJob job;
	job.set_color_processor(processor);

	const olive::VideoParams params(64, 64, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);

	renderer.blit_color_managed(job, params);
	EXPECT_EQ(renderer.blit_count, 1);
	EXPECT_EQ(renderer.create_shader_count, 1);
	EXPECT_FALSE(renderer.last_blit_shader.isNull());

	// Same processor id: color context cache hit, no new shader compilation.
	renderer.blit_color_managed(job, params);
	EXPECT_EQ(renderer.blit_count, 2);
	EXPECT_EQ(renderer.create_shader_count, 1);

	// A distinct override id bypasses the cached context and compiles again.
	olive::ColorTransformJob other_job;
	other_job.set_color_processor(processor);
	other_job.set_override_id(QStringLiteral("other-context"));
	renderer.blit_color_managed(other_job, params);
	EXPECT_EQ(renderer.blit_count, 3);
	EXPECT_EQ(renderer.create_shader_count, 2);

	renderer.destroy();
}

// The destination overload of BlitColorManaged must forward the destination
// texture and its params to the backend blit.
TEST(RendererColorManagement, BlitToDestinationForwardsTexture)
{
	StubRenderer renderer;

	olive::ColorProcessorPtr processor = create_identity_processor();
	ASSERT_TRUE(processor);

	olive::ColorTransformJob job;
	job.set_color_processor(processor);

	const olive::VideoParams params(32, 16, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);
	olive::TexturePtr destination = renderer.create_texture(params);
	ASSERT_NE(destination, nullptr);

	renderer.blit_color_managed(job, destination.get());
	EXPECT_EQ(renderer.blit_count, 1);
	EXPECT_EQ(renderer.last_blit_destination, destination.get());
	EXPECT_EQ(renderer.last_blit_params.width(), 32);
	EXPECT_EQ(renderer.last_blit_params.height(), 16);

	destination.reset();
	renderer.destroy();
}

class RenderMiscAutoCacherTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		olive::ColorManager::set_up_default_config();

		// Use the dummy render backend so PreviewAutoCacher can be exercised
		// without initializing OpenGL/Vulkan in the unit-test process.
		olive::Config::current()[QStringLiteral("GraphicsBackend")] =
			QStringLiteral("dummy");

		olive::DiskManager::create_instance();
		olive::ConformManager::create_instance();
		olive::RenderManager::create_instance();

		project_ = std::make_unique<olive::Project>();
		project_->initialize();
	}

	void TearDown() override
	{
		project_.reset();
		olive::RenderManager::destroy_instance();
		olive::ConformManager::destroy_instance();
		olive::DiskManager::destroy_instance();
	}

	std::unique_ptr<olive::Project> project_;
};

// Requesting a new single frame must cancel the previously queued (not yet
// dispatched) single-frame ticket: the old ticket finishes without a result
// and the new one becomes the pending render.
TEST_F(RenderMiscAutoCacherTest, GetSingleFrameCancelsPreviouslyQueuedTicket)
{
	auto *viewer = new olive::ViewerOutput();
	viewer->setParent(project_.get());

	olive::PreviewAutoCacher cacher;

	olive::RenderTicketPtr first =
		cacher.get_single_frame(viewer, olive::Rational(0));
	olive::RenderTicketPtr second =
		cacher.get_single_frame(viewer, olive::Rational(1));

	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	EXPECT_NE(first, second);

	EXPECT_EQ(first->get_finish_count(), 1);
	EXPECT_FALSE(first->is_running());
	EXPECT_FALSE(first->has_result());

	EXPECT_TRUE(second->is_running());
}

// SetProject() early-outs when the same project (or null twice) is passed;
// neither call may disturb the copied graph or leak tasks.
TEST_F(RenderMiscAutoCacherTest, SetSameProjectTwiceIsNoOp)
{
	olive::PreviewAutoCacher cacher;
	cacher.set_project(project_.get());
	cacher.set_project(project_.get());
	cacher.set_project(nullptr);
	cacher.set_project(nullptr);
	EXPECT_FALSE(cacher.is_rendering_custom_range());
}

// ForceCacheRange queues the requested frames through TryRender; with the
// dummy backend each ticket finishes without a result, and the cacher must
// still emit StopCacheProxyTasks when the range iterator is exhausted.
TEST_F(RenderMiscAutoCacherTest,
	   ForceCacheRangeSchedulesVideoJobAndSignalsCompletion)
{
	auto *viewer = new olive::ViewerOutput();
	viewer->setParent(project_.get());
	viewer->set_video_params(
		olive::VideoParams(64, 64, olive::Rational(1, 25),
						   olive::PixelFormat::u8,
						   olive::VideoParams::k_rgba_channel_count));

	olive::PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	QSignalSpy stop_spy(&cacher, &olive::PreviewAutoCacher::stop_cache_proxy_tasks);

	cacher.force_cache_range(
		viewer, olive::TimeRange(olive::Rational(0), olive::Rational(1, 25)));

	EXPECT_GE(stop_spy.count(), 1);
	EXPECT_FALSE(cacher.is_rendering_custom_range());

	// Deliver the queued RenderTicketWatcher::Finished emissions so the
	// completed watchers are reaped before teardown.
	QCoreApplication::processEvents();

	cacher.set_project(nullptr);
}

// A conform-ready notification with no conform-blocked audio ranges must be a
// harmless no-op: no cache jobs may be queued and no signals emitted.
TEST_F(RenderMiscAutoCacherTest, ConformReadyWithoutPendingConformsIsNoOp)
{
	olive::PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	QSignalSpy stop_spy(&cacher, &olive::PreviewAutoCacher::stop_cache_proxy_tasks);
	QSignalSpy progress_spy(&cacher,
							&olive::PreviewAutoCacher::signal_cache_proxy_task_progress);

	emit olive::ConformManager::instance()->conform_ready();

	EXPECT_EQ(stop_spy.count(), 0);
	EXPECT_EQ(progress_spy.count(), 0);
	EXPECT_FALSE(cacher.is_rendering_custom_range());

	cacher.set_project(nullptr);
}

// Cancelling cache-proxy tasks must drop pending video jobs without touching
// the (empty) running task list.
TEST_F(RenderMiscAutoCacherTest, CacheProxyTaskCancelledClearsPendingJobs)
{
	auto *viewer = new olive::ViewerOutput();
	viewer->setParent(project_.get());
	viewer->set_video_params(
		olive::VideoParams(64, 64, olive::Rational(1, 25),
						   olive::PixelFormat::u8,
						   olive::VideoParams::k_rgba_channel_count));

	olive::PreviewAutoCacher cacher;

	// No project is set, so the forced range cannot be dispatched and sits in
	// the pending queue
	cacher.force_cache_range(
		viewer, olive::TimeRange(olive::Rational(0), olive::Rational(1)));
	EXPECT_TRUE(cacher.is_rendering_custom_range());

	EXPECT_TRUE(QMetaObject::invokeMethod(&cacher, "cache_proxy_task_cancelled",
										  Qt::DirectConnection));

	// With the pending jobs cleared, the custom range is no longer being
	// rendered
	EXPECT_FALSE(cacher.is_rendering_custom_range());

	cacher.set_project(nullptr);
}

// With an unknown/dummy graphics backend the RenderManager never creates the
// GPU-side objects. The auto-cacher pointer must be null (previously it was
// left uninitialized, so callers such as ViewerWidget dereferenced garbage
// and crashed).
TEST(RenderManagerDummyBackend, GpuCacherMemberIsNullRatherThanUninitialized)
{
	const QVariant previous =
		olive::Config::current()[QStringLiteral("GraphicsBackend")];
	olive::Config::current()[QStringLiteral("GraphicsBackend")] =
		QStringLiteral("dummy");

	olive::RenderManager::create_instance();

	EXPECT_EQ(olive::RenderManager::instance()->backend(),
			  olive::RenderManager::k_dummy);
	EXPECT_EQ(olive::RenderManager::instance()->requested_backend(),
			  olive::RenderManager::k_dummy);
	EXPECT_EQ(olive::RenderManager::instance()->get_cacher(), nullptr);

	olive::RenderManager::destroy_instance();
	olive::Config::current()[QStringLiteral("GraphicsBackend")] = previous;
}
