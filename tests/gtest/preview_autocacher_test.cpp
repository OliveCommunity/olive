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
		ColorManager::SetUpDefaultConfig();

		// Use the dummy render backend so PreviewAutoCacher can be exercised
		// without initializing OpenGL/Vulkan in the unit-test process.
		OLIVE_CONFIG("GraphicsBackend") = QStringLiteral("dummy");

		DiskManager::CreateInstance();
		ConformManager::CreateInstance();
		RenderManager::CreateInstance();

		project_ = std::make_unique<Project>();
		project_->Initialize();
	}

	void TearDown() override
	{
		RenderManager::DestroyInstance();
		ConformManager::DestroyInstance();
		DiskManager::DestroyInstance();
	}

	ViewerOutput *CreateViewer()
	{
		auto *viewer = new ViewerOutput();
		viewer->setParent(project_.get());
		return viewer;
	}

	ViewerOutput *CreateViewerWithValidParams()
	{
		ViewerOutput *viewer = CreateViewer();
		viewer->SetVideoParams(
			VideoParams(64, 64, rational(1, 25), PixelFormat::U8,
						VideoParams::kRGBAChannelCount));
		return viewer;
	}

	std::unique_ptr<Project> project_;
};

TEST_F(PreviewAutoCacherTest, ConstructionInitializesDefaultState)
{
	PreviewAutoCacher cacher;
	EXPECT_FALSE(cacher.IsRenderingCustomRange());
}

// With a project set, a single-frame request is dispatched to the render
// pipeline, so the next request must not cancel it. After SetProject(nullptr)
// the copied graph is gone, so requests can only stay queued and each new
// request cancels the previously queued one.
TEST_F(PreviewAutoCacherTest, SetProjectToNullStopsSingleFrameDispatch)
{
	ViewerOutput *viewer = CreateViewerWithValidParams();

	// The single-frame path renders the node connected to the viewer's
	// texture input, so connect something the copier can duplicate.
	auto *solid = new SolidGenerator();
	solid->setParent(project_.get());
	Node::ConnectEdge(solid, NodeInput(viewer, ViewerOutput::kTextureInput));

	PreviewAutoCacher cacher;
	cacher.SetProject(project_.get());

	RenderTicketPtr dispatched = cacher.GetSingleFrame(viewer, rational(0));
	ASSERT_NE(dispatched, nullptr);

	// A dispatched ticket is owned by the render pipeline; the next request
	// must leave it alone
	RenderTicketPtr next = cacher.GetSingleFrame(viewer, rational(1));
	ASSERT_NE(next, nullptr);
	EXPECT_EQ(dispatched->GetFinishCount(), 0);
	EXPECT_TRUE(dispatched->IsRunning());

	cacher.SetProject(nullptr);

	// Without a copied graph there is nothing to dispatch to: the request
	// stays queued and the next request cancels it
	RenderTicketPtr queued = cacher.GetSingleFrame(viewer, rational(2));
	ASSERT_NE(queued, nullptr);
	RenderTicketPtr cancelling = cacher.GetSingleFrame(viewer, rational(3));
	ASSERT_NE(cancelling, nullptr);
	EXPECT_EQ(queued->GetFinishCount(), 1);
	EXPECT_FALSE(queued->HasResult());

	cacher.SetProject(nullptr);
}

// While renders are paused, forced cache ranges must stay queued; unpausing
// must dispatch them.
TEST_F(PreviewAutoCacherTest, SetRendersPausedBlocksAndResumesCacheJobs)
{
	ViewerOutput *viewer = CreateViewerWithValidParams();

	PreviewAutoCacher cacher;
	cacher.SetProject(project_.get());

	QSignalSpy stop_spy(&cacher, &PreviewAutoCacher::StopCacheProxyTasks);

	cacher.SetRendersPaused(true);
	cacher.ForceCacheRange(viewer, TimeRange(rational(0), rational(1, 25)));
	EXPECT_TRUE(cacher.IsRenderingCustomRange());
	EXPECT_EQ(stop_spy.count(), 0);

	// The dummy backend finishes each ticket without a result, exhausting the
	// range as soon as it is dispatched
	cacher.SetRendersPaused(false);
	EXPECT_FALSE(cacher.IsRenderingCustomRange());
	EXPECT_GE(stop_spy.count(), 1);

	// Deliver the queued RenderTicketWatcher::Finished emissions so the
	// completed watchers are reaped before teardown.
	QCoreApplication::processEvents();

	cacher.SetProject(nullptr);
}

// pause_thumbnails_ gates the same pending-video-job dispatch loop, so it is
// observable the same way as pause_renders_.
TEST_F(PreviewAutoCacherTest, SetThumbnailsPausedBlocksAndResumesCacheJobs)
{
	ViewerOutput *viewer = CreateViewerWithValidParams();

	PreviewAutoCacher cacher;
	cacher.SetProject(project_.get());

	QSignalSpy stop_spy(&cacher, &PreviewAutoCacher::StopCacheProxyTasks);

	cacher.SetThumbnailsPaused(true);
	cacher.ForceCacheRange(viewer, TimeRange(rational(0), rational(1, 25)));
	EXPECT_TRUE(cacher.IsRenderingCustomRange());
	EXPECT_EQ(stop_spy.count(), 0);

	cacher.SetThumbnailsPaused(false);
	EXPECT_FALSE(cacher.IsRenderingCustomRange());
	EXPECT_GE(stop_spy.count(), 1);

	// Deliver the queued RenderTicketWatcher::Finished emissions so the
	// completed watchers are reaped before teardown.
	QCoreApplication::processEvents();

	cacher.SetProject(nullptr);
}

// ClearSingleFrameRenders only cancels already-dispatched passthrough renders;
// a single-frame ticket that is still queued must be left untouched.
TEST_F(PreviewAutoCacherTest, ClearSingleFrameRendersLeavesQueuedTicketPending)
{
	ViewerOutput *viewer = CreateViewer();

	PreviewAutoCacher cacher;
	RenderTicketPtr ticket = cacher.GetSingleFrame(viewer, rational(0));
	ASSERT_NE(ticket, nullptr);

	cacher.ClearSingleFrameRenders();
	cacher.ClearSingleFrameRendersThatArentRunning();

	EXPECT_TRUE(ticket->IsRunning());
	EXPECT_EQ(ticket->GetFinishCount(), 0);
	EXPECT_FALSE(ticket->HasResult());
}

TEST_F(PreviewAutoCacherTest, GetSingleFrameWithoutProjectReturnsTicket)
{
	auto *viewer = new ViewerOutput();
	viewer->setParent(project_.get());

	PreviewAutoCacher cacher;
	RenderTicketPtr ticket = cacher.GetSingleFrame(viewer, rational(0));
	EXPECT_NE(ticket, nullptr);
}
