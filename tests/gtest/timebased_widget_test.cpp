#include <gtest/gtest.h>

#include "widget/timebased/timebasedwidget.h"
#include "widget/timeruler/seekablewidget.h"
#include "node/output/viewer/viewer.h"

TEST(TimeBasedWidget, ConnectViewerNodeNullSafe)
{
	olive::TimeBasedWidget widget(false, false);
	widget.connect_viewer_node(nullptr);
	EXPECT_EQ(widget.get_connected_node(), nullptr);
}

TEST(TimeBasedWidget, ConnectedNodeClearsOnDelete)
{
	olive::TimeBasedWidget widget(false, false);
	auto *viewer = new olive::ViewerOutput();
	widget.connect_viewer_node(viewer);
	EXPECT_EQ(widget.get_connected_node(), viewer);
	delete viewer;
	EXPECT_EQ(widget.get_connected_node(), nullptr);
}

TEST(SeekableWidget, ConstructionInitializesDefaults)
{
	olive::SeekableWidget widget;
	EXPECT_FALSE(widget.is_dragging_playhead());
	EXPECT_FALSE(widget.has_items_selected());
	EXPECT_EQ(widget.get_markers(), nullptr);
	EXPECT_EQ(widget.get_work_area(), nullptr);
}

TEST(SeekableWidget, SetScrollAdjustsScrollBar)
{
	olive::SeekableWidget widget;
	widget.resize(400, 100);

	widget.set_scroll(0);
	EXPECT_EQ(widget.get_scroll(), 0);

	// Give the scene a deterministic length much wider than the viewport so
	// the horizontal scrollbar gains a non-zero range (60 seconds at
	// 100 px/second)
	widget.set_timebase(olive::Rational(1, 30));
	widget.set_scale(100.0);
	widget.set_end_time(olive::Rational(60));

	const int max_scroll = widget.horizontalScrollBar()->maximum();
	ASSERT_GT(max_scroll, 0);

	widget.set_scroll(max_scroll);
	EXPECT_EQ(widget.get_scroll(), max_scroll);

	// Values beyond the range clamp to the scrollbar's maximum
	widget.set_scroll(max_scroll + 1000);
	EXPECT_EQ(widget.get_scroll(), max_scroll);
}

TEST(SeekableWidget, SetMarkersAndWorkAreaAreReflected)
{
	olive::SeekableWidget widget;

	olive::TimelineMarkerList markers;
	olive::TimelineWorkArea workarea;

	widget.set_markers(&markers);
	widget.set_work_area(&workarea);

	EXPECT_EQ(widget.get_markers(), &markers);
	EXPECT_EQ(widget.get_work_area(), &workarea);
}

TEST(SeekableWidget, MarkerEditingEnabledToggles)
{
	olive::SeekableWidget widget;
	EXPECT_TRUE(widget.is_marker_editing_enabled());

	widget.set_marker_editing_enabled(false);
	EXPECT_FALSE(widget.is_marker_editing_enabled());

	widget.set_marker_editing_enabled(true);
	EXPECT_TRUE(widget.is_marker_editing_enabled());
}
