#include <gtest/gtest.h>

#include <QFontMetrics>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>

#include "core.h"
#include "node/output/viewer/viewer.h"
#include "olive/core/util/timecodefunctions.h"
#include "render/diskmanager.h"
#include "timeline/timelinemarker.h"
#include "widget/playbackcontrols/playbackcontrols.h"
#include "widget/slider/base/sliderbase.h"
#include "widget/slider/base/sliderlabel.h"
#include "widget/slider/rationalslider.h"
#include "widget/timeruler/timeruler.h"

using namespace olive;

namespace
{

// PlaybackControls and playhead seeking talk to the Core singleton
void ensure_app_singletons()
{
	if (!olive::Core::instance()) {
		new olive::Core(olive::Core::CoreParams()); // intentionally leaked
	}
	if (!olive::DiskManager::instance()) {
		olive::DiskManager::create_instance();
	}
}

} // namespace

TEST(TimeRuler, ConstructionWithAndWithoutDecorations)
{
	ensure_app_singletons();

	// Text shown, cache status hidden
	TimeRuler plain;
	EXPECT_EQ(plain.get_markers(), nullptr);
	EXPECT_EQ(plain.get_work_area(), nullptr);

	// Text hidden, cache status shown
	TimeRuler decorated(false, true);

	// The height is fixed and derived from the enabled decorations: base
	// text height plus marker height always, another text height when text
	// is visible, and the cache indicator height when cache status is shown
	const QFontMetrics fm = plain.fontMetrics();
	const int marker_h = TimelineMarker::get_marker_height(fm);

	EXPECT_EQ(plain.minimumHeight(), 2 * fm.height() + marker_h);
	EXPECT_EQ(plain.maximumHeight(), plain.minimumHeight());

	EXPECT_EQ(decorated.minimumHeight(),
			  fm.height() + PlaybackCache::get_cache_indicator_height() + marker_h);
	EXPECT_EQ(decorated.maximumHeight(), decorated.minimumHeight());

	// Centered text only affects painting, not geometry
	decorated.set_centered_text(true);
	EXPECT_EQ(decorated.minimumHeight(),
			  fm.height() + PlaybackCache::get_cache_indicator_height() + marker_h);
}

TEST(TimeRuler, TimebaseAndScaleDriveTimePixelConversion)
{
	TimeRuler ruler;

	// Without a timebase everything collapses to zero
	EXPECT_DOUBLE_EQ(ruler.time_to_scene(Rational(1)), 0.0);

	ruler.set_timebase(Rational(1, 30));
	ruler.set_scale(100.0);

	// One second at scale 100 lands at scene x=100, half a second at 50
	EXPECT_DOUBLE_EQ(ruler.time_to_scene(Rational(1)), 100.0);
	EXPECT_DOUBLE_EQ(ruler.time_to_scene(Rational(1, 2)), 50.0);

	// Inverse conversion returns whole frames in the ruler's timebase
	EXPECT_EQ(ruler.scene_to_time(100.0), Rational(1));
	EXPECT_EQ(ruler.scene_to_time(50.0), Rational(1, 2));

	// Fractional positions floor to the frame below (or ceil when negative)
	EXPECT_EQ(ruler.scene_to_time(51.0), Rational(1, 2));
	EXPECT_EQ(ruler.scene_to_time(-51.0), Rational(-1, 2));

	// Rounding mode snaps to the nearest frame instead
	EXPECT_EQ(ruler.scene_to_time(51.0, true), Rational(1, 2));
	EXPECT_EQ(ruler.scene_to_time(80.0, true), Rational(4, 5));
}

TEST(TimeRuler, SeekToScenePointSeeksConnectedViewer)
{
	ensure_app_singletons();

	TimeRuler ruler;
	ruler.set_timebase(Rational(1, 30));
	ruler.set_scale(100.0);

	ViewerOutput viewer;
	ruler.set_viewer_node(&viewer);

	ruler.seek_to_scene_point(150.0);
	EXPECT_EQ(viewer.get_playhead(), Rational(3, 2));

	// Positions before zero clamp to zero
	viewer.set_playhead(Rational(5));
	ruler.seek_to_scene_point(-50.0);
	EXPECT_EQ(viewer.get_playhead(), Rational(0));
}

