#include <gtest/gtest.h>

#include <QImage>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include "core.h"
#include "node/output/viewer/viewer.h"
#include "timeline/timelinemarker.h"
#include "timeline/timelineworkarea.h"
#include "widget/resizablescrollbar/resizabletimelinescrollbar.h"
#include "widget/timebased/timebasedview.h"
#include "widget/timebased/timebasedviewselectionmanager.h"
#include "widget/timebased/timescaledobject.h"
#include "widget/timetarget/timetarget.h"

namespace
{

// HandMovableView (base of TimeBasedView) connects to Core::instance() at
// construction, so the application singleton must exist
void ensure_core()
{
	if (!olive::Core::instance()) {
		new olive::Core(); // intentionally leaked
	}
}

// TimeScaledObject is not a QObject and its interesting hooks are protected,
// so expose them through a probe
class ProbeScaledObject : public olive::TimeScaledObject {
public:
	int timebase_events = 0;
	olive::Rational last_timebase;
	int scale_events = 0;
	double last_scale = 0.0;

	void pub_set_minimum_scale(const double &min)
	{
		set_minimum_scale(min);
	}
	void pub_set_maximum_scale(const double &max)
	{
		set_maximum_scale(max);
	}

protected:
	void TimebaseChangedEvent(const olive::Rational &tb) override
	{
		timebase_events++;
		last_timebase = tb;
	}

	void ScaleChangedEvent(const double &scale) override
	{
		scale_events++;
		last_scale = scale;
	}
};

// Records the y-axis hook and exposes the protected playhead/zoom entry
// points of TimeBasedView
class ProbeTimeBasedView : public olive::TimeBasedView {
public:
	int y_scale_events = 0;
	double last_y_scale = 0.0;
	std::vector<void *> select_events;
	std::vector<void *> deselect_events;

	void pub_set_y_axis_enabled(bool e)
	{
		set_y_axis_enabled(e);
	}
	void pub_zoom(QWheelEvent *e, double multiplier, const QPointF &pos)
	{
		zoom_into_cursor_position(e, multiplier, pos);
	}
	bool pub_playhead_press(QMouseEvent *e)
	{
		return playhead_press(e);
	}
	bool pub_playhead_move(QMouseEvent *e)
	{
		return playhead_move(e);
	}
	bool pub_playhead_release(QMouseEvent *e)
	{
		return playhead_release(e);
	}

	void SelectionManagerSelectEvent(void *obj) override
	{
		select_events.push_back(obj);
	}
	void SelectionManagerDeselectEvent(void *obj) override
	{
		deselect_events.push_back(obj);
	}

protected:
	void VerticalScaleChangedEvent(double scale) override
	{
		y_scale_events++;
		last_y_scale = scale;
	}
};

// Stand-in selectable object for the templated selection manager; the drag
// paths (which need engine keyframe/marker free functions) are never
// instantiated by these tests
struct FakeSelectable {
};

// Records the protected TimeTargetObject hooks
class ProbeTimeTarget : public olive::TimeTargetObject {
public:
	std::vector<OakEngineNode *> connected;
	std::vector<OakEngineNode *> disconnected;
	std::vector<OakEngineNode *> changed;

protected:
	void TimeTargetConnectEvent(OakEngineNode *n) override
	{
		connected.push_back(n);
	}
	void TimeTargetDisconnectEvent(OakEngineNode *n) override
	{
		disconnected.push_back(n);
	}
	void TimeTargetChangedEvent(OakEngineNode *n) override
	{
		changed.push_back(n);
	}
};

QMouseEvent make_press(const QPointF &pos, Qt::MouseButton button,
					   Qt::KeyboardModifiers mods = Qt::NoModifier)
{
	return QMouseEvent(QEvent::MouseButtonPress, pos, pos, pos, button, button,
					   mods);
}

// The selection manager hit-tests in unscaled scene coordinates: it maps the
// view-local click through mapToScene() and then unscale_point(). A hidden
// QGraphicsView centers its (zero-height) scene rect inside the viewport, so
// view-local coordinates are NOT scene coordinates; use the inverse mapping
// to aim a synthetic event at an unscaled scene point deterministically.
QPoint view_pos_for_unscaled_point(olive::TimeBasedView *view, const QPointF &p)
{
	return view->mapFromScene(view->scale_point(p));
}

// Renders a widget into a fresh image so two states can be compared
// pixel-for-pixel without needing the window to be exposed
QImage render_widget(QWidget *w)
{
	QImage img(w->size(), QImage::Format_ARGB32);
	img.fill(Qt::transparent);
	w->render(&img);
	return img;
}

} // namespace

