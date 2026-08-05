#include <gtest/gtest.h>

#include <QApplication>
#include <QCloseEvent>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QTextCursor>
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

#include "node/output/viewer/viewer.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "render/videoparams.h"
#include "widget/viewer/displaybuffer.h"
#include "widget/viewer/vieweroutpututils.h"
#include "widget/viewer/viewerplaybacktimer.h"
#include "widget/viewer/viewerpreventsleep.h"
#include "widget/viewer/viewersizer.h"
#include "widget/viewer/viewertexteditor.h"

namespace
{

// ViewerSizer's scrollbars are created in a fixed order (horizontal first),
// and findChildren preserves child creation order
QList<QScrollBar *> sizer_scrollbars(olive::ViewerSizer *sizer)
{
	return sizer->findChildren<QScrollBar *>();
}

// ViewerTextEditorToolBar push buttons in creation order (see
// viewertexteditor.cpp): underline, strikethrough, color, align left/center/
// right/justify, align top/middle/bottom, small caps
QList<QPushButton *> toolbar_buttons(olive::ViewerTextEditorToolBar *toolbar)
{
	return toolbar->findChildren<QPushButton *>();
}

enum ToolBarButton {
	k_underline = 0,
	k_strikethrough = 1,
	k_color = 2,
	k_align_left = 3,
	k_align_center = 4,
	k_align_right = 5,
	k_align_justify = 6,
	k_align_top = 7,
	k_align_middle = 8,
	k_align_bottom = 9,
	k_small_caps = 10,
};

QMatrix4x4 last_matrix(QSignalSpy &spy)
{
	return spy.last().first().value<QMatrix4x4>();
}

} // namespace

TEST(ViewerSizer, ScrollBarsStartHiddenAndZoomIsFit)
{
	olive::ViewerSizer sizer;
	const auto bars = sizer_scrollbars(&sizer);
	ASSERT_EQ(bars.size(), 2);
	EXPECT_EQ(bars.at(0)->orientation(), Qt::Horizontal);
	EXPECT_EQ(bars.at(1)->orientation(), Qt::Vertical);
	EXPECT_FALSE(bars.at(0)->isVisibleTo(&sizer));
	EXPECT_FALSE(bars.at(1)->isVisibleTo(&sizer));
}

TEST(ViewerSizer, ZeroChildSizeFillsContainerWithoutScaleRequest)
{
	olive::ViewerSizer sizer;
	sizer.resize(400, 300);

	auto *child = new QWidget();
	sizer.set_widget(child);

	QSignalSpy scale_spy(&sizer, &olive::ViewerSizer::request_scale);

	// width_/height_ default to 0, so the child simply fills the container
	EXPECT_EQ(child->geometry(), QRect(0, 0, 400, 300));
	EXPECT_EQ(scale_spy.count(), 0);
}

TEST(ViewerSizer, SetWidgetDestroysPreviousWidget)
{
	olive::ViewerSizer sizer;
	sizer.resize(200, 200);

	QPointer<QWidget> first = new QWidget();
	sizer.set_widget(first);
	ASSERT_FALSE(first.isNull());
	EXPECT_EQ(first->parentWidget(), &sizer);

	auto *second = new QWidget();
	sizer.set_widget(second);

	// set_widget() deletes the previously installed widget
	EXPECT_TRUE(first.isNull());
	EXPECT_EQ(second->parentWidget(), &sizer);
}

TEST(ViewerSizer, WideContainerScalesByHeight)
{
	olive::ViewerSizer sizer;
	sizer.resize(400, 300);
	sizer.set_widget(new QWidget());

	QSignalSpy scale_spy(&sizer, &olive::ViewerSizer::request_scale);

	// Square image in a 4:3 container: container is wider, so the matrix
	// compresses X by sequence_aspect / container_aspect = 1 / (4/3)
	sizer.set_child_size(100, 100);

	ASSERT_GE(scale_spy.count(), 1);
	const QMatrix4x4 m = last_matrix(scale_spy);
	EXPECT_NEAR(m(0, 0), 0.75, 1e-9);
	EXPECT_NEAR(m(1, 1), 1.0, 1e-9);
}

