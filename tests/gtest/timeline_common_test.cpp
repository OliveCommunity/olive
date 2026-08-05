#include <gtest/gtest.h>

#include <QColor>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>

#include "core.h"
#include "node/color/colormanager/colormanager.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "oakengine/node.h"
#include "oakengine/timeline.h"
#include "timeline/timelinecommonapp.h"
#include "widget/clickablelabel/clickablelabel.h"
#include "widget/focusablelineedit/focusablelineedit.h"
#include "widget/timelinewidget/timelineandtrackview.h"
#include "widget/timelinewidget/trackview/trackview.h"
#include "widget/timelinewidget/trackview/trackviewitem.h"
#include "widget/timelinewidget/trackview/trackviewsplitter.h"
#include "widget/timelinewidget/view/timelineview.h"

namespace
{

// HandMovableView (base of TimelineView) connects to Core::instance() at
// construction, so anything embedding a TimelineView needs the singleton
void ensure_core()
{
	if (!olive::Core::instance()) {
		new olive::Core(); // intentionally leaked
	}
}

olive::Sequence *create_sequence(olive::Project *project)
{
	auto *sequence = new olive::Sequence();
	sequence->setParent(project);
	return sequence;
}

// Mirrors sequence_test.cpp: grow the track input array, then connect the
// edge so the TrackList assigns type, index and owning sequence
olive::Track *append_track(olive::Project *project, olive::Sequence *sequence,
						   olive::Track::Type type)
{
	auto *track = new olive::Track();
	track->setParent(project);
	olive::TrackList *list = sequence->track_list(type);
	list->array_append();
	olive::Node::connect_edge(track, list->track_input(list->array_size() - 1));
	return track;
}

OakEngineSequence *seq_handle(olive::Sequence *sequence)
{
	return reinterpret_cast<OakEngineSequence *>(sequence);
}

OakEngineNode *node_handle(olive::Node *node)
{
	return reinterpret_cast<OakEngineNode *>(node);
}

OakEngineTrack *track_handle(olive::Track *track)
{
	return reinterpret_cast<OakEngineTrack *>(track);
}

// TrackViewItem's mute/lock buttons are distinguished only by the checked
// color baked into their style sheets (red for mute, gray for lock)
QPushButton *find_button_by_checked_color(QWidget *parent, const QString &rgb)
{
	for (QPushButton *b : parent->findChildren<QPushButton *>()) {
		if (b->styleSheet().contains(rgb)) {
			return b;
		}
	}
	return nullptr;
}

} // namespace

TEST(TimelineApp, MovementModeOrdinalsMatchEngineAbi)
{
	// The C ABI transports these as ints (OAKENGINE_MOVEMENT_MODE_*), so the
	// app-side enum ordinals must stay in sync with the engine
	EXPECT_EQ(int(olive::TimelineApp::k_none), OAKENGINE_MOVEMENT_MODE_NONE);
	EXPECT_EQ(int(olive::TimelineApp::k_move), OAKENGINE_MOVEMENT_MODE_MOVE);
	EXPECT_EQ(int(olive::TimelineApp::k_trim_in), OAKENGINE_MOVEMENT_MODE_TRIM_IN);
	EXPECT_EQ(int(olive::TimelineApp::k_trim_out),
			  OAKENGINE_MOVEMENT_MODE_TRIM_OUT);
}

TEST(TimelineApp, IsATrimMode)
{
	EXPECT_TRUE(olive::TimelineApp::is_a_trim_mode(olive::TimelineApp::k_trim_in));
	EXPECT_TRUE(
		olive::TimelineApp::is_a_trim_mode(olive::TimelineApp::k_trim_out));
	EXPECT_FALSE(olive::TimelineApp::is_a_trim_mode(olive::TimelineApp::k_none));
	EXPECT_FALSE(olive::TimelineApp::is_a_trim_mode(olive::TimelineApp::k_move));
}

TEST(TimelineApp, ThumbnailAndWaveformOrdinals)
{
	EXPECT_EQ(int(olive::TimelineApp::k_thumbnail_off), 0);
	EXPECT_EQ(int(olive::TimelineApp::k_thumbnail_in_out), 1);
	EXPECT_EQ(int(olive::TimelineApp::k_thumbnail_on), 2);

	EXPECT_EQ(int(olive::TimelineApp::k_waveforms_disabled), 0);
	EXPECT_EQ(int(olive::TimelineApp::k_waveforms_enabled), 1);
}