TEST(TimeScaledObject, DefaultsAreUnitScaleAndNullTimebase)
{
	olive::TimelineScaledWidget w;
	EXPECT_DOUBLE_EQ(w.get_scale(), 1.0);
	EXPECT_TRUE(w.timebase().isNull());
	// NB: timebase_dbl() is only meaningful after set_timebase(); the
	// constructor leaves it uninitialized, so it is not asserted here
	EXPECT_GT(w.get_maximum_scale(), 1.0);
}

TEST(TimeScaledObject, SetTimebaseCachesDoubleAndFiresEvent)
{
	ProbeScaledObject o;
	o.set_timebase(olive::Rational(1, 30));

	EXPECT_EQ(o.timebase(), olive::Rational(1, 30));
	EXPECT_NEAR(o.timebase_dbl(), 1.0 / 30.0, 1e-12);
	EXPECT_EQ(o.timebase_events, 1);
	EXPECT_EQ(o.last_timebase, olive::Rational(1, 30));

	o.set_timebase(olive::Rational(1, 24));
	EXPECT_EQ(o.timebase_events, 2);
	EXPECT_NEAR(o.timebase_dbl(), 1.0 / 24.0, 1e-12);
}

TEST(TimeScaledObject, ScaleClampsToMinimumAndMaximum)
{
	ProbeScaledObject o;
	o.pub_set_minimum_scale(0.5);
	o.pub_set_maximum_scale(10.0);

	// Default scale (1.0) is inside the new limits, so no clamp event yet
	EXPECT_DOUBLE_EQ(o.get_scale(), 1.0);

	o.set_scale(100.0);
	EXPECT_DOUBLE_EQ(o.get_scale(), 10.0);
	EXPECT_DOUBLE_EQ(o.last_scale, 10.0);

	o.set_scale(0.01);
	EXPECT_DOUBLE_EQ(o.get_scale(), 0.5);
	EXPECT_DOUBLE_EQ(o.last_scale, 0.5);

	o.set_scale(2.0);
	EXPECT_DOUBLE_EQ(o.get_scale(), 2.0);
	EXPECT_EQ(o.scale_events, 3);
}

TEST(TimeScaledObject, ShrinkingLimitsPullScaleIntoRange)
{
	ProbeScaledObject o;
	o.set_scale(5.0);

	// Lowering the maximum below the current scale clamps the scale down
	o.pub_set_maximum_scale(2.0);
	EXPECT_DOUBLE_EQ(o.get_scale(), 2.0);

	// Raising the minimum above the current scale pushes the scale up
	o.pub_set_maximum_scale(100.0);
	o.pub_set_minimum_scale(4.0);
	EXPECT_DOUBLE_EQ(o.get_scale(), 4.0);
}

TEST(TimeScaledObject, InvertedLimitsNeverBreakScaleClamp)
{
	ProbeScaledObject o;
	o.set_scale(5.0);

	// Narrow the valid range to [2, 3]
	o.pub_set_maximum_scale(3.0);
	o.pub_set_minimum_scale(2.0);
	EXPECT_DOUBLE_EQ(o.get_scale(), 3.0);

	// Raising the minimum past the maximum pulls the maximum up with it, so
	// the scale lands exactly on the new minimum instead of hitting an
	// inverted std::clamp (undefined behavior)
	o.pub_set_minimum_scale(10.0);
	EXPECT_DOUBLE_EQ(o.get_maximum_scale(), 10.0);
	EXPECT_DOUBLE_EQ(o.get_scale(), 10.0);

	// Lowering the maximum past the minimum pushes the minimum down instead
	o.pub_set_maximum_scale(4.0);
	EXPECT_DOUBLE_EQ(o.get_maximum_scale(), 4.0);
	EXPECT_DOUBLE_EQ(o.get_scale(), 4.0);
}