TEST(ViewerSizer, TallContainerScalesByWidth)
{
	olive::ViewerSizer sizer;
	sizer.resize(300, 300);
	sizer.set_widget(new QWidget());

	QSignalSpy scale_spy(&sizer, &olive::ViewerSizer::request_scale);

	// 2:1 image in a square container: container is taller, so the matrix
	// compresses Y by container_aspect / sequence_aspect = 1 / 2
	sizer.set_child_size(200, 100);

	ASSERT_GE(scale_spy.count(), 1);
	const QMatrix4x4 m = last_matrix(scale_spy);
	EXPECT_NEAR(m(0, 0), 1.0, 1e-9);
	EXPECT_NEAR(m(1, 1), 0.5, 1e-9);
}

TEST(ViewerSizer, PixelAspectRatioFactorsIntoScale)
{
	olive::ViewerSizer sizer;
	sizer.resize(300, 300);
	sizer.set_widget(new QWidget());
	sizer.set_child_size(100, 100);

	QSignalSpy scale_spy(&sizer, &olive::ViewerSizer::request_scale);

	// 2:1 pixel aspect doubles the sequence aspect -> 2:1 in a square container
	sizer.set_pixel_aspect_ratio(olive::Rational(2, 1));
	ASSERT_GE(scale_spy.count(), 1);
	QMatrix4x4 m = last_matrix(scale_spy);
	EXPECT_NEAR(m(0, 0), 1.0, 1e-9);
	EXPECT_NEAR(m(1, 1), 0.5, 1e-9);

	// 1:2 pixel aspect halves it -> 1:2 image, container wider than image
	sizer.set_pixel_aspect_ratio(olive::Rational(1, 2));
	m = last_matrix(scale_spy);
	EXPECT_NEAR(m(0, 0), 0.5, 1e-9);
	EXPECT_NEAR(m(1, 1), 1.0, 1e-9);
}

TEST(ViewerSizer, ExplicitZoomShowsScrollBars)
{
	olive::ViewerSizer sizer;
	sizer.resize(200, 200);
	sizer.set_widget(new QWidget());
	sizer.set_child_size(100, 100);
	sizer.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&sizer));

	const auto bars = sizer_scrollbars(&sizer);
	ASSERT_EQ(bars.size(), 2);

	QSignalSpy scale_spy(&sizer, &olive::ViewerSizer::request_scale);

	// 100px child at 400% is 400px > 200px container, so both bars appear
	sizer.set_zoom(4.0);
	EXPECT_TRUE(bars.at(0)->isVisible());
	EXPECT_TRUE(bars.at(1)->isVisible());

	// The zoom is folded into the requested scale on top of the fit scale
	ASSERT_GE(scale_spy.count(), 1);
	const QMatrix4x4 m = last_matrix(scale_spy);
	EXPECT_GT(m(0, 0), 1.0);
	EXPECT_GT(m(1, 1), 1.0);

	// Back to fit: scrollbars disappear again
	sizer.set_zoom(-1);
	EXPECT_FALSE(bars.at(0)->isVisible());
	EXPECT_FALSE(bars.at(1)->isVisible());
}

TEST(ViewerSizer, ScrollBarMovementEmitsTranslate)
{
	olive::ViewerSizer sizer;
	sizer.resize(200, 200);
	sizer.set_widget(new QWidget());
	sizer.set_child_size(100, 100);
	sizer.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&sizer));
	sizer.set_zoom(4.0);

	const auto bars = sizer_scrollbars(&sizer);
	ASSERT_EQ(bars.size(), 2);
	QScrollBar *horiz = bars.at(0);
	ASSERT_TRUE(horiz->isVisible());
	ASSERT_GT(horiz->maximum(), 0);

	QSignalSpy translate_spy(&sizer, &olive::ViewerSizer::request_translate);

	// Scrolled to the far right the offset is negative. Go to the maximum
	// first so the subsequent setValue(0) is a real change (QScrollBar only
	// emits valueChanged, which drives scroll_bar_moved, on actual changes)
	horiz->setValue(horiz->maximum());
	ASSERT_GE(translate_spy.count(), 1);
	const float at_max = last_matrix(translate_spy)(0, 3);
	EXPECT_LT(at_max, 0.0f);

	// Back at 0 the view centers on the left edge: positive X offset
	translate_spy.clear();
	horiz->setValue(0);
	ASSERT_GE(translate_spy.count(), 1);
	const float at_min = last_matrix(translate_spy)(0, 3);
	EXPECT_GT(at_min, 0.0f);
}

