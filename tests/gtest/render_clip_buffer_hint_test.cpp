/*
 * Oak Video Editor - Clip Buffer Value Hint Regression Test
 * Copyright (C) 2026 Oak Team
 *
 * Regression test for a pre-existing black-screen bug: when a footage node
 * was connected directly to a clip's buffer input (no effect node in
 * between), the preview rendered black. The buffer input is declared as
 * NodeValue::kNone, so the traverser had no type information when pulling a
 * value from the connected node's table and fell back to the last value in
 * the table. A footage pushes its video texture first and its audio samples
 * last, so a video clip ended up fed with audio samples and produced no
 * texture at all. The fix makes ClipBlock::GetValueHintForInput prefer the
 * value type matching the clip's track.
 *
 * These tests drive the exact application render path:
 * PreviewAutoCacher -> RenderManager -> RenderWorkerPool -> oak-render-worker.
 */

#include <gtest/gtest.h>

#include <atomic>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#include "codec/conformmanager.h"
#include "codec/frame.h"
#include "codec/proxymanager.h"
#include "config/config.h"
#include "node/block/clip/clip.h"
#include "node/color/colormanager/colormanager.h"
#include "node/effect/opacity/opacityeffect.h"
#include "node/factory.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializer.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/sampleformat.h"
#include "render/diskmanager.h"
#include "render/framemanager.h"
#include "render/previewautocacher.h"
#include "render/rendermanager.h"
#include "render/renderticket.h"
#include "task/taskmanager.h"

#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
#include "render/backend/dynamicrenderer.h"
#include "render/backend/renderbackend_c.h"
#endif

using namespace olive;

namespace
{

QString demo_video_path()
{
	return QDir(QStringLiteral(OAK_TEST_SOURCE_DIR))
		.filePath(QStringLiteral("tests/demo.mp4"));
}

QString worker_binary_path()
{
	// The test binary lives in cmake-build-debug/tests/gtest; the worker is in
	// cmake-build-debug/worker.
	QDir dir(QCoreApplication::applicationDirPath());
	dir.cdUp(); // tests/gtest -> tests
	dir.cdUp(); // tests -> build dir
	dir.cd(QStringLiteral("worker"));
#if defined(_WIN32)
	return dir.filePath(QStringLiteral("oak-render-worker.exe"));
#else
	return dir.filePath(QStringLiteral("oak-render-worker"));
#endif
}

bool is_render_backend_available(const QString &backend)
{
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	olive::DynamicRenderer renderer(backend);
	if (!renderer.load()) {
		return false;
	}

	OakRenderBackendInfo info = {};
	if (!renderer.get_backend_info(&info)) {
		return false;
	}

	if (backend == QStringLiteral("vulkan") &&
		info.kind != oak_render_backend_vulkan) {
		return false;
	}

	if (backend == QStringLiteral("opengl") &&
		info.kind != oak_render_backend_opengl) {
		return false;
	}

	return renderer.init();
#else
	Q_UNUSED(backend)
	return false;
#endif
}

// Returns the number of non-zero bytes sampled from the frame buffer, or -1
// when the frame is invalid.
int count_non_zero_bytes(const FramePtr &frame)
{
	if (!frame || !frame->is_allocated()) {
		return -1;
	}
	const auto *data = reinterpret_cast<const uint8_t *>(frame->const_data());
	const int size = frame->allocated_size();
	int nonzero = 0;
	// Sample across the buffer to keep the check fast.
	const int step = qMax(1, size / 4096);
	for (int i = 0; i < size; i += step) {
		if (data[i] != 0) {
			nonzero++;
		}
	}
	return nonzero;
}

} // namespace