TEST(TimeScaledObject, TimeToSceneScalesTime)
{
	olive::TimelineScaledWidget w;

	// Null timebase maps everything to zero
	EXPECT_DOUBLE_EQ(w.time_to_scene(olive::Rational(10)), 0.0);

	w.set_timebase(olive::Rational(1, 30));
	w.set_scale(30.0);

	// time_to_scene is time in seconds multiplied by the scale
	EXPECT_DOUBLE_EQ(w.time_to_scene(olive::Rational(1, 30)), 1.0);
	EXPECT_DOUBLE_EQ(w.time_to_scene(olive::Rational(2)), 60.0);
}

TEST(TimeScaledObject, SceneToTimeFloorsCeilsAndRounds)
{
	olive::TimelineScaledWidget w;
	w.set_timebase(olive::Rational(1, 30));
	w.set_scale(30.0);

	// 45.5 px / 30 px-per-frame = 45.5 frames
	// floor for positive, ceil for negative, nearest when rounding
	EXPECT_EQ(w.scene_to_time(45.5), olive::Rational(45, 30));
	EXPECT_EQ(w.scene_to_time(-10.5), olive::Rational(-10, 30));
	EXPECT_EQ(w.scene_to_time(45.5, true), olive::Rational(46, 30));

	// Exact grid points map exactly
	EXPECT_EQ(w.scene_to_time(60.0), olive::Rational(2));

	// Null timebase always yields a null time
	olive::TimelineScaledWidget null_tb;
	EXPECT_TRUE(null_tb.scene_to_time(123.0).isNull());
	EXPECT_TRUE(
		olive::TimeScaledObject::scene_to_time(10.0, 1.0, olive::Rational())
			.isNull());
}

TEST(TimeScaledObject, SceneToTimeNoGridIgnoresTimebase)
{
	// Static form is a pure scale division
	const olive::Rational r =
		olive::TimeScaledObject::scene_to_time_no_grid(3.0, 2.0);
	EXPECT_NEAR(r.to_double(), 1.5, 1e-9);

	// The instance form with a null timebase divides by the current scale
	olive::TimelineScaledWidget w;
	w.set_scale(2.0);
	EXPECT_NEAR(w.scene_to_time_no_grid(3.0).to_double(), 1.5, 1e-9);

	// A set timebase does not affect the no-grid conversion
	w.set_timebase(olive::Rational(1, 30));
	EXPECT_NEAR(w.scene_to_time_no_grid(3.0).to_double(), 1.5, 1e-9);
}

TEST(TimeScaledObject, ScaleFromDimensionsMath)
{
	// (viewport / 10 * 9) / content
	EXPECT_DOUBLE_EQ(
		olive::TimeScaledObject::calculate_scale_from_dimensions(1000, 500),
		1.8);
	EXPECT_DOUBLE_EQ(
		olive::TimeScaledObject::calculate_padding_from_dimension_scale(100),
		5.0);

	olive::TimelineScaledWidget w;
	w.set_scale_from_dimensions(1000, 500);
	EXPECT_DOUBLE_EQ(w.get_scale(), 1.8);
}

TEST(TimeBasedView, ConstructionDefaults)
{
	ensure_core();

	olive::TimeBasedView view;
	EXPECT_DOUBLE_EQ(view.get_scale(), 1.0);
	EXPECT_DOUBLE_EQ(view.get_y_scale(), 1.0);
	EXPECT_FALSE(view.is_snapped());
	EXPECT_EQ(view.get_snap_service(), nullptr);
	EXPECT_EQ(view.get_viewer_node(), nullptr);
	EXPECT_FALSE(view.is_dragging_playhead());
	EXPECT_EQ(view.dragMode(), QGraphicsView::NoDrag);
	EXPECT_NE(view.scene(), nullptr);
}

TEST(TimeBasedView, SnapEnableDisableTogglesState)
{
	ensure_core();

	olive::TimeBasedView view;
	view.enable_snap({ olive::Rational(1), olive::Rational(2) });
	EXPECT_TRUE(view.is_snapped());

	view.disable_snap();
	EXPECT_FALSE(view.is_snapped());
}

TEST(TimeBasedView, YScaleEventOnlyFiresWhenAxisEnabled)
{
	ensure_core();

	ProbeTimeBasedView view;

	// Y axis disabled: the value is stored but no event fires
	view.set_y_scale(2.0);
	EXPECT_DOUBLE_EQ(view.get_y_scale(), 2.0);
	EXPECT_EQ(view.y_scale_events, 0);

	view.pub_set_y_axis_enabled(true);
	view.set_y_scale(3.0);
	EXPECT_DOUBLE_EQ(view.get_y_scale(), 3.0);
	EXPECT_EQ(view.y_scale_events, 1);
	EXPECT_DOUBLE_EQ(view.last_y_scale, 3.0);
}

