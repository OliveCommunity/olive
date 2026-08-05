#include <gtest/gtest.h>

#include <QDateTime>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

#include "node/block/clip/clip.h"
#include "node/color/colormanager/colormanager.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelinemarker.h"
#include "widget/playbackcontrols/dragbutton.h"
#include "widget/taskview/elapsedcounterwidget.h"
#include "widget/timelinewidget/cliphandle.h"
#include "widget/timelinewidget/trackhandle.h"
#include "widget/timeruler/markerhandle.h"
#include "widget/timeruler/markerpainting.h"
#include "oakutil/qtutils.h"

TEST(WidgetDragButton, DefaultsAndPlainClick)
{
	olive::DragButton btn;
	btn.resize(80, 30);
	EXPECT_EQ(btn.cursor().shape(), Qt::OpenHandCursor);

	QSignalSpy drag_spy(&btn, &olive::DragButton::drag_started);
	QSignalSpy click_spy(&btn, &QPushButton::clicked);

	// A plain click is still a normal button click, not a drag
	QTest::mouseClick(&btn, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
	EXPECT_EQ(drag_spy.count(), 0);
	EXPECT_EQ(click_spy.count(), 1);
}

TEST(WidgetDragButton, DragEmitsDragStartedOncePerPress)
{
	olive::DragButton btn;
	btn.resize(80, 30);

	QSignalSpy drag_spy(&btn, &olive::DragButton::drag_started);

	// Press then move: the first move with a button held starts the drag
	QTest::mousePress(&btn, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
	QTest::mouseMove(&btn, QPoint(20, 15));
	ASSERT_EQ(drag_spy.count(), 1);

	// The drag state latches until release
	QTest::mouseMove(&btn, QPoint(30, 20));
	EXPECT_EQ(drag_spy.count(), 1);
	QTest::mouseRelease(&btn, Qt::LeftButton, Qt::NoModifier, QPoint(30, 20));

	// A new press re-arms the drag detection
	QTest::mousePress(&btn, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
	QTest::mouseMove(&btn, QPoint(25, 15));
	EXPECT_EQ(drag_spy.count(), 2);
	QTest::mouseRelease(&btn, Qt::LeftButton, Qt::NoModifier, QPoint(25, 15));
}

TEST(WidgetDragButton, MoveWithoutButtonsDoesNotEmit)
{
	olive::DragButton btn;
	btn.resize(80, 30);

	QSignalSpy drag_spy(&btn, &olive::DragButton::drag_started);

	QTest::mouseMove(&btn, QPoint(40, 15));
	EXPECT_EQ(drag_spy.count(), 0);
}

TEST(WidgetElapsedCounter, InitialLabelsAreZero)
{
	olive::ElapsedCounterWidget w;
	const auto labels = w.findChildren<QLabel *>();
	ASSERT_EQ(labels.size(), 2);

	EXPECT_EQ(labels.at(0)->text(), QStringLiteral("Elapsed: 00:00:00"));
	EXPECT_EQ(labels.at(1)->text(), QStringLiteral("Remaining: 00:00:00"));
}

TEST(WidgetElapsedCounter, HalfProgressMakesElapsedEqualRemaining)
{
	olive::ElapsedCounterWidget w;
	const auto labels = w.findChildren<QLabel *>();
	ASSERT_EQ(labels.size(), 2);

	// At 50% progress, remaining time is derived from the same elapsed
	// value, so both labels must agree regardless of wall-clock drift
	w.start(QDateTime::currentMSecsSinceEpoch() - 60000);
	w.set_progress(0.5);

	const QString elapsed = labels.at(0)->text();
	const QString remaining = labels.at(1)->text();
	EXPECT_NE(elapsed, QStringLiteral("Elapsed: 00:00:00"));
	EXPECT_EQ(elapsed, QStringLiteral("Elapsed: ") +
							remaining.mid(QStringLiteral("Remaining: ").size()));

	// A full progress report leaves nothing remaining
	w.set_progress(1.0);
	EXPECT_EQ(labels.at(1)->text(), QStringLiteral("Remaining: 00:00:00"));

	w.stop();
}

TEST(WidgetElapsedCounter, ZeroProgressResetsLabels)
{
	olive::ElapsedCounterWidget w;
	const auto labels = w.findChildren<QLabel *>();
	ASSERT_EQ(labels.size(), 2);

	w.start(QDateTime::currentMSecsSinceEpoch() - 60000);
	w.set_progress(0.5);
	EXPECT_NE(labels.at(0)->text(), QStringLiteral("Elapsed: 00:00:00"));

	// No progress means no estimate at all
	w.set_progress(0.0);
	EXPECT_EQ(labels.at(0)->text(), QStringLiteral("Elapsed: 00:00:00"));
	EXPECT_EQ(labels.at(1)->text(), QStringLiteral("Remaining: 00:00:00"));

	w.stop();
}

TEST(WidgetClipHandle, EmptyClipDefaults)
{
	OakEngineBlock *clip = olive::clip_create_empty("Test Clip");
	ASSERT_NE(clip, nullptr);

	EXPECT_EQ(olive::cliphandle(clip),
			  reinterpret_cast<OakEngineClip *>(clip));
	EXPECT_DOUBLE_EQ(olive::clip_speed(clip), 1.0);
	EXPECT_EQ(olive::clip_loop_mode(clip), 0);
	EXPECT_FALSE(olive::clip_is_reversed(clip));
	EXPECT_FALSE(olive::clip_maintain_audio_pitch(clip));
	EXPECT_FALSE(olive::clip_is_autocaching(clip));

	// Nothing is connected to the buffer input and the clip is on no track
	EXPECT_EQ(olive::clip_connected_node(clip), nullptr);
	EXPECT_EQ(olive::clip_thumbnails(clip), nullptr);
	EXPECT_EQ(olive::clip_waveform(clip), nullptr);
	EXPECT_EQ(olive::clip_connected_video_cache(clip), nullptr);
	EXPECT_EQ(olive::block_track_handle(clip), nullptr);

	EXPECT_EQ(olive::clip_media_in(clip), olive::Rational(0));
	const olive::TimeRange range = olive::clip_media_range(clip);
	EXPECT_EQ(range.in(), olive::Rational(0));
	EXPECT_EQ(range.out(), olive::Rational(0));

	// Invalidation requests with nothing connected are a safe no-op
	olive::clip_request_invalidate_connected(clip);

	delete reinterpret_cast<olive::ClipBlock *>(clip);
}

TEST(WidgetClipHandle, SetMediaInRoundTripsThroughFacade)
{
	OakEngineBlock *clip = olive::clip_create_empty(nullptr);
	ASSERT_NE(clip, nullptr);

	olive::clip_set_media_in(clip, olive::Rational(2, 1));
	EXPECT_EQ(olive::clip_media_in(clip), olive::Rational(2, 1));

	// Media out is media in plus the clip's (zero) length
	const olive::TimeRange range = olive::clip_media_range(clip);
	EXPECT_EQ(range.in(), olive::Rational(2, 1));
	EXPECT_EQ(range.out(), olive::Rational(2, 1));

	delete reinterpret_cast<olive::ClipBlock *>(clip);
}

TEST(WidgetClipHandle, NullClipIsTolerated)
{
	olive::clip_set_media_in(nullptr, olive::Rational(1, 1));
	olive::clip_request_invalidate_connected(nullptr);
	olive::clip_request_invalidate_connected(
		nullptr, true,
		olive::TimeRange(olive::Rational(0, 1), olive::Rational(1, 1)));
	SUCCEED();
}

TEST(WidgetTrackHandle, NullTrackIsTolerated)
{
	EXPECT_EQ(olive::trackhandle(nullptr), nullptr);
	EXPECT_EQ(olive::track_sequence_handle(nullptr), nullptr);
	EXPECT_EQ(olive::track_type_of(nullptr), -1);
	EXPECT_EQ(olive::track_index_of(nullptr), -1);
	EXPECT_FALSE(olive::track_is_locked(nullptr));
	EXPECT_FALSE(olive::track_is_muted(nullptr));
}

TEST(WidgetTrackHandle, ConnectedTrackReportsSequenceIndexAndFlags)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *sequence = new olive::Sequence();
	sequence->setParent(&project);
	sequence->add_default_nodes();

	olive::TrackList *video = sequence->track_list(olive::Track::k_video);
	olive::TrackList *audio = sequence->track_list(olive::Track::k_audio);
	ASSERT_EQ(video->get_track_count(), 1);
	ASSERT_EQ(audio->get_track_count(), 1);

	olive::Track *video_track = video->get_track_at(0);
	auto *h = reinterpret_cast<OakEngineTrack *>(video_track);

	EXPECT_EQ(olive::trackhandle(h), h);
	EXPECT_EQ(olive::track_type_of(h), OAKENGINE_TRACK_TYPE_VIDEO);
	EXPECT_EQ(olive::track_index_of(h), 0);
	EXPECT_EQ(olive::track_sequence_handle(h),
			  reinterpret_cast<OakEngineSequence *>(sequence));

	EXPECT_FALSE(olive::track_is_locked(h));
	EXPECT_FALSE(olive::track_is_muted(h));

	video_track->set_locked(true);
	EXPECT_TRUE(olive::track_is_locked(h));

	video_track->set_muted(true);
	EXPECT_TRUE(olive::track_is_muted(h));

	// Lock/mute are per-track; the audio track is unaffected
	auto *ah = reinterpret_cast<OakEngineTrack *>(audio->get_track_at(0));
	EXPECT_EQ(olive::track_type_of(ah), OAKENGINE_TRACK_TYPE_AUDIO);
	EXPECT_FALSE(olive::track_is_locked(ah));
	EXPECT_FALSE(olive::track_is_muted(ah));
}

TEST(WidgetMarkerHandle, DetachedMarkerAccessors)
{
	OakEngineMarker *m = oakengine_marker_create(3, 5, 1, 8, 1, "Intro");
	ASSERT_NE(m, nullptr);

	const olive::TimeRange t = olive::marker_time(m);
	EXPECT_EQ(t.in(), olive::Rational(5, 1));
	EXPECT_EQ(t.out(), olive::Rational(8, 1));
	EXPECT_EQ(olive::marker_name(m), QStringLiteral("Intro"));
	EXPECT_EQ(olive::marker_color(m), 3);

	// ADL customization points used by the selection manager
	EXPECT_EQ(olive::selection_time(m), olive::Rational(5, 1));
	EXPECT_EQ(olive::selection_time_end(m), olive::Rational(8, 1));
	EXPECT_EQ(olive::selection_time_target_parent(m), nullptr);

	oakengine_marker_free(m);
}

TEST(WidgetMarkerHandle, SetTimeLiveUpdatesRange)
{
	OakEngineMarker *m = oakengine_marker_create(0, 5, 1, 8, 1, "M");
	ASSERT_NE(m, nullptr);

	olive::marker_set_time_live(
		m, olive::TimeRange(olive::Rational(1, 1), olive::Rational(2, 1)));

	const olive::TimeRange t = olive::marker_time(m);
	EXPECT_EQ(t.in(), olive::Rational(1, 1));
	EXPECT_EQ(t.out(), olive::Rational(2, 1));

	oakengine_marker_free(m);
}

TEST(WidgetMarkerHandle, SelectionSetTimePreservesLength)
{
	OakEngineMarker *m = oakengine_marker_create(0, 5, 1, 8, 1, nullptr);
	ASSERT_NE(m, nullptr);

	// Moving the in-point drags the out-point along, keeping a length of 3
	olive::selection_set_time(m, olive::Rational(10, 1));

	const olive::TimeRange t = olive::marker_time(m);
	EXPECT_EQ(t.in(), olive::Rational(10, 1));
	EXPECT_EQ(t.out(), olive::Rational(13, 1));

	oakengine_marker_free(m);
}

TEST(WidgetMarkerHandle, SiblingDetectionWithinList)
{
	// The list asserts that in-points are unique, so use distinct times
	olive::TimelineMarkerList list;
	olive::TimelineMarker a(
		1,
		olive::core::TimeRange(olive::core::Rational(5, 1),
							   olive::core::Rational(5, 1)),
		QStringLiteral("A"), &list);
	olive::TimelineMarker b(
		2,
		olive::core::TimeRange(olive::core::Rational(7, 1),
							   olive::core::Rational(7, 1)),
		QStringLiteral("B"), &list);
	olive::TimelineMarker c(
		3,
		olive::core::TimeRange(olive::core::Rational(9, 1),
							   olive::core::Rational(9, 1)),
		QStringLiteral("C"), &list);
	ASSERT_EQ(list.size(), 3);

	auto *hc = reinterpret_cast<OakEngineMarker *>(&c);

	// Another marker sits at t=5, so c has a sibling there
	EXPECT_TRUE(olive::marker_has_sibling_at_time(hc, olive::Rational(5, 1)));

	// c is the only marker at t=9; a marker is not its own sibling
	EXPECT_FALSE(olive::marker_has_sibling_at_time(hc, olive::Rational(9, 1)));

	// No marker at all at t=11
	EXPECT_FALSE(olive::marker_has_sibling_at_time(hc, olive::Rational(11, 1)));
}

TEST(WidgetMarkerPainting, HeightMatchesFontMetrics)
{
	QImage img(200, 100, QImage::Format_ARGB32);
	img.fill(Qt::transparent);
	QPainter p(&img);

	EXPECT_EQ(olive::MarkerPainting::height(p.fontMetrics()),
			  p.fontMetrics().height());
}

TEST(WidgetMarkerPainting, PointMarkerGeometryAndFill)
{
	QImage img(200, 100, QImage::Format_ARGB32);
	img.fill(Qt::transparent);
	QPainter p(&img);

	const QFontMetrics fm = p.fontMetrics();
	const int h = olive::MarkerPainting::height(fm);
	const int w = olive::QtUtils::q_font_metrics_width(fm, QStringLiteral("H"));

	const QPoint pt(100, 90);
	const QRect r = olive::MarkerPainting::draw(&p, pt, -1, 10.0, false,
												QString(), 0,
												olive::Rational(2, 1),
												olive::Rational(2, 1));
	p.end();

	// A point marker (in == out) is centered horizontally on the anchor and
	// sits one marker-height above it
	EXPECT_EQ(r, QRect(pt.x() - w / 2, pt.y() - h, w, h));

	// The polygon body is filled around its center
	const QColor px = img.pixelColor(QPoint(pt.x(), pt.y() - h / 2));
	EXPECT_GT(px.alpha(), 0);
}

TEST(WidgetMarkerPainting, RangedMarkerGeometryScalesWithRange)
{
	QImage img(400, 100, QImage::Format_ARGB32);
	img.fill(Qt::transparent);
	QPainter p(&img);

	const QFontMetrics fm = p.fontMetrics();
	const int h = olive::MarkerPainting::height(fm);

	const QPoint pt(50, 90);
	const QRect r = olive::MarkerPainting::draw(&p, pt, -1, 10.0, false,
												QStringLiteral("Span"), 0,
												olive::Rational(2, 1),
												olive::Rational(5, 1));
	p.end();

	// A ranged marker starts at the anchor and extends (out - in) * scale px
	EXPECT_EQ(r, QRect(pt.x(), pt.y() - h, 30, h));

	// The rect body is filled
	const QColor px = img.pixelColor(QPoint(pt.x() + 2, pt.y() - h / 2));
	EXPECT_GT(px.alpha(), 0);
}

TEST(WidgetMarkerPainting, SelectionAndLabelDoNotAffectGeometry)
{
	QImage img(400, 100, QImage::Format_ARGB32);
	img.fill(Qt::transparent);
	QPainter p(&img);

	const QPoint pt(100, 90);

	const QRect plain = olive::MarkerPainting::draw(
		&p, pt, -1, 10.0, false, QString(), 0, olive::Rational(2, 1),
		olive::Rational(2, 1));

	// Selected + a label to the right of the marker
	const QRect selected = olive::MarkerPainting::draw(
		&p, pt, 300, 10.0, true, QStringLiteral("Named"), 0,
		olive::Rational(2, 1), olive::Rational(2, 1));
	p.end();

	EXPECT_EQ(plain, selected);
}