class RenderClipBufferHintTest
	: public ::testing::Test,
	  public ::testing::WithParamInterface<QString> {
protected:
	static void SetUpTestSuite()
	{
		// Mirror Core::Start()'s singleton initialization order (RenderManager
		// is created per-test instead, so that each backend gets a fresh one).
		NodeFactory::initialize();
		ColorManager::set_up_default_config();
		TaskManager::create_instance();
		ConformManager::create_instance();
		ProxyManager::create_instance();
		FrameManager::create_instance();
		ProjectSerializer::initialize();
		DiskManager::create_instance();

		// Point the worker pool at the built worker binary.
		const QString worker = worker_binary_path();
		if (QFileInfo::exists(worker)) {
			qputenv("OAK_RENDER_WORKER", QFile::encodeName(worker));
		}
	}

	static void TearDownTestSuite()
	{
		DiskManager::destroy_instance();
		ProjectSerializer::destroy();
		FrameManager::destroy_instance();
		ProxyManager::destroy_instance();
		ConformManager::destroy_instance();
		TaskManager::destroy_instance();
		NodeFactory::destroy();
	}

	void SetUp() override
	{
		backend_ = GetParam();
		if (!is_render_backend_available(backend_)) {
			GTEST_SKIP() << "Render backend is not available: "
						 << backend_.toStdString();
		}

		const QString worker = worker_binary_path();
		if (!QFileInfo::exists(worker)) {
			GTEST_SKIP() << "worker binary not found at "
						 << worker.toStdString();
		}

		demo_path_ = demo_video_path();
		ASSERT_TRUE(QFileInfo::exists(demo_path_));

		Config::current()[QStringLiteral("GraphicsBackend")] = backend_;

		project_ = std::make_unique<Project>();
		project_->initialize();

		footage_ = new Footage(demo_path_);
		footage_->setParent(project_.get());
		ASSERT_TRUE(footage_->is_valid())
			<< "Footage failed to probe " << demo_path_.toStdString();
		// The bug requires the footage to provide both a video and an audio
		// stream, so that the video texture is not the last value in the
		// footage's table.
		ASSERT_GE(footage_->get_video_stream_count(), 1);
		ASSERT_GE(footage_->get_audio_stream_count(), 1)
			<< "Test footage must contain an audio stream";

		RenderManager::create_instance();
		RenderManager::instance()->get_cacher()->set_project(project_.get());
	}

	void TearDown() override
	{
		// May be null when SetUp() skipped before creating the instance.
		if (RenderManager::instance()) {
			RenderManager::instance()->get_cacher()->set_project(nullptr);
			RenderManager::destroy_instance();
		}
		project_.reset();
	}

	// Builds sequence <- track <- clip <- (optional effect) <- footage and
	// returns the clip. When insert_effect is false the footage is connected
	// directly to the clip's buffer input, which is the black-screen case.
	ClipBlock *build_video_clip_chain(bool insert_effect)
	{
		sequence_ = new Sequence();
		sequence_->setParent(project_.get());
		sequence_->set_video_params(VideoParams(
			1920, 1080, Rational(25),
			static_cast<PixelFormat::Format>(
				Config::current()[QStringLiteral("OfflinePixelFormat")]
					.toInt()),
			VideoParams::k_internal_channel_count, Rational(1),
			VideoParams::k_interlace_none, 1));
		sequence_->set_audio_params(olive::core::AudioParams(
			48000, olive::core::k_channel_layout_stereo,
			olive::core::SampleFormat::f32_p));

		Track *track = new Track();
		track->setParent(project_.get());
		video_track_ = track;
		ClipBlock *clip = new ClipBlock();
		clip->setParent(project_.get());
		clip->set_length_and_media_out(footage_->get_length());

		Node *buffer_source = footage_;
		if (insert_effect) {
			OpacityEffect *opacity = new OpacityEffect();
			opacity->setParent(project_.get());
			Node::connect_edge(footage_,
							  NodeInput(opacity, OpacityEffect::k_texture_input));
			buffer_source = opacity;
		}
		Node::connect_edge(buffer_source,
						  NodeInput(clip, ClipBlock::k_buffer_in));

		track->append_block(clip);

		// Wire the track into the sequence's video track list (this is what
		// assigns the track its type) and into the sequence's texture output.
		TrackList *track_list = sequence_->track_list(Track::k_video);
		track_list->array_append();
		Node::connect_edge(
			track, track_list->track_input(track_list->array_size() - 1));
		Node::connect_edge(track,
						  NodeInput(sequence_, ViewerOutput::k_texture_input));

		return clip;
	}

	// Renders one frame through the application's preview path and returns the
	// resulting CPU frame (nullptr on failure/timeout).
	FramePtr render_one_frame(ViewerOutput *viewer, const Rational &time)
	{
		RenderTicketPtr ticket =
			RenderManager::instance()->get_cacher()->get_single_frame(viewer, time,
																   false);
		if (!ticket) {
			return nullptr;
		}

		std::atomic<bool> finished{ false };
		QObject::connect(ticket.get(), &RenderTicket::finished,
						 [&finished]() { finished = true; });

		QElapsedTimer timer;
		timer.start();
		while (!finished.load() && !timer.hasExpired(60000)) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			QThread::msleep(5);
		}

		if (!finished.load() || !ticket->has_result()) {
			return nullptr;
		}

		return ticket->get().value<FramePtr>();
	}

	QString backend_;
	QString demo_path_;
	std::unique_ptr<Project> project_;
	Footage *footage_ = nullptr;
	Sequence *sequence_ = nullptr;
	Track *video_track_ = nullptr;
};