TEST(TimeBasedView, ScalePointAndUnscalePointAreInverses)
{
	ensure_core();

	olive::TimeBasedView view;
	view.set_scale(2.0);
	view.set_y_scale(4.0);

	const QPointF scaled = view.scale_point(QPointF(1.5, 0.5));
	EXPECT_DOUBLE_EQ(scaled.x(), 3.0);
	EXPECT_DOUBLE_EQ(scaled.y(), 2.0);

	const QPointF unscaled = view.unscale_point(scaled);
	EXPECT_DOUBLE_EQ(unscaled.x(), 1.5);
	EXPECT_DOUBLE_EQ(unscaled.y(), 0.5);
}

TEST(TimeBasedView, EndTimeDrivesSceneRectRightEdge)
{
	ensure_core();

	olive::TimeBasedView view;
	view.resize(400, 300);
	view.set_timebase(olive::Rational(1, 30));
	view.set_scale(100.0);
	view.set_end_time(olive::Rational(10));

	// update_scene_rect pins the left edge to zero and puts the right edge
	// one viewport width past the end of the content
	QRectF rect = view.scene()->sceneRect();
	EXPECT_DOUBLE_EQ(rect.left(), 0.0);
	EXPECT_DOUBLE_EQ(rect.right(), 10.0 * 100.0 + view.width());

	// Changing the scale re-runs update_scene_rect
	view.set_scale(200.0);
	rect = view.scene()->sceneRect();
	EXPECT_DOUBLE_EQ(rect.right(), 10.0 * 200.0 + view.width());
}

TEST(TimeBasedView, ZoomWithoutYAxisEmitsHorizontalScaleChange)
{
	ensure_core();

	ProbeTimeBasedView view;
	view.resize(400, 300);
	QSignalSpy spy(&view, &olive::TimeBasedView::scale_changed);

	QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(),
					  QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
					  Qt::NoScrollPhase, false);
	view.pub_zoom(&wheel, 2.0, QPointF(10, 10));

	// The view only emits the requested scale; applying it is up to the
	// parent widget, so the stored scale is unchanged
	ASSERT_EQ(spy.count(), 1);
	EXPECT_DOUBLE_EQ(spy.first().first().toDouble(), 2.0);
	EXPECT_DOUBLE_EQ(view.get_scale(), 1.0);
	EXPECT_DOUBLE_EQ(view.get_y_scale(), 1.0);
}

TEST(TimeBasedView, ZoomWithYAxisAndShiftOnlyScalesVertically)
{
	ensure_core();

	ProbeTimeBasedView view;
	view.resize(400, 300);
	view.pub_set_y_axis_enabled(true);
	QSignalSpy spy(&view, &olive::TimeBasedView::scale_changed);

	// Shift (without Alt) restricts the zoom to the vertical axis
	QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(),
					  QPoint(0, 120), Qt::NoButton, Qt::ShiftModifier,
					  Qt::NoScrollPhase, false);
	view.pub_zoom(&wheel, 2.0, QPointF(10, 10));

	EXPECT_EQ(spy.count(), 0);
	EXPECT_DOUBLE_EQ(view.get_y_scale(), 2.0);
	EXPECT_EQ(view.y_scale_events, 1);
}

TEST(TimeBasedView, ZoomWithYAxisShiftAndAltOnlyScalesHorizontally)
{
	ensure_core();

	ProbeTimeBasedView view;
	view.resize(400, 300);
	view.pub_set_y_axis_enabled(true);
	QSignalSpy spy(&view, &olive::TimeBasedView::scale_changed);

	QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(),
					  QPoint(0, 120), Qt::NoButton,
					  Qt::ShiftModifier | Qt::AltModifier, Qt::NoScrollPhase,
					  false);
	view.pub_zoom(&wheel, 2.0, QPointF(10, 10));

	ASSERT_EQ(spy.count(), 1);
	EXPECT_DOUBLE_EQ(spy.first().first().toDouble(), 2.0);
	EXPECT_DOUBLE_EQ(view.get_y_scale(), 1.0);
	EXPECT_EQ(view.y_scale_events, 0);
}