TEST(ViewerSizer, HandDragMoveAdjustsVisibleScrollBars)
{
	olive::ViewerSizer sizer;
	sizer.resize(200, 200);
	sizer.set_widget(new QWidget());
	sizer.set_child_size(100, 100);
	sizer.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&sizer));
	sizer.set_zoom(4.0);

	const auto bars = sizer_scrollbars(&sizer);
	ASSERT_EQ(bars.size(), 2);
	bars.at(0)->setValue(50);
	bars.at(1)->setValue(30);

	// A drag of (+10, +5) pulls the content, decreasing both scroll values
	sizer.hand_drag_move(10, 5);
	EXPECT_EQ(bars.at(0)->value(), 40);
	EXPECT_EQ(bars.at(1)->value(), 25);
}

TEST(ViewerSizer, AnchoredZoomOutToFitResetsScrollPositions)
{
	olive::ViewerSizer sizer;
	sizer.resize(200, 200);
	sizer.set_widget(new QWidget());
	sizer.set_child_size(100, 100);
	sizer.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&sizer));

	const auto bars = sizer_scrollbars(&sizer);
	ASSERT_EQ(bars.size(), 2);

	sizer.set_zoom_anchored(4.0, 10.0, 10.0);
	ASSERT_TRUE(bars.at(0)->isVisible());

	// A non-positive anchored zoom is the "fit" command: zoom clears and the
	// scroll offsets reset
	sizer.set_zoom_anchored(0.0, 0.0, 0.0);
	EXPECT_FALSE(bars.at(0)->isVisible());
	EXPECT_FALSE(bars.at(1)->isVisible());
	EXPECT_EQ(bars.at(0)->value(), 0);
	EXPECT_EQ(bars.at(1)->value(), 0);
}

TEST(ViewerPlaybackTimer, ZeroSpeedKeepsStartTimestamp)
{
	olive::ViewerPlaybackTimer timer;
	timer.start(42, 0, 1.0 / 30.0);

	// No elapsed real time can move the timestamp when the speed is zero
	EXPECT_EQ(timer.get_timestamp_now(), 42);
}

TEST(ViewerPlaybackTimer, HugeTimebaseFreezesFrameAdvance)
{
	olive::ViewerPlaybackTimer timer;
	// 1000-second frames: any sub-second test run still rounds to zero frames
	timer.start(7, 3, 1000.0);
	EXPECT_EQ(timer.get_timestamp_now(), 7);
}

TEST(ViewerPlaybackTimer, NegativeSpeedAlsoStartsAtTimestamp)
{
	olive::ViewerPlaybackTimer timer;
	timer.start(100, -2, 1000.0);
	EXPECT_EQ(timer.get_timestamp_now(), 100);
}

TEST(ViewerPlaybackTimer, RestartReplacesAnchor)
{
	olive::ViewerPlaybackTimer timer;
	timer.start(1, 0, 1.0 / 30.0);
	EXPECT_EQ(timer.get_timestamp_now(), 1);

	timer.start(500, 0, 1.0 / 30.0);
	EXPECT_EQ(timer.get_timestamp_now(), 500);
}

TEST(ViewerPreventSleep, ToggleOnOffDoesNotCrash)
{
	// No observable state is exposed; on macOS this creates/releases an IOPM
	// assertion, elsewhere it talks to the OS sleep-inhibit service. Toggling
	// twice exercises the released-assertion no-op path.
	olive::prevent_sleep(true);
	olive::prevent_sleep(false);
	olive::prevent_sleep(false);
	SUCCEED();
}

