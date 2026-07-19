#include <gtest/gtest.h>

#include "widget/timebased/timebasedwidget.h"
#include "widget/timeruler/seekablewidget.h"
#include "node/output/viewer/viewer.h"

TEST(TimeBasedWidget, ConnectViewerNodeNullSafe)
{
	olive::TimeBasedWidget widget(false, false);
	widget.ConnectViewerNode(nullptr);
	EXPECT_EQ(widget.GetConnectedNode(), nullptr);
}

TEST(TimeBasedWidget, ConnectedNodeClearsOnDelete)
{
	olive::TimeBasedWidget widget(false, false);
	auto *viewer = new olive::ViewerOutput();
	widget.ConnectViewerNode(viewer);
	EXPECT_EQ(widget.GetConnectedNode(), viewer);
	delete viewer;
	EXPECT_EQ(widget.GetConnectedNode(), nullptr);
}

TEST(SeekableWidget, ConstructionInitializesDefaults)
{
	olive::SeekableWidget widget;
	EXPECT_FALSE(widget.IsDraggingPlayhead());
	EXPECT_FALSE(widget.HasItemsSelected());
	EXPECT_EQ(widget.GetMarkers(), nullptr);
	EXPECT_EQ(widget.GetWorkArea(), nullptr);
}

TEST(SeekableWidget, SetScrollAdjustsScrollBar)
{
	olive::SeekableWidget widget;
	widget.resize(400, 100);

	widget.SetScroll(0);
	EXPECT_EQ(widget.GetScroll(), 0);

	// Give the scene a deterministic length much wider than the viewport so
	// the horizontal scrollbar gains a non-zero range (60 seconds at
	// 100 px/second)
	widget.SetTimebase(olive::rational(1, 30));
	widget.SetScale(100.0);
	widget.SetEndTime(olive::rational(60));

	const int max_scroll = widget.horizontalScrollBar()->maximum();
	ASSERT_GT(max_scroll, 0);

	widget.SetScroll(max_scroll);
	EXPECT_EQ(widget.GetScroll(), max_scroll);

	// Values beyond the range clamp to the scrollbar's maximum
	widget.SetScroll(max_scroll + 1000);
	EXPECT_EQ(widget.GetScroll(), max_scroll);
}

TEST(SeekableWidget, SetMarkersAndWorkAreaAreReflected)
{
	olive::SeekableWidget widget;

	olive::TimelineMarkerList markers;
	olive::TimelineWorkArea workarea;

	widget.SetMarkers(&markers);
	widget.SetWorkArea(&workarea);

	EXPECT_EQ(widget.GetMarkers(), &markers);
	EXPECT_EQ(widget.GetWorkArea(), &workarea);
}

TEST(SeekableWidget, MarkerEditingEnabledToggles)
{
	olive::SeekableWidget widget;
	EXPECT_TRUE(widget.IsMarkerEditingEnabled());

	widget.SetMarkerEditingEnabled(false);
	EXPECT_FALSE(widget.IsMarkerEditingEnabled());

	widget.SetMarkerEditingEnabled(true);
	EXPECT_TRUE(widget.IsMarkerEditingEnabled());
}