TEST(TimelineApp, EditToInfoIsPlainStruct)
{
	olive::TimelineApp::EditToInfo info;
	info.track = nullptr;
	info.nearest_block = nullptr;
	info.nearest_time = olive::core::Rational(5, 1);

	EXPECT_EQ(info.track, nullptr);
	EXPECT_EQ(info.nearest_block, nullptr);
	EXPECT_EQ(info.nearest_time, olive::core::Rational(5, 1));

	olive::TimelineApp::EditToInfo copy = info;
	EXPECT_EQ(copy.nearest_time, olive::core::Rational(5, 1));
}

TEST(TimelineAndTrackView, ConstructionWiresSplitterAndViews)
{
	ensure_core();

	olive::TimelineAndTrackView tav;
	ASSERT_NE(tav.splitter(), nullptr);
	ASSERT_NE(tav.view(), nullptr);
	ASSERT_NE(tav.track_view(), nullptr);

	EXPECT_EQ(tav.splitter()->orientation(), Qt::Horizontal);
	EXPECT_FALSE(tav.splitter()->childrenCollapsible());
	ASSERT_EQ(tav.splitter()->count(), 2);

	// Track headers on the left, timeline view on the right
	EXPECT_EQ(tav.splitter()->widget(0), tav.track_view());
	EXPECT_EQ(tav.splitter()->widget(1), tav.view());
}

TEST(TimelineAndTrackView, ScrollbarsTrackEachOther)
{
	ensure_core();

	olive::TimelineAndTrackView tav;
	QScrollBar *view_sb = tav.view()->verticalScrollBar();
	QScrollBar *track_sb = tav.track_view()->verticalScrollBar();
	ASSERT_NE(view_sb, nullptr);
	ASSERT_NE(track_sb, nullptr);

	// With an empty sequence both scrollbars have a degenerate range; give
	// them explicit ranges so the sync math is observable. The ranges stick
	// because nothing relayouts the hidden widget afterwards
	view_sb->setRange(10, 100);
	track_sb->setRange(0, 90);

	// Scrolling the view scrolls the track headers by (value - minimum)
	view_sb->setValue(50);
	EXPECT_EQ(track_sb->value(), 40);

	// Scrolling the track headers scrolls the view by (minimum + value)
	track_sb->setValue(25);
	EXPECT_EQ(view_sb->value(), 35);

	// Deliberately no cleanup of the sync connections: leaving the scrollbars
	// at non-zero values exercises ~TimelineAndTrackView, which must detach
	// them before the child views reset their scenes during teardown
}

TEST(TrackView, ConstructionDefaults)
{
	olive::TrackView view;
	EXPECT_EQ(view.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
	EXPECT_EQ(view.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
	EXPECT_TRUE(view.widgetResizable());
	EXPECT_EQ(view.alignment(), Qt::Alignment(Qt::AlignLeft | Qt::AlignTop));

	auto *splitter = view.findChild<olive::TrackViewSplitter *>();
	ASSERT_NE(splitter, nullptr);
	EXPECT_EQ(splitter->orientation(), Qt::Vertical);
	// Only the trailing spacer widget before any track list is connected
	EXPECT_EQ(splitter->count(), 1);

	olive::TrackView bottom(Qt::AlignBottom);
	EXPECT_EQ(bottom.alignment(), Qt::Alignment(Qt::AlignLeft | Qt::AlignBottom));
}

TEST(TrackView, ConnectTrackListPopulatesAndDisconnectClears)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);
	append_track(&project, sequence, olive::Track::k_video);
	append_track(&project, sequence, olive::Track::k_video);
	append_track(&project, sequence, olive::Track::k_audio);

	olive::TrackView view;
	auto *splitter = view.findChild<olive::TrackViewSplitter *>();
	ASSERT_NE(splitter, nullptr);
	EXPECT_EQ(splitter->count(), 1);

	// Binding the video track list adds one item per track (plus the spacer)
	view.connect_track_list(seq_handle(sequence), OAKENGINE_TRACK_TYPE_VIDEO);
	EXPECT_EQ(splitter->count(), 3);

	// Rebinding to the audio list swaps the items, it doesn't accumulate
	view.connect_track_list(seq_handle(sequence), OAKENGINE_TRACK_TYPE_AUDIO);
	EXPECT_EQ(splitter->count(), 2);

	view.disconnect_track_list();
	EXPECT_EQ(splitter->count(), 1);
}