TEST(TimeRuler, SeekToScenePointWithoutTimebaseIsNoOp)
{
	// No timebase and no viewer: must return before touching either
	TimeRuler ruler;
	ruler.seek_to_scene_point(150.0);
	SUCCEED();
}

TEST(TimeRuler, SeekToScenePointWithoutViewerIsNoOp)
{
	ensure_app_singletons();

	// Timebase set but no viewer connected: previously dereferenced a null
	// GetViewerNode() and crashed.
	TimeRuler ruler;
	ruler.set_timebase(Rational(1, 30));
	ruler.set_scale(100.0);

	ruler.seek_to_scene_point(150.0);
	SUCCEED();
}

class PlaybackControlsTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ensure_app_singletons();
	}

	// The play/pause stacked widget (SliderBase is also a QStackedWidget
	// and must be filtered out)
	static QStackedWidget *play_pause_stack(PlaybackControls *controls)
	{
		foreach (QStackedWidget *s, controls->findChildren<QStackedWidget *>()) {
			if (!qobject_cast<SliderBase *>(s)) {
				return s;
			}
		}
		return nullptr;
	}

	// The play/pause buttons live inside the play/pause stacked widget
	static void play_pause_buttons(PlaybackControls *controls,
								 QPushButton **play_btn, QPushButton **pause_btn)
	{
		QStackedWidget *stack = play_pause_stack(controls);
		*play_btn = qobject_cast<QPushButton *>(stack->widget(0));
		*pause_btn = qobject_cast<QPushButton *>(stack->widget(1));
	}

	// The remaining buttons in creation order: go-to-start, previous frame,
	// next frame, go-to-end, video drag, audio drag
	static QList<QPushButton *> navigation_buttons(PlaybackControls *controls)
	{
		QPushButton *play_btn;
		QPushButton *pause_btn;
		play_pause_buttons(controls, &play_btn, &pause_btn);

		QList<QPushButton *> buttons;
		foreach (QPushButton *b, controls->findChildren<QPushButton *>()) {
			if (b != play_btn && b != pause_btn) {
				buttons.append(b);
			}
		}
		return buttons;
	}
};

TEST_F(PlaybackControlsTest, NullTimebaseDisablesWidget)
{
	PlaybackControls controls;
	EXPECT_FALSE(controls.isEnabled());

	controls.set_timebase(Rational(1, 30));
	EXPECT_TRUE(controls.isEnabled());

	controls.set_timebase(Rational());
	EXPECT_FALSE(controls.isEnabled());
}

TEST_F(PlaybackControlsTest, ButtonsEmitCorrespondingSignals)
{
	PlaybackControls controls;
	controls.set_timebase(Rational(1, 30));

	QPushButton *play_btn;
	QPushButton *pause_btn;
	play_pause_buttons(&controls, &play_btn, &pause_btn);
	ASSERT_NE(play_btn, nullptr);
	ASSERT_NE(pause_btn, nullptr);

	const QList<QPushButton *> buttons = navigation_buttons(&controls);
	ASSERT_EQ(buttons.size(), 6);

	QSignalSpy begin_spy(&controls, &PlaybackControls::begin_clicked);
	QSignalSpy prev_spy(&controls, &PlaybackControls::prev_frame_clicked);
	QSignalSpy play_spy(&controls, &PlaybackControls::play_clicked);
	QSignalSpy pause_spy(&controls, &PlaybackControls::pause_clicked);
	QSignalSpy next_spy(&controls, &PlaybackControls::next_frame_clicked);
	QSignalSpy end_spy(&controls, &PlaybackControls::end_clicked);
	QSignalSpy video_spy(&controls, &PlaybackControls::video_clicked);
	QSignalSpy audio_spy(&controls, &PlaybackControls::audio_clicked);

	buttons.at(0)->click();
	EXPECT_EQ(begin_spy.count(), 1);

	buttons.at(1)->click();
	EXPECT_EQ(prev_spy.count(), 1);

	play_btn->click();
	EXPECT_EQ(play_spy.count(), 1);

	pause_btn->click();
	EXPECT_EQ(pause_spy.count(), 1);

	buttons.at(2)->click();
	EXPECT_EQ(next_spy.count(), 1);

	buttons.at(3)->click();
	EXPECT_EQ(end_spy.count(), 1);

	buttons.at(4)->click();
	EXPECT_EQ(video_spy.count(), 1);

	buttons.at(5)->click();
	EXPECT_EQ(audio_spy.count(), 1);
}