TEST(DisplayBuffer, NullHandlesAreSafeToWrapAndDestroy)
{
	auto tex = olive::oak_make_shared_texture(nullptr);
	EXPECT_EQ(tex->handle, nullptr);
	EXPECT_EQ(tex->type, olive::OakSharedBuffer::k_texture);

	auto frame = olive::oak_make_shared_frame(nullptr);
	EXPECT_EQ(frame->handle, nullptr);
	EXPECT_EQ(frame->type, olive::OakSharedBuffer::k_frame);

	// Both facade free functions ignore nullptr, so destroying these is safe
	tex.reset();
	frame.reset();
	SUCCEED();
}

TEST(DisplayBuffer, SharedCopiesExtendHandleLifetime)
{
	void *raw = oakengine_codec_frame_create();
	ASSERT_NE(raw, nullptr);

	auto frame = olive::oak_make_shared_frame(raw);
	EXPECT_EQ(frame->handle, raw);
	EXPECT_EQ(frame.use_count(), 1);

	{
		auto copy = frame;
		EXPECT_EQ(frame.use_count(), 2);
		EXPECT_EQ(copy->handle, raw);
	}

	// The copy is gone but the original still holds the handle
	EXPECT_EQ(frame.use_count(), 1);
}

TEST(DisplayBuffer, RoundTripsThroughVariant)
{
	auto frame = olive::oak_make_shared_frame(nullptr);

	QVariant v = QVariant::fromValue(frame);
	EXPECT_TRUE(v.isValid());

	const olive::OakSharedBufferPtr out = v.value<olive::OakSharedBufferPtr>();
	EXPECT_EQ(out, frame);
	EXPECT_EQ(out.use_count(), frame.use_count());
}

TEST(ViewerOutputUtils, NullViewerReturnsDefaults)
{
	const oak::VideoParams vp = olive::viewer_output_video_params(nullptr);
	EXPECT_EQ(vp.width(), 0);
	EXPECT_EQ(vp.height(), 0);
	EXPECT_EQ(vp, olive::empty_video_params());

	const olive::AudioParams ap = olive::viewer_output_audio_params(nullptr);
	EXPECT_EQ(ap.sample_rate(), 0);
	EXPECT_FALSE(ap.is_valid());

	EXPECT_DOUBLE_EQ(olive::viewer_output_playhead(nullptr).to_double(), 0.0);
	EXPECT_DOUBLE_EQ(olive::viewer_output_length(nullptr).to_double(), 0.0);
	EXPECT_DOUBLE_EQ(olive::viewer_output_video_length(nullptr).to_double(), 0.0);
	EXPECT_DOUBLE_EQ(olive::viewer_output_audio_length(nullptr).to_double(), 0.0);
	EXPECT_DOUBLE_EQ(olive::sequence_timebase(nullptr).to_double(), 0.0);

	EXPECT_FALSE(olive::viewer_output_is_sequence(nullptr));
	EXPECT_FALSE(olive::viewer_output_is_footage(nullptr));
	EXPECT_FALSE(olive::viewer_output_node_type_is(nullptr, "anything"));
}

TEST(ViewerOutputUtils, VideoParamsRoundTripThroughFacade)
{
	olive::ViewerOutput viewer;
	viewer.set_video_params(olive::VideoParams(320, 240, olive::Rational(1, 25),
											 olive::PixelFormat::u8,
											 olive::VideoParams::k_rgba_channel_count));

	const oak::VideoParams vp = olive::viewer_output_video_params(&viewer);
	EXPECT_EQ(vp.width(), 320);
	EXPECT_EQ(vp.height(), 240);
	EXPECT_TRUE(vp.is_valid());

	// sequence_timebase is the frame duration as a rational
	const olive::Rational tb = olive::sequence_timebase(&viewer);
	EXPECT_EQ(tb.numerator(), 1);
	EXPECT_EQ(tb.denominator(), 25);

	EXPECT_EQ(vp.time_base().numerator(), 1);
	EXPECT_EQ(vp.time_base().denominator(), 25);
}

