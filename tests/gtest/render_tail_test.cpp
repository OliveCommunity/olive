#include <gtest/gtest.h>

#include <cstring>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariant>

#include "codec/conformmanager.h"
#include "config/config.h"
#include "core.h"
#include "node/color/colormanager/colormanager.h"
#include "node/generator/solid/solid.h"
#include "node/keying/chromakey/chromakey.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/samplebuffer.h"
#include "render/audioplaybackcache.h"
#include "render/backend/dynamicrenderer.h"
#include "render/colorprocessor.h"
#include "render/diskmanager.h"
#include "render/job/colortransformjob.h"
#include "render/previewautocacher.h"
#include "render/renderer.h"
#include "render/rendermanager.h"
#include "render/texture.h"
#include "render/videoparams.h"

namespace
{

// CPU-only olive::Renderer that records the shader code it is asked to compile
// so Renderer::GetColorContext() shader generation can be verified without a
// GL/Vulkan backend.
class ShaderCaptureRenderer : public olive::Renderer {
public:
	ShaderCaptureRenderer()
		: create_shader_count(0)
		, create_texture_count(0)
		, blit_count(0)
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
		last_frag_code = code.frag_code();
		last_vert_code = code.vert_code();
		return QVariant(create_shader_count);
	}

	void destroy_native_shader(QVariant shader) override
	{
	}

	void upload_to_texture(const QVariant &handle, const olive::VideoParams &params,
						 const void *data, int linesize) override
	{
	}

	void download_from_texture(const QVariant &handle,
							 const olive::VideoParams &params, void *data,
							 int linesize) override
	{
	}

	void flush() override
	{
	}

	olive::Color get_pixel_from_texture(olive::Texture *texture,
									 const QPointF &pt) override
	{
		return olive::Color();
	}

	int create_shader_count;
	int create_texture_count;
	int blit_count;
	QString last_frag_code;
	QString last_vert_code;

protected:
	void blit(QVariant shader, olive::AcceleratedJob &job,
			  olive::Texture *destination, olive::VideoParams destination_params,
			  bool clear_destination) override
	{
		blit_count++;
	}

	QVariant create_native_texture(int width, int height, int depth,
								 olive::PixelFormat format, int channel_count,
								 const void *data, int linesize) override
	{
		create_texture_count++;
		return QVariant(create_texture_count);
	}

	void destroy_native_texture(QVariant texture) override
	{
	}

	void destroy_internal() override
	{
	}
};

olive::ColorProcessorPtr make_identity_processor()
{
	olive::ColorManager::set_up_default_config();

	ocio::MatrixTransformRcPtr transform = ocio::MatrixTransform::Create();
	transform->setDirection(ocio::TRANSFORM_DIR_FORWARD);

	return olive::ColorProcessor::create(
		olive::ColorManager::get_default_config()->getProcessor(transform));
}

bool write_file(const QString &path, qint64 size)
{
	QFile file(path);
	if (!file.open(QFile::WriteOnly)) {
		return false;
	}
	file.write(QByteArray(static_cast<int>(size), 'x'));
	file.close();
	return true;
}

bool read_bytes_at(const QString &path, qint64 offset, qint64 len, QByteArray *out)
{
	QFile f(path);
	if (!f.open(QFile::ReadOnly)) {
		return false;
	}
	if (!f.seek(offset)) {
		return false;
	}
	*out = f.read(len);
	return out->size() == len;
}

float bytes_to_float(const QByteArray &bytes)
{
	float v;
	memcpy(&v, bytes.constData(), sizeof(v));
	return v;
}

// AudioPlaybackCache always stores audio in fixed-size segments of 10 MB per
// channel (AudioPlaybackCache::kDefaultSegmentSizePerChannel).
const qint64 k_segment_size = 10 * 1024 * 1024;

} // namespace

// A DynamicRenderer constructed with an empty backend name must report no
// backend type; the name is stored verbatim (only lowercased).
TEST(DynamicRenderer, EmptyBackendNameHasNoBackendType)
{
	olive::DynamicRenderer renderer{ QString() };
	EXPECT_TRUE(renderer.backend_name().isEmpty());
	EXPECT_FALSE(renderer.is_open_gl());
	EXPECT_FALSE(renderer.is_vulkan());

	// Without a loaded backend, context and info accessors stay at defaults
	EXPECT_EQ(renderer.open_gl_context(), nullptr);

	OakRenderBackendInfo info = {};
	EXPECT_FALSE(renderer.get_backend_info(&info));
}