TEST_F(PlaybackControlsTest, SetTimeUpdatesCurrentTimecodeWithoutEmitting)
{
	PlaybackControls controls;
	controls.set_timebase(Rational(1, 30));

	auto *slider = controls.findChild<RationalSlider *>();
	ASSERT_NE(slider, nullptr);

	QSignalSpy time_spy(&controls, &PlaybackControls::time_changed);

	controls.set_time(Rational(3, 2));
	EXPECT_EQ(slider->get_value(), Rational(3, 2));

	// Programmatic updates must not feed back into TimeChanged
	EXPECT_EQ(time_spy.count(), 0);
}

TEST_F(PlaybackControlsTest, SetEndTimeFormatsEndTimecodeLabel)
{
	PlaybackControls controls;
	controls.set_timebase(Rational(1, 30));

	// The only plain QLabel is the end timecode; the current-time slider
	// uses a SliderLabel (a QLabel subclass) internally
	QLabel *end_label = nullptr;
	foreach (QLabel *l, controls.findChildren<QLabel *>()) {
		if (!qobject_cast<SliderLabel *>(l)) {
			end_label = l;
			break;
		}
	}
	ASSERT_NE(end_label, nullptr);

	// Pin the display mode so the expected strings don't depend on whatever
	// the config happens to hold
	const core::Timecode::Display saved_display =
		Core::instance()->get_timecode_display();
	Core::instance()->set_timecode_display(core::Timecode::k_timecode_non_drop_frame);

	// 30 seconds at 30 fps is frame 900 = 30 seconds + 0 frames
	controls.set_end_time(Rational(30));
	EXPECT_EQ(end_label->text(), QStringLiteral("00:00:30:00"));

	// 1.5 seconds at 30 fps is frame 45 = 1 second + 15 frames
	controls.set_end_time(Rational(3, 2));
	EXPECT_EQ(end_label->text(), QStringLiteral("00:00:01:15"));

	Core::instance()->set_timecode_display(saved_display);
}

TEST_F(PlaybackControlsTest, PlayPauseStackSwitchesVisibleButton)
{
	PlaybackControls controls;

	QStackedWidget *stack = play_pause_stack(&controls);
	ASSERT_NE(stack, nullptr);

	QPushButton *play_btn;
	QPushButton *pause_btn;
	play_pause_buttons(&controls, &play_btn, &pause_btn);
	ASSERT_NE(play_btn, nullptr);
	ASSERT_NE(pause_btn, nullptr);

	// The play button is the default page
	EXPECT_EQ(stack->currentWidget(), play_btn);

	controls.show_pause_button();
	EXPECT_EQ(stack->currentWidget(), pause_btn);

	controls.show_play_button();
	EXPECT_EQ(stack->currentWidget(), play_btn);
}

TEST_F(PlaybackControlsTest, AudioVideoDragButtonsToggleVisibility)
{
	PlaybackControls controls;
	const QList<QPushButton *> buttons = navigation_buttons(&controls);
	ASSERT_EQ(buttons.size(), 6);

	// Hidden by default (constructor passes false)
	EXPECT_TRUE(buttons.at(4)->isHidden());
	EXPECT_TRUE(buttons.at(5)->isHidden());

	controls.set_audio_video_drag_buttons_visible(true);
	EXPECT_FALSE(buttons.at(4)->isHidden());
	EXPECT_FALSE(buttons.at(5)->isHidden());
}