TEST(TrackView, InsertAndRemoveTrackUpdateSplitter)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);
	append_track(&project, sequence, olive::Track::k_video);

	olive::TrackView view;
	auto *splitter = view.findChild<olive::TrackViewSplitter *>();
	ASSERT_NE(splitter, nullptr);

	view.connect_track_list(seq_handle(sequence), OAKENGINE_TRACK_TYPE_VIDEO);
	ASSERT_EQ(splitter->count(), 2);

	// A track added to the sequence afterwards can be inserted explicitly
	olive::Track *t2 = append_track(&project, sequence, olive::Track::k_video);
	view.insert_track(track_handle(t2));
	ASSERT_EQ(splitter->count(), 3);

	// AlignTop ordering: items first, spacer last
	QPointer<QWidget> item = splitter->widget(1);
	ASSERT_FALSE(item.isNull());

	view.remove_track(track_handle(t2));
	EXPECT_EQ(splitter->count(), 2);
	// remove() deletes the item widget
	EXPECT_TRUE(item.isNull());
}

TEST(TrackViewItem, LabelShowsTypePrefixAndCustomLabel)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);
	olive::Track *v1 = append_track(&project, sequence, olive::Track::k_video);
	olive::Track *v2 = append_track(&project, sequence, olive::Track::k_video);
	olive::Track *a1 = append_track(&project, sequence, olive::Track::k_audio);
	olive::Track *s1 = append_track(&project, sequence, olive::Track::k_subtitle);

	ASSERT_EQ(
		oakengine_node_set_label(node_handle(v1), "Hero"), 0);

	olive::TrackViewItem item_v1(track_handle(v1));
	olive::TrackViewItem item_v2(track_handle(v2));
	olive::TrackViewItem item_a1(track_handle(a1));
	olive::TrackViewItem item_s1(track_handle(s1));

	auto *label_v1 = item_v1.findChild<olive::ClickableLabel *>();
	auto *label_v2 = item_v2.findChild<olive::ClickableLabel *>();
	auto *label_a1 = item_a1.findChild<olive::ClickableLabel *>();
	auto *label_s1 = item_s1.findChild<olive::ClickableLabel *>();
	ASSERT_NE(label_v1, nullptr);
	ASSERT_NE(label_v2, nullptr);
	ASSERT_NE(label_a1, nullptr);
	ASSERT_NE(label_s1, nullptr);

	// NLE-style prefix is 1-based (V1/V2, A1, S1) followed by the custom
	// label or, when unset, the engine default track name
	EXPECT_TRUE(label_v1->text().startsWith(QStringLiteral("V1  ")));
	EXPECT_TRUE(label_v1->text().contains(QStringLiteral("Hero")));

	EXPECT_TRUE(label_v2->text().startsWith(QStringLiteral("V2  ")));
	EXPECT_FALSE(label_v2->text().contains(QStringLiteral("Hero")));
	EXPECT_GT(label_v2->text().size(), 4);

	EXPECT_TRUE(label_a1->text().startsWith(QStringLiteral("A1  ")));
	EXPECT_TRUE(label_s1->text().startsWith(QStringLiteral("S1  ")));
}

TEST(TrackViewItem, RenameThroughLineEditRoundTrips)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);
	olive::Track *track = append_track(&project, sequence, olive::Track::k_video);

	olive::TrackViewItem item(track_handle(track));
	auto *stack = item.findChild<QStackedWidget *>();
	auto *label = item.findChild<olive::ClickableLabel *>();
	auto *edit = item.findChild<olive::FocusableLineEdit *>();
	ASSERT_NE(stack, nullptr);
	ASSERT_NE(label, nullptr);
	ASSERT_NE(edit, nullptr);

	// The label is the resting page of the stack
	EXPECT_EQ(stack->currentWidget(), label);

	// Double-clicking the label swaps in the line edit
	emit label->mouse_double_clicked();
	EXPECT_EQ(stack->currentWidget(), edit);

	// Confirming writes the label to the engine and swaps back
	edit->setText(QStringLiteral("Renamed"));
	emit edit->confirmed();
	EXPECT_EQ(stack->currentWidget(), label);
	EXPECT_TRUE(label->text().contains(QStringLiteral("Renamed")));

	char buf[256];
	buf[0] = '\0';
	oakengine_node_get_label(node_handle(track), buf, sizeof(buf));
	EXPECT_EQ(QString::fromUtf8(buf), QStringLiteral("Renamed"));

	// Cancelling discards the edit and restores the label page
	emit label->mouse_double_clicked();
	EXPECT_EQ(stack->currentWidget(), edit);
	edit->setText(QStringLiteral("Discarded"));
	emit edit->cancelled();
	EXPECT_EQ(stack->currentWidget(), label);

	buf[0] = '\0';
	oakengine_node_get_label(node_handle(track), buf, sizeof(buf));
	EXPECT_EQ(QString::fromUtf8(buf), QStringLiteral("Renamed"));
	EXPECT_TRUE(label->text().contains(QStringLiteral("Renamed")));
}