// BackendFromString lowercases its input before comparing, so mixed-case
// spellings of every backend must resolve correctly.
TEST(RenderManagerBackendStrings, FromStringIsCaseInsensitive)
{
	EXPECT_EQ(olive::RenderManager::backend_from_string(QStringLiteral("VULKAN")),
			  olive::RenderManager::k_vulkan);
	EXPECT_EQ(
		olive::RenderManager::backend_from_string(QStringLiteral("MultiProcess")),
		olive::RenderManager::k_multi_process);
	EXPECT_EQ(olive::RenderManager::backend_from_string(QStringLiteral("DUMMY")),
			  olive::RenderManager::k_dummy);

	// Unknown and empty strings fall through to OpenGL
	EXPECT_EQ(olive::RenderManager::backend_from_string(QString()),
			  olive::RenderManager::k_open_gl);
}

// BackendToString has a default return after the switch for out-of-range enum
// values, which must be the OpenGL string.
TEST(RenderManagerBackendStrings, ToStringFallsBackToOpenGLForUnknownEnum)
{
	EXPECT_EQ(olive::RenderManager::backend_to_string(
				  static_cast<olive::RenderManager::Backend>(42)),
			  QStringLiteral("opengl"));
}

// A ColorTransformJob with a custom OCIO function name must have that name
// embedded in the shader generated by GetColorContext (it is passed to
// GpuShaderDesc::setFunctionName).
TEST(RendererColorContext, CustomFunctionNameIsCompiledIntoShader)
{
	ShaderCaptureRenderer renderer;

	olive::ColorProcessorPtr processor = make_identity_processor();
	ASSERT_TRUE(processor);

	olive::ColorTransformJob job;
	job.set_color_processor(processor);
	job.set_function_name(QStringLiteral("MyCustomOcioFunc"));

	const olive::VideoParams params(32, 32, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);
	renderer.blit_color_managed(job, params);

	ASSERT_EQ(renderer.create_shader_count, 1);
	EXPECT_TRUE(
		renderer.last_frag_code.contains(QStringLiteral("MyCustomOcioFunc")));
	EXPECT_EQ(renderer.blit_count, 1);

	renderer.destroy();
}

// When the job names a custom shader source node, GetColorContext must ask the
// node for its shader code (with the OCIO stub) instead of using the built-in
// colormanage stub. ChromaKeyNode wraps the stub in its chroma-key shader.
TEST(RendererColorContext, CustomShaderSourceSuppliesFragmentCode)
{
	ShaderCaptureRenderer renderer;

	olive::ColorProcessorPtr processor = make_identity_processor();
	ASSERT_TRUE(processor);

	olive::ChromaKeyNode key_node;

	olive::ColorTransformJob job;
	job.set_color_processor(processor);
	job.set_needs_custom_shader(&key_node);

	const olive::VideoParams params(32, 32, olive::PixelFormat::u8,
									olive::VideoParams::k_rgba_channel_count);
	renderer.blit_color_managed(job, params);

	ASSERT_EQ(renderer.create_shader_count, 1);
	// A uniform name unique to chromakey.frag proves the node's code was used
	EXPECT_TRUE(renderer.last_frag_code.contains(QStringLiteral("color_key")));
	EXPECT_EQ(renderer.blit_count, 1);

	renderer.destroy();
}

class RenderTailAutoCacherTest : public ::testing::Test {
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

	olive::ViewerOutput *create_viewer_with_params()
	{
		auto *viewer = new olive::ViewerOutput();
		viewer->setParent(project_.get());
		viewer->set_video_params(
			olive::VideoParams(64, 64, olive::Rational(1, 25),
							   olive::PixelFormat::u8,
							   olive::VideoParams::k_rgba_channel_count));
		return viewer;
	}

	std::unique_ptr<olive::Project> project_;
};