TEST(TimeBasedView, PlayheadDragRequiresPaintedPlayheadRect)
{
	ensure_core();

	ProbeTimeBasedView view;
	view.resize(400, 300);

	// The playhead hit rect is only populated by drawForeground; before any
	// paint the rect is empty and nothing can grab the playhead
	QMouseEvent press = make_press(QPointF(50, 50), Qt::LeftButton);
	EXPECT_FALSE(view.pub_playhead_press(&press));
	EXPECT_FALSE(view.is_dragging_playhead());

	// Move/release are no-ops without an active drag
	QMouseEvent move(QEvent::MouseMove, QPointF(60, 50), QPointF(60, 50),
					 QPointF(60, 50), Qt::NoButton, Qt::LeftButton,
					 Qt::NoModifier);
	EXPECT_FALSE(view.pub_playhead_move(&move));

	QMouseEvent release(QEvent::MouseButtonRelease, QPointF(60, 50),
						QPointF(60, 50), QPointF(60, 50), Qt::LeftButton,
						Qt::NoButton, Qt::NoModifier);
	EXPECT_FALSE(view.pub_playhead_release(&release));
}

TEST(TimeBasedView, SetViewerNodeStoresAndClears)
{
	ensure_core();

	olive::TimeBasedView view;
	auto *viewer = new olive::ViewerOutput();
	OakEngineNode *handle = reinterpret_cast<OakEngineNode *>(viewer);

	view.set_viewer_node(handle);
	EXPECT_EQ(view.get_viewer_node(), handle);

	view.set_viewer_node(nullptr);
	EXPECT_EQ(view.get_viewer_node(), nullptr);

	delete viewer;
}

TEST(TimeBasedViewSelectionManager, SelectDeselectSemantics)
{
	ensure_core();

	olive::TimeBasedView view;
	olive::TimeBasedViewSelectionManager<FakeSelectable> mgr(&view);

	FakeSelectable a, b;
	EXPECT_TRUE(mgr.get_selected_objects().empty());

	EXPECT_TRUE(mgr.select(&a));
	EXPECT_TRUE(mgr.is_selected(&a));
	EXPECT_FALSE(mgr.is_selected(&b));

	// Re-selecting an already selected object is a no-op
	EXPECT_FALSE(mgr.select(&a));
	EXPECT_EQ(mgr.get_selected_objects().size(), 1u);

	EXPECT_TRUE(mgr.select(&b));
	EXPECT_EQ(mgr.get_selected_objects().size(), 2u);

	EXPECT_TRUE(mgr.deselect(&a));
	EXPECT_FALSE(mgr.is_selected(&a));

	// Deselecting something not selected reports failure
	EXPECT_FALSE(mgr.deselect(&a));

	mgr.clear_selection();
	EXPECT_TRUE(mgr.get_selected_objects().empty());
	EXPECT_FALSE(mgr.is_selected(&b));
}

TEST(TimeBasedViewSelectionManager, ObjectAtPointPrefersLastDrawn)
{
	ensure_core();

	olive::TimeBasedView view; // scale 1.0, y scale 1.0: scene == unscaled
	olive::TimeBasedViewSelectionManager<FakeSelectable> mgr(&view);

	FakeSelectable bottom, top;
	mgr.declare_drawn_object(&bottom, QRectF(0, 0, 100, 100));
	mgr.declare_drawn_object(&top, QRectF(50, 50, 100, 100));

	// In the overlap the object drawn later (i.e. on top) wins
	EXPECT_EQ(mgr.get_object_at_point(QPointF(75, 75)), &top);
	// Outside the overlap the bottom object is hit
	EXPECT_EQ(mgr.get_object_at_point(QPointF(25, 25)), &bottom);
	// A complete miss returns nullptr
	EXPECT_EQ(mgr.get_object_at_point(QPointF(500, 500)), nullptr);

	mgr.clear_drawn_objects();
	EXPECT_EQ(mgr.get_object_at_point(QPointF(25, 25)), nullptr);
}