TEST(TrackViewItem, MuteAndLockButtonsDriveEngineState)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);
	olive::Track *track = append_track(&project, sequence, olive::Track::k_video);

	olive::TrackViewItem item(track_handle(track));

	// The buttons are identified by the checked color baked into their style
	// sheets (Qt::red for mute, Qt::gray for lock); resolve the colors the
	// same way the implementation does -- QColor(Qt::gray).name() is not the
	// SVG "#808080" one might expect
	QPushButton *mute = find_button_by_checked_color(
		&item, QColor(Qt::red).name());
	QPushButton *lock = find_button_by_checked_color(
		&item, QColor(Qt::gray).name());
	ASSERT_NE(mute, nullptr);
	ASSERT_NE(lock, nullptr);
	EXPECT_TRUE(mute->isCheckable());
	EXPECT_TRUE(lock->isCheckable());
	EXPECT_FALSE(mute->isChecked());
	EXPECT_FALSE(lock->isChecked());

	OakEngineSequence *seq = seq_handle(sequence);
	EXPECT_EQ(oakengine_track_is_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0), 0);
	EXPECT_EQ(oakengine_track_is_locked(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0), 0);

	// Clicking the button writes through to the engine track
	mute->click();
	EXPECT_EQ(oakengine_track_is_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0), 1);
	EXPECT_TRUE(mute->isChecked());

	// An external mute change reflects back on the button via the
	// EngineEventBridge subscription
	oakengine_track_set_muted(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0);
	EXPECT_FALSE(mute->isChecked());

	lock->click();
	EXPECT_EQ(oakengine_track_is_locked(seq, OAKENGINE_TRACK_TYPE_VIDEO, 0), 1);
	EXPECT_TRUE(lock->isChecked());
}

TEST(TrackViewItem, DeleteTrackEmitsSignalAndRemovesTrack)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);
	olive::Track *track = append_track(&project, sequence, olive::Track::k_video);

	// Owned by the test so the dangling engine handle after removal can't
	// outlive the widget
	auto *item = new olive::TrackViewItem(track_handle(track));

	OakEngineTrack *received = nullptr;
	QObject::connect(item, &olive::TrackViewItem::about_to_delete_track,
					 [&received](OakEngineTrack *t) { received = t; });

	// The context menu wires this slot with a queued connection; on the same
	// thread a direct meta-call is equivalent and avoids the modal menu
	QMetaObject::invokeMethod(item, "delete_track");

	EXPECT_EQ(received, track_handle(track));

	int video = -1, audio = -1, subtitle = -1;
	oakengine_sequence_track_count(seq_handle(sequence), &video, &audio,
								   &subtitle);
	EXPECT_EQ(video, 0);

	delete item;
}

TEST(TrackViewSplitter, ConstructionDefaults)
{
	olive::TrackViewSplitter splitter(Qt::AlignTop);
	EXPECT_EQ(splitter.orientation(), Qt::Vertical);
	EXPECT_EQ(splitter.handleWidth(), 1);
	// The trailing spacer is added by the constructor
	EXPECT_EQ(splitter.count(), 1);
	EXPECT_EQ(splitter.height(), 0);

	olive::TrackViewSplitter bottom(Qt::AlignBottom);
	EXPECT_EQ(bottom.count(), 1);
}

TEST(TrackViewSplitter, InsertGrowsFixedHeightAndOrdersWidgets)
{
	olive::TrackViewSplitter splitter(Qt::AlignTop);

	auto *w1 = new QWidget();
	auto *w2 = new QWidget();
	splitter.insert(0, 100, w1);

	// Height = item height + one handle width
	EXPECT_EQ(splitter.count(), 2);
	EXPECT_EQ(splitter.widget(0), w1);
	EXPECT_EQ(splitter.height(), 101);

	splitter.insert(1, 50, w2);

	// Height = both items + two handle widths; spacer stays last
	EXPECT_EQ(splitter.count(), 3);
	EXPECT_EQ(splitter.widget(0), w1);
	EXPECT_EQ(splitter.widget(1), w2);
	EXPECT_EQ(splitter.height(), 152);

	const QList<int> sz = splitter.sizes();
	ASSERT_EQ(sz.size(), 3);
	EXPECT_EQ(sz.at(0), 100);
	EXPECT_EQ(sz.at(1), 50);
}