// While renders are paused, a forced cache range must sit in the pending queue;
// unpausing dispatches it and emits StopCacheProxyTasks once the (single frame)
// range iterator is exhausted.
TEST_F(RenderTailAutoCacherTest, PausedRendersDelayForcedCacheRange)
{
	olive::ViewerOutput *viewer = create_viewer_with_params();

	olive::PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	QSignalSpy stop_spy(&cacher, &olive::PreviewAutoCacher::stop_cache_proxy_tasks);

	cacher.set_renders_paused(true);
	cacher.force_cache_range(
		viewer, olive::TimeRange(olive::Rational(0), olive::Rational(1, 25)));
	EXPECT_EQ(stop_spy.count(), 0);

	cacher.set_renders_paused(false);
	EXPECT_GE(stop_spy.count(), 1);

	// Deliver the queued RenderTicketWatcher::Finished emissions so the
	// completed watchers are reaped before teardown.
	QCoreApplication::processEvents();

	cacher.set_project(nullptr);
}

// The thumbnail pause gates only the video-job half of TryRender, so a forced
// cache range queued while thumbnails are paused must wait for the unpause.
TEST_F(RenderTailAutoCacherTest, PausedThumbnailsDelayForcedCacheRange)
{
	olive::ViewerOutput *viewer = create_viewer_with_params();

	olive::PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	QSignalSpy stop_spy(&cacher, &olive::PreviewAutoCacher::stop_cache_proxy_tasks);

	cacher.set_thumbnails_paused(true);
	cacher.force_cache_range(
		viewer, olive::TimeRange(olive::Rational(0), olive::Rational(1, 25)));
	EXPECT_EQ(stop_spy.count(), 0);

	cacher.set_thumbnails_paused(false);
	EXPECT_GE(stop_spy.count(), 1);

	QCoreApplication::processEvents();

	cacher.set_project(nullptr);
}

// With a project set, GetSingleFrame resolves the node through the ProjectCopier
// and dispatches a real render ticket. With the dummy backend the underlying
// ticket finishes without a result, and the passthrough ticket must be finished
// once the watcher signals completion.
TEST_F(RenderTailAutoCacherTest, GetSingleFrameDispatchesThroughProjectCopy)
{
	olive::ViewerOutput *viewer = create_viewer_with_params();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(project_.get());

	olive::PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	olive::RenderTicketPtr ticket =
		cacher.get_single_frame(solid, viewer, olive::Rational(0));
	ASSERT_NE(ticket, nullptr);
	EXPECT_TRUE(ticket->is_running());

	// The dummy backend has no render threads, so the dispatched ticket can
	// only be finished through the clear path (covered in detail by the
	// ClearSingleFrameRenders tests below).
	cacher.clear_single_frame_renders();

	EXPECT_EQ(ticket->get_finish_count(), 1);
	EXPECT_FALSE(ticket->is_running());
	EXPECT_FALSE(ticket->has_result());

	cacher.set_project(nullptr);
}

// ClearSingleFrameRenders must cancel every dispatched (but no longer running)
// single-frame passthrough: the ticket is finished without a result and the
// watcher is reaped synchronously through VideoRendered.
TEST_F(RenderTailAutoCacherTest, ClearSingleFrameRendersFinishesDispatchedTicket)
{
	olive::ViewerOutput *viewer = create_viewer_with_params();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(project_.get());

	olive::PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	olive::RenderTicketPtr ticket =
		cacher.get_single_frame(solid, viewer, olive::Rational(0));
	ASSERT_NE(ticket, nullptr);
	ASSERT_TRUE(ticket->is_running());

	cacher.clear_single_frame_renders();

	EXPECT_EQ(ticket->get_finish_count(), 1);
	EXPECT_FALSE(ticket->is_running());
	EXPECT_FALSE(ticket->has_result());

	// Flush the stale queued watcher notification (its receiver is gone now).
	QCoreApplication::processEvents();

	cacher.set_project(nullptr);
}

// ClearSingleFrameRendersThatArentRunning follows the same path for the dummy
// backend, whose tickets are never running by the time they can be cleared.
TEST_F(RenderTailAutoCacherTest,
	   ClearSingleFrameRendersThatArentRunningFinishesDispatchedTicket)
{
	olive::ViewerOutput *viewer = create_viewer_with_params();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(project_.get());

	olive::PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	olive::RenderTicketPtr ticket =
		cacher.get_single_frame(solid, viewer, olive::Rational(0));
	ASSERT_NE(ticket, nullptr);
	ASSERT_TRUE(ticket->is_running());

	cacher.clear_single_frame_renders_that_arent_running();

	EXPECT_EQ(ticket->get_finish_count(), 1);
	EXPECT_FALSE(ticket->is_running());
	EXPECT_FALSE(ticket->has_result());

	QCoreApplication::processEvents();

	cacher.set_project(nullptr);
}

class RenderTailDiskCacheTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		if (!temp_dir_.isValid()) {
			GTEST_FAIL() << "Failed to create temporary directory";
		}

		if (!olive::Core::instance()) {
			// Leaked intentionally: matches render_diskcache_test, Core is
			// process-wide and eviction paths call Core::WarnCacheFull().
			new olive::Core(olive::Core::CoreParams());
		}

		olive::DiskManager::create_instance();
	}

	void TearDown() override
	{
		olive::DiskManager::destroy_instance();
	}

	QString make_sub_dir(const QString &name) const
	{
		QDir root(temp_dir_.path());
		if (!root.mkpath(name)) {
			return QString();
		}
		return root.filePath(name);
	}

	QTemporaryDir temp_dir_;
};

// Moving a folder to a new path must broadcast DeletedFrame for every tracked
// file (without deleting anything on disk) and reset the folder state to
// defaults before loading the new path's index.
TEST_F(RenderTailDiskCacheTest, SetPathEmitsDeletedFramesAndResetsState)
{
	const QString sub1 = make_sub_dir(QStringLiteral("move_from"));
	const QString sub2 = make_sub_dir(QStringLiteral("move_to"));
	ASSERT_FALSE(sub1.isEmpty());
	ASSERT_FALSE(sub2.isEmpty());

	olive::DiskCacheFolder folder(sub1);
	folder.set_limit(12345);

	const QString fn = QDir(sub1).filePath(QStringLiteral("frame"));
	ASSERT_TRUE(write_file(fn, 64));
	folder.created_file(fn);

	QSignalSpy spy(&folder, &olive::DiskCacheFolder::deleted_frame);

	folder.set_path(sub2);

	EXPECT_EQ(folder.get_path(), sub2);
	EXPECT_EQ(folder.get_limit(), 21474836480LL); // back to the 20 GB default
	EXPECT_FALSE(folder.get_clear_on_close());

	ASSERT_EQ(spy.count(), 1);
	const QList<QVariant> args = spy.takeFirst();
	EXPECT_EQ(args.at(0).toString(), sub1);
	EXPECT_EQ(args.at(1).toString(), fn);

	// The file itself is untouched, but it is no longer tracked
	EXPECT_TRUE(QFileInfo::exists(fn));
	EXPECT_FALSE(folder.delete_specific_file(fn));
}

// When the persisted index references files that have since been deleted
// externally, those entries must be skipped on load while surviving files are
// still picked up.
TEST_F(RenderTailDiskCacheTest, PersistedIndexSkipsFilesThatNoLongerExist)
{
	const QString sub = make_sub_dir(QStringLiteral("index_skip"));
	ASSERT_FALSE(sub.isEmpty());

	const QString keep = QDir(sub).filePath(QStringLiteral("keep"));
	const QString gone = QDir(sub).filePath(QStringLiteral("gone"));
	ASSERT_TRUE(write_file(keep, 32));
	ASSERT_TRUE(write_file(gone, 32));

	{
		olive::DiskCacheFolder folder(sub);
		folder.created_file(keep);
		folder.created_file(gone);
		// Destruction writes the index file into the cache folder
	}

	ASSERT_TRUE(QFile::remove(gone));

	{
		olive::DiskCacheFolder reopened(sub);

		// The missing file was not re-registered, the surviving one was
		EXPECT_TRUE(reopened.delete_specific_file(keep));
		EXPECT_FALSE(reopened.delete_specific_file(gone));
		EXPECT_FALSE(QFileInfo::exists(keep));
	}
}