TEST(TimeBasedViewSelectionManager, ObjectAtPointMatchesDeclaredRectBounds)
{
	ensure_core();

	olive::TimeBasedView view; // scale 1.0, y scale 1.0: scene == unscaled
	olive::TimeBasedViewSelectionManager<FakeSelectable> mgr(&view);

	FakeSelectable item;
	mgr.declare_drawn_object(&item, QRectF(10, 10, 30, 30));

	// The rect spans scene coordinates [10, 40] on both axes
	EXPECT_EQ(mgr.get_object_at_point(QPointF(10, 10)), &item);
	EXPECT_EQ(mgr.get_object_at_point(QPointF(40, 40)), &item);
	EXPECT_EQ(mgr.get_object_at_point(QPointF(41, 41)), nullptr);
	EXPECT_EQ(mgr.get_object_at_point(QPointF(9, 20)), nullptr);
}

TEST(TimeBasedViewSelectionManager, MousePressSelectsAndClears)
{
	ensure_core();

	ProbeTimeBasedView view;
	olive::TimeBasedViewSelectionManager<FakeSelectable> mgr(&view);

	FakeSelectable item;
	mgr.declare_drawn_object(&item, QRectF(10, 10, 20, 20));

	// Left click on the object selects it and notifies the view
	QMouseEvent hit = make_press(
		view_pos_for_unscaled_point(&view, QPointF(15, 15)), Qt::LeftButton);
	EXPECT_EQ(mgr.mouse_press(&hit), &item);
	EXPECT_TRUE(mgr.is_selected(&item));
	ASSERT_EQ(view.select_events.size(), 1u);
	EXPECT_EQ(view.select_events.at(0), &item);

	// Left click on empty space clears the selection and returns nothing
	QMouseEvent miss = make_press(
		view_pos_for_unscaled_point(&view, QPointF(300, 300)), Qt::LeftButton);
	EXPECT_EQ(mgr.mouse_press(&miss), nullptr);
	EXPECT_TRUE(mgr.get_selected_objects().empty());

	// Right click also selects
	QMouseEvent right = make_press(
		view_pos_for_unscaled_point(&view, QPointF(15, 15)), Qt::RightButton);
	EXPECT_EQ(mgr.mouse_press(&right), &item);
	EXPECT_TRUE(mgr.is_selected(&item));
}

TEST(TimeBasedViewSelectionManager, ShiftClickTogglesWithoutClearing)
{
	ensure_core();

	ProbeTimeBasedView view;
	olive::TimeBasedViewSelectionManager<FakeSelectable> mgr(&view);

	FakeSelectable a, b;
	mgr.declare_drawn_object(&a, QRectF(10, 10, 20, 20));
	mgr.declare_drawn_object(&b, QRectF(100, 100, 20, 20));

	// Plain click selects a
	QMouseEvent press_a = make_press(
		view_pos_for_unscaled_point(&view, QPointF(15, 15)), Qt::LeftButton);
	mgr.mouse_press(&press_a);
	ASSERT_TRUE(mgr.is_selected(&a));

	// Shift-click on b adds it without clearing a
	QMouseEvent shift_b =
		make_press(view_pos_for_unscaled_point(&view, QPointF(105, 105)),
				   Qt::LeftButton, Qt::ShiftModifier);
	EXPECT_EQ(mgr.mouse_press(&shift_b), &b);
	EXPECT_TRUE(mgr.is_selected(&a));
	EXPECT_TRUE(mgr.is_selected(&b));

	// Shift-click on the already-selected a deselects it and reports no
	// object under the cursor
	QMouseEvent shift_a =
		make_press(view_pos_for_unscaled_point(&view, QPointF(15, 15)),
				   Qt::LeftButton, Qt::ShiftModifier);
	EXPECT_EQ(mgr.mouse_press(&shift_a), nullptr);
	EXPECT_FALSE(mgr.is_selected(&a));
	EXPECT_TRUE(mgr.is_selected(&b));
	ASSERT_EQ(view.deselect_events.size(), 1u);
	EXPECT_EQ(view.deselect_events.at(0), &a);
}

TEST(TimeBasedViewSelectionManager, RubberBandSelectsIntersectingObjects)
{
	ensure_core();

	olive::TimeBasedView view;
	view.resize(400, 300);
	olive::TimeBasedViewSelectionManager<FakeSelectable> mgr(&view);

	FakeSelectable inside, outside;
	mgr.declare_drawn_object(&inside, QRectF(10, 10, 20, 20));
	mgr.declare_drawn_object(&outside, QRectF(300, 300, 20, 20));

	EXPECT_FALSE(mgr.is_rubber_banding());

	QMouseEvent press = make_press(
		view_pos_for_unscaled_point(&view, QPointF(0, 0)), Qt::LeftButton);
	mgr.rubber_band_start(&press);
	EXPECT_TRUE(mgr.is_rubber_banding());

	mgr.rubber_band_move(view_pos_for_unscaled_point(&view, QPointF(100, 100)));
	EXPECT_TRUE(mgr.is_selected(&inside));
	EXPECT_FALSE(mgr.is_selected(&outside));

	mgr.rubber_band_stop();
	EXPECT_FALSE(mgr.is_rubber_banding());

	// Stopping twice is harmless
	mgr.rubber_band_stop();
}