TEST(ViewerOutputUtils, PlayheadRoundTripsAndLengthDefaultsToZero)
{
	olive::ViewerOutput viewer;

	viewer.set_playhead(olive::Rational(3, 2));
	const olive::Rational ph = olive::viewer_output_playhead(&viewer);
	EXPECT_EQ(ph.numerator(), 3);
	EXPECT_EQ(ph.denominator(), 2);

	// A fresh viewer has no content, so all lengths are zero
	EXPECT_DOUBLE_EQ(olive::viewer_output_length(&viewer).to_double(), 0.0);
	EXPECT_DOUBLE_EQ(olive::viewer_output_video_length(&viewer).to_double(), 0.0);
	EXPECT_DOUBLE_EQ(olive::viewer_output_audio_length(&viewer).to_double(), 0.0);
}

TEST(ViewerOutputUtils, AudioParamsRoundTripThroughFacade)
{
	olive::ViewerOutput viewer;
	const uint64_t stereo_mask = 0x3; // front-left | front-right
	viewer.set_audio_params(
		olive::AudioParams(48000, stereo_mask, olive::SampleFormat::s16));

	const olive::AudioParams ap = olive::viewer_output_audio_params(&viewer);
	EXPECT_TRUE(ap.is_valid());
	EXPECT_EQ(ap.sample_rate(), 48000);
	EXPECT_EQ(ap.channel_layout(), stereo_mask);
	EXPECT_EQ(olive::SampleFormat::Format(ap.format()), olive::SampleFormat::s16);
}

TEST(ViewerOutputUtils, TypeProbesMatchNodeIds)
{
	olive::Sequence sequence;
	EXPECT_TRUE(olive::viewer_output_is_sequence(&sequence));
	EXPECT_FALSE(olive::viewer_output_is_footage(&sequence));
	EXPECT_TRUE(olive::viewer_output_node_type_is(
		&sequence, "org.olivevideoeditor.Olive.sequence"));
	EXPECT_FALSE(
		olive::viewer_output_node_type_is(&sequence, "org.oak.test.nonexistent"));

	olive::Footage footage;
	EXPECT_TRUE(olive::viewer_output_is_footage(&footage));
	EXPECT_FALSE(olive::viewer_output_is_sequence(&footage));

	// A plain ViewerOutput is neither a Sequence nor Footage
	olive::ViewerOutput viewer;
	EXPECT_FALSE(olive::viewer_output_is_sequence(&viewer));
	EXPECT_FALSE(olive::viewer_output_is_footage(&viewer));
}

TEST(ViewerTextEditorToolBar, SettersUpdateButtonsWithoutEmitting)
{
	olive::ViewerTextEditorToolBar toolbar;
	const auto buttons = toolbar_buttons(&toolbar);
	ASSERT_EQ(buttons.size(), 11);

	QSignalSpy underline_spy(&toolbar,
							 &olive::ViewerTextEditorToolBar::underline_changed);
	QSignalSpy strike_spy(
		&toolbar, &olive::ViewerTextEditorToolBar::strikethrough_changed);
	QSignalSpy caps_spy(&toolbar,
						&olive::ViewerTextEditorToolBar::small_caps_changed);

	toolbar.set_underline(true);
	EXPECT_TRUE(buttons.at(k_underline)->isChecked());
	toolbar.set_strikethrough(true);
	EXPECT_TRUE(buttons.at(k_strikethrough)->isChecked());
	toolbar.set_small_caps(true);
	EXPECT_TRUE(buttons.at(k_small_caps)->isChecked());

	// Programmatic sync from the editor must not bounce back as user edits
	EXPECT_EQ(underline_spy.count(), 0);
	EXPECT_EQ(strike_spy.count(), 0);
	EXPECT_EQ(caps_spy.count(), 0);
}