// The clear-on-close flag is serialized into the index along with the limit,
// so a folder reopened after closing with the flag set must restore it.
TEST_F(RenderTailDiskCacheTest, ClearOnCloseFlagPersistsAcrossInstances)
{
	const QString sub = make_sub_dir(QStringLiteral("persist_clear_flag"));
	ASSERT_FALSE(sub.isEmpty());

	const QString fn = QDir(sub).filePath(QStringLiteral("frame"));
	ASSERT_TRUE(write_file(fn, 32));

	{
		olive::DiskCacheFolder folder(sub);
		folder.set_clear_on_close(true);
		folder.created_file(fn);
		// Destruction clears the cache and saves the flag into the index
	}

	ASSERT_FALSE(QFileInfo::exists(fn));

	{
		olive::DiskCacheFolder reopened(sub);
		EXPECT_TRUE(reopened.get_clear_on_close());

		// The cleared entry must not come back through the index either
		EXPECT_FALSE(reopened.delete_specific_file(fn));
	}
}

// Registering a file that does not exist on disk tracks it with size zero;
// deleting it again succeeds because a missing file counts as deleted.
TEST_F(RenderTailDiskCacheTest, CreatedFileForMissingFileIsTrackedAsZeroSize)
{
	const QString sub = make_sub_dir(QStringLiteral("zero_size"));
	ASSERT_FALSE(sub.isEmpty());

	olive::DiskCacheFolder folder(sub);

	const QString ghost = QDir(sub).filePath(QStringLiteral("ghost"));
	ASSERT_FALSE(QFileInfo::exists(ghost));
	folder.created_file(ghost);

	QSignalSpy spy(&folder, &olive::DiskCacheFolder::deleted_frame);

	EXPECT_TRUE(folder.delete_specific_file(ghost));

	ASSERT_EQ(spy.count(), 1);
	EXPECT_EQ(spy.first().at(0).toString(), sub);
	EXPECT_EQ(spy.first().at(1).toString(), ghost);
}

// DiskManager::Accessed/CreatedFile forward to the matching folder and
// DeleteSpecificFile broadcasts to every open folder, re-emitting the folder's
// DeletedFrame signal as its own.
TEST_F(RenderTailDiskCacheTest,
	   DiskManagerAccessedAndDeleteSpecificFileForwardToFolder)
{
	olive::DiskManager *dm = olive::DiskManager::instance();
	ASSERT_NE(dm, nullptr);

	const QString sub = make_sub_dir(QStringLiteral("forwarding"));
	ASSERT_FALSE(sub.isEmpty());

	const QString fn = QDir(sub).filePath(QStringLiteral("frame"));
	ASSERT_TRUE(write_file(fn, 32));

	dm->created_file(sub, fn);
	dm->accessed(sub, fn);
	ASSERT_TRUE(QFileInfo::exists(fn));

	QSignalSpy spy(dm, &olive::DiskManager::deleted_frame);

	dm->delete_specific_file(fn);

	EXPECT_FALSE(QFileInfo::exists(fn));
	ASSERT_EQ(spy.count(), 1);
	EXPECT_EQ(spy.first().at(0).toString(), sub);
	EXPECT_EQ(spy.first().at(1).toString(), fn);
}

// The static path helpers must return distinct, non-empty locations; the config
// file name is part of the on-disk format.
TEST_F(RenderTailDiskCacheTest, DefaultDiskCachePathsAreNonEmptyAndDistinct)
{
	const QString config_file =
		olive::DiskManager::get_default_disk_cache_config_file();
	const QString cache_path = olive::DiskManager::get_default_disk_cache_path();

	EXPECT_FALSE(config_file.isEmpty());
	EXPECT_FALSE(cache_path.isEmpty());
	EXPECT_NE(config_file, cache_path);
	EXPECT_TRUE(config_file.endsWith(QStringLiteral("defaultdiskcache")));
}

class RenderTailAudioCacheTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		if (!temp_dir_.isValid()) {
			GTEST_FAIL() << "Failed to create temporary directory";
		}

		if (!olive::Core::instance()) {
			new olive::Core(olive::Core::CoreParams()); // intentionally leaked
		}

		olive::DiskManager::create_instance();

		// Point the project cache at a folder alongside the (unsaved) project
		// file so every cache write stays inside the temporary directory.
		olive::ColorManager::set_up_default_config();
		project_ = std::make_unique<olive::Project>();
		project_->initialize();
		project_->set_filename(
			QDir(temp_dir_.path()).filePath(QStringLiteral("test.ove")));
		project_->set_cache_location_setting(
			olive::Project::k_cache_store_alongside_project);
	}

	void TearDown() override
	{
		project_.reset();
		olive::DiskManager::destroy_instance();
	}

	static olive::core::AudioParams make_params()
	{
		return olive::core::AudioParams(48000,
										olive::core::k_channel_layout_stereo,
										olive::core::SampleFormat::f32_p);
	}

	static void fill_buffer(olive::core::SampleBuffer *buf, float ch0, float ch1)
	{
		for (size_t i = 0; i < buf->sample_count(); i++) {
			buf->data(0)[i] = ch0;
			buf->data(1)[i] = ch1;
		}
	}

	QTemporaryDir temp_dir_;
	std::unique_ptr<olive::Project> project_;
};

// SetParameters stores the audio params; setting the same value twice early-outs
// and leaves them untouched.
TEST_F(RenderTailAudioCacheTest, SetParametersRoundTrip)
{
	olive::AudioPlaybackCache cache(project_.get());
	EXPECT_EQ(cache.get_parameters().channel_count(), 0);

	const olive::core::AudioParams params = make_params();
	cache.set_parameters(params);
	EXPECT_EQ(cache.get_parameters(), params);

	cache.set_parameters(params);
	EXPECT_EQ(cache.get_parameters(), params);

	const olive::core::AudioParams other(44100,
										 olive::core::k_channel_layout_mono,
										 olive::core::SampleFormat::f32_p);
	cache.set_parameters(other);
	EXPECT_EQ(cache.get_parameters().sample_rate(), 44100);
	EXPECT_EQ(cache.get_parameters().channel_count(), 1);
}

// WritePCM writes one segment file per channel, zero-padded to the full segment
// size, and validates exactly the written range.
TEST_F(RenderTailAudioCacheTest, WritePcmWritesSegmentFilesAndValidatesRange)
{
	olive::AudioPlaybackCache cache(project_.get());
	cache.set_parameters(make_params());

	const olive::TimeRange range(olive::Rational(0), olive::Rational(1, 10));

	olive::core::SampleBuffer buf(make_params(), olive::Rational(1, 10));
	ASSERT_TRUE(buf.is_allocated());
	fill_buffer(&buf, 0.5f, 0.25f);

	cache.write_pcm(range, { range }, buf);

	EXPECT_TRUE(cache.has_validated_ranges());
	EXPECT_FALSE(cache.has_invalidated_ranges(range));

	// 4800 samples of 4-byte floats per channel
	const qint64 data_bytes = 19200;

	const QDir seg_dir = cache.get_this_cache_directory();
	const QString ch0 = seg_dir.filePath(QStringLiteral("0.0"));
	const QString ch1 = seg_dir.filePath(QStringLiteral("0.1"));
	ASSERT_TRUE(QFileInfo::exists(ch0));
	ASSERT_TRUE(QFileInfo::exists(ch1));

	// The buffer covered the whole range, so no padding is needed and the
	// segment contains exactly the written data
	EXPECT_EQ(QFileInfo(ch0).size(), data_bytes);
	EXPECT_EQ(QFileInfo(ch1).size(), data_bytes);

	QByteArray bytes;
	ASSERT_TRUE(read_bytes_at(ch0, 0, 4, &bytes));
	EXPECT_FLOAT_EQ(bytes_to_float(bytes), 0.5f);

	ASSERT_TRUE(read_bytes_at(ch1, 0, 4, &bytes));
	EXPECT_FLOAT_EQ(bytes_to_float(bytes), 0.25f);
}