TEST(TimeBasedViewSelectionManager, RubberBandKeepsPreselection)
{
	ensure_core();

	olive::TimeBasedView view;
	view.resize(400, 300);
	olive::TimeBasedViewSelectionManager<FakeSelectable> mgr(&view);

	FakeSelectable pre, banded;
	mgr.declare_drawn_object(&pre, QRectF(300, 300, 20, 20));
	mgr.declare_drawn_object(&banded, QRectF(10, 10, 20, 20));
	mgr.select(&pre);

	QMouseEvent press = make_press(
		view_pos_for_unscaled_point(&view, QPointF(0, 0)), Qt::LeftButton);
	mgr.rubber_band_start(&press);
	mgr.rubber_band_move(view_pos_for_unscaled_point(&view, QPointF(100, 100)));

	// Objects selected before the band started stay selected
	EXPECT_TRUE(mgr.is_selected(&pre));
	EXPECT_TRUE(mgr.is_selected(&banded));

	mgr.rubber_band_stop();
}

TEST(TimeTargetObject, DefaultsToNullTarget)
{
	olive::TimeTargetObject target;
	EXPECT_EQ(target.get_time_target(), nullptr);
}

TEST(TimeTargetObject, SetTimeTargetFiresEventSequence)
{
	auto *node_a = new olive::ViewerOutput();
	auto *node_b = new olive::ViewerOutput();
	OakEngineNode *handle_a = reinterpret_cast<OakEngineNode *>(node_a);
	OakEngineNode *handle_b = reinterpret_cast<OakEngineNode *>(node_b);

	ProbeTimeTarget target;

	// null -> a: changed + connect, no disconnect
	target.set_time_target(handle_a);
	EXPECT_EQ(target.get_time_target(), handle_a);
	EXPECT_TRUE(target.disconnected.empty());
	ASSERT_EQ(target.changed.size(), 1u);
	EXPECT_EQ(target.changed.at(0), handle_a);
	ASSERT_EQ(target.connected.size(), 1u);
	EXPECT_EQ(target.connected.at(0), handle_a);

	// a -> b: disconnect(a), changed(b), connect(b)
	target.set_time_target(handle_b);
	ASSERT_EQ(target.disconnected.size(), 1u);
	EXPECT_EQ(target.disconnected.at(0), handle_a);
	ASSERT_EQ(target.changed.size(), 2u);
	EXPECT_EQ(target.changed.at(1), handle_b);
	ASSERT_EQ(target.connected.size(), 2u);
	EXPECT_EQ(target.connected.at(1), handle_b);

	// b -> null: disconnect(b), changed(null), no connect
	target.set_time_target(nullptr);
	EXPECT_EQ(target.get_time_target(), nullptr);
	ASSERT_EQ(target.disconnected.size(), 2u);
	EXPECT_EQ(target.disconnected.at(1), handle_b);
	ASSERT_EQ(target.changed.size(), 3u);
	EXPECT_EQ(target.changed.at(2), nullptr);
	EXPECT_EQ(target.connected.size(), 2u);

	delete node_a;
	delete node_b;
}

TEST(TimeTargetObject, AdjustedTimePassesThroughWhenEitherNodeIsNull)
{
	auto *node = new olive::ViewerOutput();
	OakEngineNode *handle = reinterpret_cast<OakEngineNode *>(node);

	olive::TimeTargetObject target;
	target.set_path_index(1);

	// Null source returns the input unchanged
	EXPECT_EQ(target.get_adjusted_time(nullptr, handle, olive::Rational(5),
									   olive::k_transform_towards_output),
			  olive::Rational(5));

	// Null target returns the input unchanged
	EXPECT_EQ(target.get_adjusted_time(handle, nullptr, olive::Rational(7),
									   olive::k_transform_towards_input),
			  olive::Rational(7));

	// Same for the TimeRange overload
	const olive::TimeRange range(olive::Rational(2), olive::Rational(9));
	const olive::TimeRange out = target.get_adjusted_time(
		nullptr, nullptr, range, olive::k_transform_towards_output);
	EXPECT_EQ(out.in(), olive::Rational(2));
	EXPECT_EQ(out.out(), olive::Rational(9));

	delete node;
}