TEST(ViewerTextEditorToolBar, SetAlignmentChecksMatchingButtonOnly)
{
	olive::ViewerTextEditorToolBar toolbar;
	const auto buttons = toolbar_buttons(&toolbar);
	ASSERT_EQ(buttons.size(), 11);

	toolbar.set_alignment(Qt::AlignHCenter);
	EXPECT_FALSE(buttons.at(k_align_left)->isChecked());
	EXPECT_TRUE(buttons.at(k_align_center)->isChecked());
	EXPECT_FALSE(buttons.at(k_align_right)->isChecked());
	EXPECT_FALSE(buttons.at(k_align_justify)->isChecked());

	toolbar.set_alignment(Qt::AlignRight);
	EXPECT_FALSE(buttons.at(k_align_left)->isChecked());
	EXPECT_FALSE(buttons.at(k_align_center)->isChecked());
	EXPECT_TRUE(buttons.at(k_align_right)->isChecked());
	EXPECT_FALSE(buttons.at(k_align_justify)->isChecked());
}

TEST(ViewerTextEditorToolBar, SetVerticalAlignmentChecksMatchingButtonOnly)
{
	olive::ViewerTextEditorToolBar toolbar;
	const auto buttons = toolbar_buttons(&toolbar);
	ASSERT_EQ(buttons.size(), 11);

	toolbar.set_vertical_alignment(Qt::AlignVCenter);
	EXPECT_FALSE(buttons.at(k_align_top)->isChecked());
	EXPECT_TRUE(buttons.at(k_align_middle)->isChecked());
	EXPECT_FALSE(buttons.at(k_align_bottom)->isChecked());

	toolbar.set_vertical_alignment(Qt::AlignBottom);
	EXPECT_FALSE(buttons.at(k_align_top)->isChecked());
	EXPECT_FALSE(buttons.at(k_align_middle)->isChecked());
	EXPECT_TRUE(buttons.at(k_align_bottom)->isChecked());
}

TEST(ViewerTextEditorToolBar, AlignmentButtonsEmitTheirAlignment)
{
	olive::ViewerTextEditorToolBar toolbar;
	const auto buttons = toolbar_buttons(&toolbar);
	ASSERT_EQ(buttons.size(), 11);

	// Lambda capture instead of QSignalSpy: Qt::Alignment is not guaranteed
	// to be a registered metatype for the spy on every platform
	QVector<Qt::Alignment> h_received;
	QObject::connect(&toolbar,
					 &olive::ViewerTextEditorToolBar::alignment_changed,
					 [&h_received](Qt::Alignment a) { h_received.append(a); });
	QVector<Qt::Alignment> v_received;
	QObject::connect(
		&toolbar, &olive::ViewerTextEditorToolBar::vertical_alignment_changed,
		[&v_received](Qt::Alignment a) { v_received.append(a); });

	buttons.at(k_align_center)->click();
	ASSERT_EQ(h_received.size(), 1);
	EXPECT_EQ(h_received.first(), Qt::AlignHCenter);

	buttons.at(k_align_justify)->click();
	ASSERT_EQ(h_received.size(), 2);
	EXPECT_EQ(h_received.last(), Qt::AlignJustify);

	buttons.at(k_align_middle)->click();
	ASSERT_EQ(v_received.size(), 1);
	EXPECT_EQ(v_received.first(), Qt::AlignVCenter);

	buttons.at(k_align_bottom)->click();
	ASSERT_EQ(v_received.size(), 2);
	EXPECT_EQ(v_received.last(), Qt::AlignBottom);
}

TEST(ViewerTextEditorToolBar, ToggleButtonsEmitTheirState)
{
	olive::ViewerTextEditorToolBar toolbar;
	const auto buttons = toolbar_buttons(&toolbar);
	ASSERT_EQ(buttons.size(), 11);

	QSignalSpy underline_spy(&toolbar,
							 &olive::ViewerTextEditorToolBar::underline_changed);
	QSignalSpy strike_spy(
		&toolbar, &olive::ViewerTextEditorToolBar::strikethrough_changed);
	QSignalSpy caps_spy(&toolbar,
						&olive::ViewerTextEditorToolBar::small_caps_changed);

	buttons.at(k_underline)->click();
	ASSERT_EQ(underline_spy.count(), 1);
	EXPECT_TRUE(underline_spy.first().first().toBool());

	buttons.at(k_strikethrough)->click();
	ASSERT_EQ(strike_spy.count(), 1);
	EXPECT_TRUE(strike_spy.first().first().toBool());

	buttons.at(k_small_caps)->click();
	ASSERT_EQ(caps_spy.count(), 1);
	EXPECT_TRUE(caps_spy.first().first().toBool());

	// Unchecking emits false
	buttons.at(k_underline)->click();
	ASSERT_EQ(underline_spy.count(), 2);
	EXPECT_FALSE(underline_spy.last().first().toBool());
}