// A write that does not start at zero must seek into the segment, leaving the
// preceding bytes as silence.
TEST_F(RenderTailAudioCacheTest, WritePcmAtNonZeroStartWritesAtByteOffset)
{
	olive::AudioPlaybackCache cache(project_.get());
	cache.set_parameters(make_params());

	const olive::TimeRange range(olive::Rational(1, 10), olive::Rational(1, 5));

	olive::core::SampleBuffer buf(make_params(), olive::Rational(1, 10));
	ASSERT_TRUE(buf.is_allocated());
	fill_buffer(&buf, 0.75f, 0.75f);

	cache.write_pcm(range, { range }, buf);

	EXPECT_FALSE(cache.has_invalidated_ranges(range));

	const qint64 data_bytes = 19200;
	const QString ch0 =
		cache.get_this_cache_directory().filePath(QStringLiteral("0.0"));
	ASSERT_TRUE(QFileInfo::exists(ch0));
	// The file extends exactly to the end of the written range
	EXPECT_EQ(QFileInfo(ch0).size(), 2 * data_bytes);

	// The first range was never written, so it reads back as silence
	QByteArray bytes;
	ASSERT_TRUE(read_bytes_at(ch0, 0, 4, &bytes));
	EXPECT_EQ(bytes, QByteArray(4, '\0'));

	// The new data starts exactly at its byte offset
	ASSERT_TRUE(read_bytes_at(ch0, data_bytes, 4, &bytes));
	EXPECT_FLOAT_EQ(bytes_to_float(bytes), 0.75f);
}

// Only the listed valid ranges are validated, even when the sample buffer
// covers the whole render range.
TEST_F(RenderTailAudioCacheTest, WritePcmWithPartialValidRangesValidatesOnlyThose)
{
	olive::AudioPlaybackCache cache(project_.get());
	cache.set_parameters(make_params());

	const olive::TimeRange range(olive::Rational(0), olive::Rational(1, 5));
	const olive::TimeRange first_half(olive::Rational(0), olive::Rational(1, 10));

	olive::core::SampleBuffer buf(make_params(), olive::Rational(1, 5));
	ASSERT_TRUE(buf.is_allocated());
	fill_buffer(&buf, 0.5f, 0.5f);

	cache.write_pcm(range, { first_half }, buf);

	EXPECT_FALSE(cache.has_invalidated_ranges(first_half));
	EXPECT_TRUE(cache.has_invalidated_ranges(range));
}

// An empty valid-range list writes no segments and validates nothing.
TEST_F(RenderTailAudioCacheTest, WritePcmWithNoValidRangesWritesNothing)
{
	olive::AudioPlaybackCache cache(project_.get());
	cache.set_parameters(make_params());

	const olive::TimeRange range(olive::Rational(0), olive::Rational(1, 10));

	olive::core::SampleBuffer buf(make_params(), olive::Rational(1, 10));
	ASSERT_TRUE(buf.is_allocated());

	cache.write_pcm(range, olive::TimeRangeList(), buf);

	EXPECT_FALSE(cache.has_validated_ranges());
	EXPECT_FALSE(QFileInfo::exists(
		cache.get_this_cache_directory().filePath(QStringLiteral("0.0"))));
}

// A write larger than one segment must spill into the next segment file, with
// each touched segment zero-padded to its full extent.
TEST_F(RenderTailAudioCacheTest,
	   WritePcmSpanningSegmentBoundaryCreatesBothSegments)
{
	olive::AudioPlaybackCache cache(project_.get());
	cache.set_parameters(make_params());

	// 56 seconds at 48000 Hz is 10752000 bytes per channel, just over one
	// 10 MB segment.
	const olive::TimeRange range(olive::Rational(0), olive::Rational(56));

	olive::core::SampleBuffer buf(make_params(), olive::Rational(56));
	ASSERT_TRUE(buf.is_allocated());
	ASSERT_EQ(buf.sample_count(), size_t(56 * 48000));
	fill_buffer(&buf, 1.0f, 1.0f);

	cache.write_pcm(range, { range }, buf);

	EXPECT_FALSE(cache.has_invalidated_ranges(range));

	const QDir seg_dir = cache.get_this_cache_directory();
	const QString seg0 = seg_dir.filePath(QStringLiteral("0.0"));
	const QString seg1 = seg_dir.filePath(QStringLiteral("1.0"));
	ASSERT_TRUE(QFileInfo::exists(seg0));
	ASSERT_TRUE(QFileInfo::exists(seg1));

	EXPECT_EQ(QFileInfo(seg0).size(), k_segment_size);
	// The second segment holds exactly the spillover bytes
	EXPECT_EQ(QFileInfo(seg1).size(),
			  56 * 48000 * 4 - k_segment_size);

	// The spillover data starts at the beginning of the second segment file
	QByteArray bytes;
	ASSERT_TRUE(read_bytes_at(seg1, 0, 4, &bytes));
	EXPECT_FLOAT_EQ(bytes_to_float(bytes), 1.0f);
}