TEST(ResizableTimelineScrollBar, ConstructionDefaults)
{
	olive::ResizableTimelineScrollBar bar;
	EXPECT_EQ(bar.orientation(), Qt::Vertical);
	EXPECT_EQ(bar.singleStep(), 20); // inherited from ResizableScrollBar
	EXPECT_DOUBLE_EQ(bar.get_scale(), 1.0); // TimeScaledObject base
	EXPECT_TRUE(bar.timebase().isNull());

	olive::ResizableTimelineScrollBar hbar(Qt::Horizontal);
	EXPECT_EQ(hbar.orientation(), Qt::Horizontal);
}

TEST(ResizableTimelineScrollBar, NullConnectionsAreSafe)
{
	olive::ResizableTimelineScrollBar bar(Qt::Horizontal);
	bar.connect_markers(nullptr);
	bar.connect_work_area(nullptr);
	bar.SetScale(2.0);

	// Disconnecting when nothing was ever connected is equally safe
	bar.connect_markers(nullptr);
	bar.connect_work_area(nullptr);
}

TEST(ResizableTimelineScrollBar, EnabledWorkAreaChangesPaintedPixels)
{
	olive::ResizableTimelineScrollBar bar(Qt::Horizontal);
	bar.resize(400, 20);
	bar.set_timebase(olive::Rational(1, 30));
	bar.set_scale(10.0);

	olive::TimelineWorkArea workarea; // disabled by default
	auto *handle = reinterpret_cast<OakEngineWorkarea *>(&workarea);
	bar.connect_work_area(handle);
	oakengine_workarea_set_range(handle, 1, 1, 10, 1);

	const QImage disabled = render_widget(&bar);

	oakengine_workarea_set_enabled(handle, 1);
	const QImage enabled = render_widget(&bar);
	EXPECT_NE(disabled, enabled);

	// Disabling again restores the base scrollbar paint exactly
	oakengine_workarea_set_enabled(handle, 0);
	EXPECT_EQ(render_widget(&bar), disabled);

	// Disconnecting the workarea also restores it while the workarea is on
	oakengine_workarea_set_enabled(handle, 1);
	bar.connect_work_area(nullptr);
	EXPECT_EQ(render_widget(&bar), disabled);
}

TEST(ResizableTimelineScrollBar, MarkersChangePaintedPixels)
{
	olive::ResizableTimelineScrollBar bar(Qt::Horizontal);
	bar.resize(400, 20);
	bar.set_timebase(olive::Rational(1, 30));
	bar.set_scale(10.0);

	const QImage clean = render_widget(&bar);

	olive::TimelineMarkerList list;
	auto *list_handle = reinterpret_cast<OakEngineMarkerList *>(&list);
	bar.connect_markers(list_handle);

	// Parenting a marker to the list registers it through the list's
	// childEvent
	auto *marker = new olive::TimelineMarker(
		0, olive::TimeRange(olive::Rational(2), olive::Rational(5)),
		QStringLiteral("m"), &list);
	ASSERT_EQ(oakengine_marker_list_count(list_handle), 1);

	const QImage with_marker = render_widget(&bar);
	EXPECT_NE(clean, with_marker);

	// Removing the marker restores the base paint. NB: the list only
	// unregisters a marker on ChildRemoved, which must arrive while the
	// marker is still fully constructed (its childEvent dynamic_casts the
	// child to TimelineMarker), so reparent before deleting — the same
	// order MarkerRemoveCommand uses.
	marker->setParent(nullptr);
	delete marker;
	EXPECT_EQ(render_widget(&bar), clean);

	// Disconnecting the list with a marker present also restores it
	auto *marker2 = new olive::TimelineMarker(
		0, olive::TimeRange(olive::Rational(2), olive::Rational(5)),
		QStringLiteral("m2"), &list);
	bar.connect_markers(nullptr);
	EXPECT_EQ(render_widget(&bar), clean);
	delete marker2;
}