TEST(ViewerTextEditorToolBar, SetColorPaintsColorButton)
{
	olive::ViewerTextEditorToolBar toolbar;
	const auto buttons = toolbar_buttons(&toolbar);
	ASSERT_EQ(buttons.size(), 11);

	const QColor c(QStringLiteral("#FF0000"));
	toolbar.set_color(c);

	QPushButton *color_btn = buttons.at(k_color);
	EXPECT_EQ(color_btn->property("color").value<QColor>(), c);
	EXPECT_TRUE(color_btn->styleSheet().contains(c.name()));
}

TEST(ViewerTextEditorToolBar, SetFontFamilyUpdatesComboWithoutEmitting)
{
	olive::ViewerTextEditorToolBar toolbar;

	QSignalSpy family_spy(&toolbar,
						  &olive::ViewerTextEditorToolBar::family_changed);

	// Pick a family from the combo's own model: QFontDatabase also lists
	// system fonts the QFontComboBox filters out (e.g. ".Apple Color Emoji"
	// on macOS), which setCurrentFont can only approximate
	auto *combo = toolbar.findChild<QFontComboBox *>();
	ASSERT_NE(combo, nullptr);
	ASSERT_GT(combo->count(), 1);
	const QString family = combo->itemText(combo->count() / 2);
	ASSERT_FALSE(family.isEmpty());

	toolbar.set_font_family(family);

	EXPECT_EQ(toolbar.get_font_family(), family);
	// set_font_family blocks the combo's signals while syncing
	EXPECT_EQ(family_spy.count(), 0);
}

TEST(ViewerTextEditorToolBar, FirstPaintEmittedOnce)
{
	olive::ViewerTextEditorToolBar toolbar;
	QSignalSpy spy(&toolbar, &olive::ViewerTextEditorToolBar::first_paint);

	// Deliver paint events directly: whether the offscreen QPA paints on
	// expose is platform-dependent, but QWidget::event always dispatches a
	// QPaintEvent to paintEvent
	QPaintEvent first(toolbar.rect());
	QApplication::sendEvent(&toolbar, &first);
	EXPECT_EQ(spy.count(), 1);

	// Later repaints don't re-emit
	QPaintEvent second(toolbar.rect());
	QApplication::sendEvent(&toolbar, &second);
	EXPECT_EQ(spy.count(), 1);
}

TEST(ViewerTextEditorToolBar, CloseEventIsIgnored)
{
	olive::ViewerTextEditorToolBar toolbar;

	QCloseEvent ev;
	QApplication::sendEvent(&toolbar, &ev);

	// The toolbar is a floating child of the viewer; closing must never hide it
	EXPECT_FALSE(ev.isAccepted());
}

TEST(ViewerTextEditorToolBar, LeftDragMovesToolbarWithinParent)
{
	QWidget parent;
	parent.resize(600, 400);

	olive::ViewerTextEditorToolBar toolbar(&parent);
	toolbar.move(100, 100);
	parent.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&parent));

	QTest::mousePress(&toolbar, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));

	// QTest::mouseMove can't report held buttons, so deliver the move manually
	QMouseEvent move(QEvent::MouseMove, QPointF(30, 25), QPointF(130, 125),
					 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(&toolbar, &move);

	EXPECT_EQ(toolbar.pos(), QPoint(120, 115));
}

