#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>

#include "codec/conformmanager.h"
#include "node/generator/solid/solid.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "render/diskmanager.h"
#include "render/previewautocacher.h"
#include "render/rendermanager.h"
#include "render/videoparams.h"

using namespace olive;

class PreviewAutoCacherTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();

		// Use the dummy render backend so PreviewAutoCacher can be exercised
		// without initializing OpenGL/Vulkan in the unit-test process.
		OAK_CONFIG("GraphicsBackend") = QStringLiteral("dummy");

		DiskManager::create_instance();
		ConformManager::create_instance();
		RenderManager::create_instance();

		project_ = std::make_unique<Project>();
		project_->initialize();
	}

	void TearDown() override
	{
		RenderManager::destroy_instance();
		ConformManager::destroy_instance();
		DiskManager::destroy_instance();
	}

	ViewerOutput *create_viewer()
	{
		auto *viewer = new ViewerOutput();
		viewer->setParent(project_.get());
		return viewer;
	}

	ViewerOutput *create_viewer_with_valid_params()
	{
		ViewerOutput *viewer = create_viewer();
		viewer->set_video_params(
			VideoParams(64, 64, Rational(1, 25), PixelFormat::u8,
						VideoParams::k_rgba_channel_count));
		return viewer;
	}

	std::unique_ptr<Project> project_;
};

TEST_F(PreviewAutoCacherTest, ConstructionInitializesDefaultState)
{
	PreviewAutoCacher cacher;
	EXPECT_FALSE(cacher.is_rendering_custom_range());
}

// With a project set, a single-frame request is dispatched to the render
// pipeline, so the next request must not cancel it. After SetProject(nullptr)
// the copied graph is gone, so requests can only stay queued and each new
// request cancels the previously queued one.
TEST_F(PreviewAutoCacherTest, SetProjectToNullStopsSingleFrameDispatch)
{
	ViewerOutput *viewer = create_viewer_with_valid_params();

	// The single-frame path renders the node connected to the viewer's
	// texture input, so connect something the copier can duplicate.
	auto *solid = new SolidGenerator();
	solid->setParent(project_.get());
	Node::connect_edge(solid, NodeInput(viewer, ViewerOutput::k_texture_input));

	PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	RenderTicketPtr dispatched = cacher.get_single_frame(viewer, Rational(0));
	ASSERT_NE(dispatched, nullptr);

	// A dispatched ticket is owned by the render pipeline; the next request
	// must leave it alone
	RenderTicketPtr next = cacher.get_single_frame(viewer, Rational(1));
	ASSERT_NE(next, nullptr);
	EXPECT_EQ(dispatched->get_finish_count(), 0);
	EXPECT_TRUE(dispatched->is_running());

	cacher.set_project(nullptr);

	// Without a copied graph there is nothing to dispatch to: the request
	// stays queued and the next request cancels it
	RenderTicketPtr queued = cacher.get_single_frame(viewer, Rational(2));
	ASSERT_NE(queued, nullptr);
	RenderTicketPtr cancelling = cacher.get_single_frame(viewer, Rational(3));
	ASSERT_NE(cancelling, nullptr);
	EXPECT_EQ(queued->get_finish_count(), 1);
	EXPECT_FALSE(queued->has_result());

	cacher.set_project(nullptr);
}

// While renders are paused, forced cache ranges must stay queued; unpausing
// must dispatch them.
TEST_F(PreviewAutoCacherTest, SetRendersPausedBlocksAndResumesCacheJobs)
{
	ViewerOutput *viewer = create_viewer_with_valid_params();

	PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	QSignalSpy stop_spy(&cacher, &PreviewAutoCacher::stop_cache_proxy_tasks);

	cacher.set_renders_paused(true);
	cacher.force_cache_range(viewer, TimeRange(Rational(0), Rational(1, 25)));
	EXPECT_TRUE(cacher.is_rendering_custom_range());
	EXPECT_EQ(stop_spy.count(), 0);

	// The dummy backend finishes each ticket without a result, exhausting the
	// range as soon as it is dispatched
	cacher.set_renders_paused(false);
	EXPECT_FALSE(cacher.is_rendering_custom_range());
	EXPECT_GE(stop_spy.count(), 1);

	// Deliver the queued RenderTicketWatcher::Finished emissions so the
	// completed watchers are reaped before teardown.
	QCoreApplication::processEvents();

	cacher.set_project(nullptr);
}

// pause_thumbnails_ gates the same pending-video-job dispatch loop, so it is
// observable the same way as pause_renders_.
TEST_F(PreviewAutoCacherTest, SetThumbnailsPausedBlocksAndResumesCacheJobs)
{
	ViewerOutput *viewer = create_viewer_with_valid_params();

	PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	QSignalSpy stop_spy(&cacher, &PreviewAutoCacher::stop_cache_proxy_tasks);

	cacher.set_thumbnails_paused(true);
	cacher.force_cache_range(viewer, TimeRange(Rational(0), Rational(1, 25)));
	EXPECT_TRUE(cacher.is_rendering_custom_range());
	EXPECT_EQ(stop_spy.count(), 0);

	cacher.set_thumbnails_paused(false);
	EXPECT_FALSE(cacher.is_rendering_custom_range());
	EXPECT_GE(stop_spy.count(), 1);

	// Deliver the queued RenderTicketWatcher::Finished emissions so the
	// completed watchers are reaped before teardown.
	QCoreApplication::processEvents();

	cacher.set_project(nullptr);
}

// ClearSingleFrameRenders only cancels already-dispatched passthrough renders;
// a single-frame ticket that is still queued must be left untouched.
TEST_F(PreviewAutoCacherTest, ClearSingleFrameRendersLeavesQueuedTicketPending)
{
	ViewerOutput *viewer = create_viewer();

	PreviewAutoCacher cacher;
	RenderTicketPtr ticket = cacher.get_single_frame(viewer, Rational(0));
	ASSERT_NE(ticket, nullptr);

	cacher.clear_single_frame_renders();
	cacher.clear_single_frame_renders_that_arent_running();

	EXPECT_TRUE(ticket->is_running());
	EXPECT_EQ(ticket->get_finish_count(), 0);
	EXPECT_FALSE(ticket->has_result());
}

TEST_F(PreviewAutoCacherTest, GetSingleFrameWithoutProjectReturnsTicket)
{
	auto *viewer = new ViewerOutput();
	viewer->setParent(project_.get());

	PreviewAutoCacher cacher;
	RenderTicketPtr ticket = cacher.get_single_frame(viewer, Rational(0));
	EXPECT_NE(ticket, nullptr);
}
