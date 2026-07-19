/*
 * Oak Video Editor - Direct-Connection Preview Black Screen Regression Test
 * Copyright (C) 2026 Oak Team
 *
 * Reproduces the pre-existing bug where the preview is black when a footage
 * node is connected directly to a viewer output, while inserting any node in
 * between renders correctly. Drives the exact application render path:
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
#include "node/color/colormanager/colormanager.h"
#include "node/distort/transform/transformdistortnode.h"
#include "node/effect/opacity/opacityeffect.h"
#include "node/factory.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/serializer/serializer.h"
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
	// cmake-build-debug/app.
	QDir dir(QCoreApplication::applicationDirPath());
	dir.cdUp(); // tests/gtest -> tests
	dir.cdUp(); // tests -> build dir
	dir.cd(QStringLiteral("app"));
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

class RenderDirectConnectionTest
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

	// Renders one frame through the application's preview path and returns the
	// resulting CPU frame (nullptr on failure/timeout).
	FramePtr render_one_frame(ViewerOutput *viewer)
	{
		RenderTicketPtr ticket =
			RenderManager::instance()->get_cacher()->get_single_frame(
				viewer, Rational(0), false);
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
};

// Footage previewed directly (footage node connected straight to the viewer
// output, no intermediate node) must not produce a black frame.
TEST_P(RenderDirectConnectionTest, DirectConnectionIsNotBlack)
{
	ViewerOutput *viewer = new ViewerOutput();
	viewer->setParent(project_.get());

	// Direct connection: footage -> viewer
	Node::connect_edge(footage_, NodeInput(viewer, ViewerOutput::k_texture_input));

	FramePtr frame = render_one_frame(viewer);
	ASSERT_TRUE(frame != nullptr)
		<< "Direct connection render produced no frame (timeout or empty "
		   "ticket)";
	ASSERT_TRUE(frame->is_allocated());

	int nonzero = count_non_zero_bytes(frame);
	EXPECT_GT(nonzero, 0)
		<< "Direct connection render is BLACK (all sampled bytes are zero)";

	Node::disconnect_edge(footage_,
						 NodeInput(viewer, ViewerOutput::k_texture_input));
}

// Same as above but with an effect node between footage and viewer. This is
// the control case that reportedly works.
TEST_P(RenderDirectConnectionTest, IndirectConnectionIsNotBlack)
{
	ViewerOutput *viewer = new ViewerOutput();
	viewer->setParent(project_.get());

	OpacityEffect *opacity = new OpacityEffect();
	opacity->setParent(project_.get());

	// Indirect connection: footage -> opacity -> viewer
	Node::connect_edge(footage_,
					  NodeInput(opacity, OpacityEffect::k_texture_input));
	Node::connect_edge(opacity, NodeInput(viewer, ViewerOutput::k_texture_input));

	FramePtr frame = render_one_frame(viewer);
	ASSERT_TRUE(frame != nullptr) << "Indirect connection render produced no "
									 "frame (timeout or empty ticket)";
	ASSERT_TRUE(frame->is_allocated());

	int nonzero = count_non_zero_bytes(frame);
	EXPECT_GT(nonzero, 0)
		<< "Indirect connection render is BLACK (all sampled bytes are zero)";
}

INSTANTIATE_TEST_SUITE_P(Backends, RenderDirectConnectionTest,
						 ::testing::Values(QStringLiteral("opengl"),
										   QStringLiteral("vulkan")));

// Resolution-mismatch scenarios: footage is 1920x1080 while the viewer is
// 1280x720, forcing a rescale when the frame is downloaded in the worker.
class RenderResolutionMismatchTest : public RenderDirectConnectionTest {
protected:
	ViewerOutput *create_small_viewer()
	{
		ViewerOutput *viewer = new ViewerOutput();
		viewer->setParent(project_.get());
		viewer->set_video_params(VideoParams(
			1280, 720, Rational(24),
			static_cast<PixelFormat::Format>(
				Config::current()[QStringLiteral("OfflinePixelFormat")]
					.toInt()),
			VideoParams::k_internal_channel_count, Rational(1),
			VideoParams::k_interlace_none, 1));
		return viewer;
	}

	void expect_frame_not_black(FramePtr frame, const char *what)
	{
		ASSERT_TRUE(frame != nullptr)
			<< what << " render produced no frame (timeout or empty ticket)";
		ASSERT_TRUE(frame->is_allocated());
		EXPECT_GT(count_non_zero_bytes(frame), 0)
			<< what << " render is BLACK (all sampled bytes are zero)";
	}
};

// Direct connection with mismatched resolutions: the worker must rescale the
// footage-sized texture into the viewer-sized output frame.
TEST_P(RenderResolutionMismatchTest, DirectConnectionNotBlack)
{
	ViewerOutput *viewer = create_small_viewer();
	Node::connect_edge(footage_, NodeInput(viewer, ViewerOutput::k_texture_input));

	expect_frame_not_black(render_one_frame(viewer),
						"Direct connection (resolution mismatch)");

	Node::disconnect_edge(footage_,
						 NodeInput(viewer, ViewerOutput::k_texture_input));
}

// Opacity passes the footage-sized texture through, so the worker still has
// to rescale at download time. Control case for the direct test above.
TEST_P(RenderResolutionMismatchTest, IndirectOpacityNotBlack)
{
	ViewerOutput *viewer = create_small_viewer();

	OpacityEffect *opacity = new OpacityEffect();
	opacity->setParent(project_.get());

	Node::connect_edge(footage_,
					  NodeInput(opacity, OpacityEffect::k_texture_input));
	Node::connect_edge(opacity, NodeInput(viewer, ViewerOutput::k_texture_input));

	expect_frame_not_black(render_one_frame(viewer),
						"Indirect opacity (resolution mismatch)");
}

// Transform with auto-scale renders at the sequence resolution, so no size
// rescale is needed at download time. This mirrors the timeline chain.
TEST_P(RenderResolutionMismatchTest, IndirectTransformNotBlack)
{
	ViewerOutput *viewer = create_small_viewer();

	TransformDistortNode *transform = new TransformDistortNode();
	transform->setParent(project_.get());
	// 1 = Fit
	transform->set_standard_value(TransformDistortNode::k_autoscale_input, 1);

	Node::connect_edge(footage_,
					  NodeInput(transform, TransformDistortNode::k_texture_input));
	Node::connect_edge(transform,
					  NodeInput(viewer, ViewerOutput::k_texture_input));

	expect_frame_not_black(render_one_frame(viewer),
						"Indirect transform (resolution mismatch)");
}

INSTANTIATE_TEST_SUITE_P(Backends, RenderResolutionMismatchTest,
						 ::testing::Values(QStringLiteral("opengl"),
										   QStringLiteral("vulkan")));