TEST(ViewerTextEditor, CursorWidthScalesWithInverseZoom)
{
	olive::ViewerTextEditor at_full(1.0);
	EXPECT_EQ(at_full.cursorWidth(), 1);

	olive::ViewerTextEditor zoomed_out(0.5);
	EXPECT_EQ(zoomed_out.cursorWidth(), 2);

	olive::ViewerTextEditor far_out(0.25);
	EXPECT_EQ(far_out.cursorWidth(), 4);
}

TEST(ViewerTextEditor, ConstructionDefaults)
{
	olive::ViewerTextEditor editor(1.0);

	// Rich text paste is disabled; colors default to white on transparent
	EXPECT_FALSE(editor.acceptRichText());
	EXPECT_EQ(editor.palette().color(QPalette::Text), QColor(Qt::white));

	// Scrolling is handled by the viewer gizmo, not the text edit
	EXPECT_EQ(editor.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
	EXPECT_EQ(editor.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
}

TEST(ViewerTextEditor, ConnectToolBarSyncsCurrentFormat)
{
	olive::ViewerTextEditor editor(1.0);

	// Set a distinctive format before connecting; connect_tool_bar pushes the
	// editor's current format into the toolbar exactly once
	editor.setFontUnderline(true);
	editor.setAlignment(Qt::AlignRight);

	olive::ViewerTextEditorToolBar toolbar;
	editor.connect_tool_bar(&toolbar);

	const auto buttons = toolbar_buttons(&toolbar);
	ASSERT_EQ(buttons.size(), 11);
	EXPECT_TRUE(buttons.at(k_underline)->isChecked());
	EXPECT_TRUE(buttons.at(k_align_right)->isChecked());
	EXPECT_FALSE(buttons.at(k_align_left)->isChecked());
	EXPECT_FALSE(buttons.at(k_align_center)->isChecked());
}

TEST(ViewerTextEditor, ToolBarTogglesApplyToEditorFormat)
{
	olive::ViewerTextEditor editor(1.0);
	olive::ViewerTextEditorToolBar toolbar;
	editor.connect_tool_bar(&toolbar);

	const auto buttons = toolbar_buttons(&toolbar);
	ASSERT_EQ(buttons.size(), 11);

	buttons.at(k_underline)->click();
	EXPECT_TRUE(editor.currentCharFormat().fontUnderline());

	buttons.at(k_strikethrough)->click();
	EXPECT_TRUE(editor.currentCharFormat().fontStrikeOut());

	buttons.at(k_small_caps)->click();
	EXPECT_EQ(editor.currentCharFormat().fontCapitalization(),
			  QFont::SmallCaps);

	buttons.at(k_align_center)->click();
	EXPECT_EQ(editor.alignment(), Qt::AlignHCenter);

	// The toolbar follows the editor back (same alignment echo)
	EXPECT_TRUE(buttons.at(k_align_center)->isChecked());
	EXPECT_FALSE(buttons.at(k_align_left)->isChecked());
}

TEST(ViewerTextEditor, PaintNeverRendersTextByDesign)
{
	olive::ViewerTextEditor editor(1.0);
	editor.resize(200, 80);
	editor.setPlainText(QStringLiteral("Hello"));

	// document_changed() swaps in a clone whose text is fully transparent (the
	// HACK in viewertexteditor.cpp: the gizmo underneath renders the text, the
	// editor must not render it a second time). Verify that contract: nothing
	// is painted, with or without a selection, at any vertical alignment.
	QTextCursor c = editor.textCursor();
	c.select(QTextCursor::Document);
	editor.setTextCursor(c);

	for (const Qt::Alignment valign :
		 { Qt::AlignTop, Qt::AlignVCenter, Qt::AlignBottom }) {
		QImage img(200, 80, QImage::Format_ARGB32);
		img.fill(Qt::transparent);
		{
			QPainter p(&img);
			editor.paint(&p, valign);
			EXPECT_TRUE(p.isActive());
		}
		for (int y = 0; y < img.height(); y++) {
			for (int x = 0; x < img.width(); x++) {
				EXPECT_EQ(qAlpha(img.pixel(x, y)), 0)
					<< "painted pixel at " << x << "," << y;
			}
		}
	}
}