TEST(TrackViewSplitter, InsertAlignBottomAppendsAfterSpacer)
{
	olive::TrackViewSplitter splitter(Qt::AlignBottom);

	auto *w1 = new QWidget();
	auto *w2 = new QWidget();
	splitter.insert(0, 60, w1);
	splitter.insert(1, 40, w2);

	// With bottom alignment the spacer stays at the top and later inserts
	// land between the spacer and the existing items
	ASSERT_EQ(splitter.count(), 3);
	EXPECT_EQ(splitter.widget(1), w2);
	EXPECT_EQ(splitter.widget(2), w1);

	// Height = both items + two handle widths
	EXPECT_EQ(splitter.height(), 102);
}

TEST(TrackViewSplitter, RemoveDeletesWidgetAndShrinksHeight)
{
	olive::TrackViewSplitter splitter(Qt::AlignTop);

	auto *w1 = new QWidget();
	auto *w2 = new QWidget();
	splitter.insert(0, 100, w1);
	splitter.insert(1, 50, w2);
	ASSERT_EQ(splitter.height(), 152);

	QPointer<QWidget> gone = w1;
	splitter.remove(0);

	EXPECT_TRUE(gone.isNull());
	EXPECT_EQ(splitter.count(), 2);
	EXPECT_EQ(splitter.widget(0), w2);
	// Removing an item subtracts its height and one handle width
	EXPECT_EQ(splitter.height(), 51);

	const QList<int> sz = splitter.sizes();
	ASSERT_EQ(sz.size(), 2);
	EXPECT_EQ(sz.at(0), 50);
}

TEST(TrackViewSplitter, SetSpacerHeightGrowsTotalHeight)
{
	olive::TrackViewSplitter splitter(Qt::AlignTop);

	auto *w1 = new QWidget();
	splitter.insert(0, 100, w1);
	ASSERT_EQ(splitter.height(), 101);

	// The spacer keeps a handle on the trailing edge; its height is added to
	// the fixed total
	splitter.set_spacer_height(40);
	EXPECT_EQ(splitter.height(), 141);
}

TEST(TrackViewSplitter, SetTrackHeightAdjustsFixedHeight)
{
	olive::TrackViewSplitter splitter(Qt::AlignTop);

	splitter.insert(0, 100, new QWidget());
	splitter.insert(1, 50, new QWidget());
	ASSERT_EQ(splitter.height(), 152);

	// Growing a track by 30px grows the whole splitter by 30px
	splitter.set_track_height(0, 130);
	EXPECT_EQ(splitter.height(), 182);

	// The fixed height always moves by (new - old) for the addressed track;
	// read the current size back instead of assuming an exact layout, since
	// QSplitter redistributes sizes when the fixed height changes
	const int old_size = splitter.sizes().at(1);
	const int old_height = splitter.height();
	splitter.set_track_height(1, 20);
	EXPECT_EQ(splitter.height(), old_height + (20 - old_size));
}

TEST(TrackViewSplitter, HandlesAreTrackViewSplitterHandles)
{
	olive::TrackViewSplitter splitter(Qt::AlignTop);
	splitter.insert(0, 100, new QWidget());

	// createHandle() must produce the custom handle type
	EXPECT_NE(qobject_cast<olive::TrackViewSplitterHandle *>(splitter.handle(1)),
			  nullptr);
}

TEST(TrackViewSplitter, HandleReceiverResizesAndEmits)
{
	olive::TrackViewSplitter splitter(Qt::AlignTop);
	splitter.insert(0, 100, new QWidget());
	splitter.insert(1, 50, new QWidget());
	ASSERT_EQ(splitter.height(), 152);

	const QList<int> sz = splitter.sizes();
	ASSERT_EQ(sz.size(), 3);
	ASSERT_EQ(sz.at(0), 100);

	QSignalSpy spy(&splitter, &olive::TrackViewSplitter::track_height_changed);

	// Dragging the handle below the first track grows it by the drag delta
	auto *handle =
		qobject_cast<olive::TrackViewSplitterHandle *>(splitter.handle(1));
	ASSERT_NE(handle, nullptr);
	splitter.handle_receiver(handle, 30);

	ASSERT_EQ(spy.count(), 1);
	EXPECT_EQ(spy.first().at(0).toInt(), 0);
	EXPECT_EQ(spy.first().at(1).toInt(), 130);
	EXPECT_EQ(splitter.height(), 182);
}