// Footage connected directly to a video clip's buffer input must not produce
// a black frame. Before the fix, the clip pulled the footage's audio samples
// (the last value in the footage's table) instead of its video texture.
TEST_P(RenderClipBufferHintTest, DirectFootageToVideoClipNotBlack)
{
	build_video_clip_chain(false);

	for (double t : { 0.0, 1.0 }) {
		FramePtr frame = render_one_frame(sequence_, Rational::from_double(t));
		ASSERT_TRUE(frame != nullptr)
			<< "Direct footage->clip render produced no frame at t=" << t;
		ASSERT_TRUE(frame->is_allocated());
		EXPECT_GT(count_non_zero_bytes(frame), 0)
			<< "Direct footage->clip render is BLACK at t=" << t
			<< " (all sampled bytes are zero)";
	}
}

// Control case: an effect node between the footage and the clip always worked,
// because the effect's table contains only the passed-through texture.
TEST_P(RenderClipBufferHintTest, IndirectFootageToVideoClipNotBlack)
{
	build_video_clip_chain(true);

	for (double t : { 0.0, 1.0 }) {
		FramePtr frame = render_one_frame(sequence_, Rational::from_double(t));
		ASSERT_TRUE(frame != nullptr)
			<< "Indirect footage->opacity->clip render produced no frame at t="
			<< t;
		ASSERT_TRUE(frame->is_allocated());
		EXPECT_GT(count_non_zero_bytes(frame), 0)
			<< "Indirect footage->opacity->clip render is BLACK at t=" << t
			<< " (all sampled bytes are zero)";
	}
}

// Unit-level guard: the buffer input's value hint must follow the clip's
// track type so the traverser pulls the right value from a multi-stream
// source.
TEST_P(RenderClipBufferHintTest, BufferHintFollowsTrackType)
{
	ClipBlock *clip = build_video_clip_chain(false);

	Node::ValueHint video_hint =
		clip->get_value_hint_for_input(ClipBlock::k_buffer_in);
	ASSERT_FALSE(video_hint.types().isEmpty());
	EXPECT_TRUE(video_hint.types().contains(NodeValue::k_texture));

	// Move the clip onto an audio track: the hint must prefer samples.
	video_track_->ripple_remove_block(clip);

	Track *audio_track = new Track();
	audio_track->setParent(project_.get());
	audio_track->set_type(Track::k_audio);
	audio_track->append_block(clip);

	Node::ValueHint audio_hint =
		clip->get_value_hint_for_input(ClipBlock::k_buffer_in);
	ASSERT_FALSE(audio_hint.types().isEmpty());
	EXPECT_TRUE(audio_hint.types().contains(NodeValue::k_samples));
}

INSTANTIATE_TEST_SUITE_P(Backends, RenderClipBufferHintTest,
						 ::testing::Values(QStringLiteral("opengl"),
										   QStringLiteral("vulkan")));
