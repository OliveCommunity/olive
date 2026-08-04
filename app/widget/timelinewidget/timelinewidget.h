/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef OAK_TIMELINEWIDGET_H
#define OAK_TIMELINEWIDGET_H

#include <QHash>
#include <QScrollBar>
#include <QRubberBand>
#include <QSlider>
#include <QWidget>

#include "core.h"
#include "oakengine/events.h"
#include "oakengine/node.h"
#include "oakengine/serializer.h"
#include "oakengine/timeline.h"
#include "oakengine/undo.h"
#include "timeline/timelinecommonapp.h"
#include "timelineandtrackview.h"
#include "widget/slider/rationalslider.h"
#include "widget/timebased/timebasedwidget.h"
#include "widget/timelinewidget/timelinewidgetselections.h"
#include "widget/timelinewidget/tool/import.h"
#include "widget/timelinewidget/tool/tool.h"

namespace olive
{

/**
 * @brief Full widget for working with TimelineOutput nodes
 *
 * Encapsulates TimelineViews, TimeRulers, and scrollbars for a complete widget to manipulate Timelines
 */
class TimelineWidget : public TimeBasedWidget {
	Q_OBJECT
public:
	TimelineWidget(QWidget *parent = nullptr);

	virtual ~TimelineWidget() override;

	void clear();

	void select_all();

	void deselect_all();

	void ripple_to_in();

	void ripple_to_out();

	void edit_to_in();

	void edit_to_out();

	void split_at_playhead();

	void DeleteSelected(bool ripple = false);

	void increase_track_height();

	void decrease_track_height();

	void insert_footage_at_playhead(const QVector<OakEngineNode *> &footage);

	void overwrite_footage_at_playhead(const QVector<OakEngineNode *> &footage);

	void toggle_links_on_selected();

	void add_default_transitions_to_selected();

	virtual bool copy_selected(bool cut) override;

	virtual bool paste() override;

	void paste_insert();

	void delete_in_to_out(bool ripple);

	void toggle_selected_enabled();

	void set_color_label(int index);

	void nudge_left();

	void nudge_right();

	void move_in_to_playhead();

	void move_out_to_playhead();

	void show_speed_duration_dialog_for_selected_clips();

	void synchronize_selected_clips_by_source_time();

	void synchronize_selected_clips_by_waveform();

	void synchronize_selected_clips_by_waveform_with_speed();

	void generate_proxies_for_selected_clips();

	void set_selected_clips_proxy_enabled(bool enabled);

	void reveal_proxy_for_selected_clips();

	void delete_proxies_for_selected_clips();

	void show_proxy_dialog_for_selected_clips();

	void recording_callback(const QString &filename, const TimeRange &time,
						   const TrackReference &track);

	void enable_recording_overlay(const TimelineCoordinate &coord);

	void disable_recording_overlay();

	void add_tentative_subtitle_track();

	void nest_selected_clips();

	/**
   * @brief Timelines should always be connected to sequences
   */
	OakEngineSequence *sequence() const
	{
		// R8: the type check goes through the C ABI facade predicate; the
		// Sequence handle is shared with the node handle, so no engine C++
		// definition is needed here (replaces the old static_cast, which
		// required the complete engine type).
		auto *connected = get_connected_node();
		return (connected &&
				oakengine_node_is_sequence(
					reinterpret_cast<OakEngineNode *>(connected)))
				   ? reinterpret_cast<OakEngineSequence *>(connected)
				   : nullptr;
	}

	const QVector<OakEngineBlock *> &get_selected_blocks() const
	{
		return selected_blocks_;
	}

	QByteArray save_splitter_state() const;

	void restore_splitter_state(const QByteArray &state);

	static void replace_blocks_with_gaps(const QVector<OakEngineBlock *> &blocks,
									  bool remove_from_graph,
									  void *command,
									  bool handle_transitions = true);

	/**
   * @brief Retrieve the QGraphicsItem at a particular scene position
   *
   * Requires a float-based scene position. If you have a screen position, use GetScenePos() first to convert it to a
   * scene position
   */
	OakEngineBlock *get_item_at_scene_pos(const TimelineCoordinate &coord);

	void add_selection(const TimeRange &time, const TrackReference &track);
	void add_selection(OakEngineBlock *item);

	void remove_selection(const TimeRange &time, const TrackReference &track);
	void remove_selection(OakEngineBlock *item);

	const TimelineWidgetSelections &get_selections() const
	{
		return selections_;
	}

	void set_selections(const TimelineWidgetSelections &s,
					   bool process_block_changes);

	OakEngineTrack *get_track_from_reference(const TrackReference &ref) const;

	void set_view_beam_cursor(const TimelineCoordinate &coord);
	void set_view_transition_overlay(OakEngineClip *out, OakEngineClip *in);

	const QVector<TimelineViewGhostItem *> &get_ghost_items() const
	{
		return ghost_items_;
	}

	void insert_gaps_at(const Rational &time, const Rational &length,
					  void *command);

	void start_rubber_band_select(const QPoint &global_cursor_start);
	void move_rubber_band_select(bool enable_selecting, bool select_links);
	void end_rubber_band_select();

	int get_track_y(const TrackReference &ref);
	int get_track_height(const TrackReference &ref);

	void add_ghost(TimelineViewGhostItem *ghost);

	void clear_ghosts();

	bool has_ghosts() const
	{
		return !ghost_items_.isEmpty();
	}

	bool is_block_selected(OakEngineBlock *b) const
	{
		return selected_blocks_.contains(b);
	}

	void set_block_links_selected(OakEngineClip *block, bool selected);

	void queue_scroll(int value);

	TimelineView *get_first_timeline_view();

	Rational get_timebase_for_track_type(TrackReference::Type type);

	const QRect &get_rubber_band_geometry() const;

	/**
   * @brief Track blocks that have newly been selected (this is preferred over emitting BlocksSelected directly)
   *
   * TimelineWidget keeps track of which blocks are selected internally. Calling this function will
   * add to that list and emit a signal to other widgets that said blocks have been selected.
   *
   * @param selected_blocks
   *
   * The list of blocks to add to the internal selection list and signal.
   *
   * @param filter
   *
   * TRUE to automatically filter blocks that are already selected from the list. In most cases,
   * this is preferable and should only be set to FALSE if the list is guaranteed not to contain
   * already selected blocks (and therefore filtering can be skipped to save time).
   */
	void signal_selected_blocks(QVector<OakEngineBlock *> selected_blocks,
							  bool filter = true);

	/**
   * @brief Track blocks that have been newly deselected
   */
	void signal_deselected_blocks(const QVector<OakEngineBlock *> &deselected_blocks);

	/**
   * @brief Convenience function to deselect all blocks and signal them
   */
	void signal_deselected_all_blocks();

	void refresh()
	{
		update_viewports();
	}

	void *take_subtitle_section_command()
	{
		// Copy pointer
		void *c = subtitle_show_command_;

		// Set to null
		subtitle_show_command_ = nullptr;
		subtitle_tentative_track_ = nullptr;

		// Return command
		return c;
	}

	void *create_set_selections_command(const TimelineWidgetSelections &now,
										const TimelineWidgetSelections &old,
										bool process_block_changes = true);

public slots:
	void clear_tentative_subtitle_track();

	void rename_selected_blocks();

signals:

	void block_selection_changed(const QVector<OakEngineBlock *> &selected_blocks);

	void request_capture_start(const TimeRange &time,
							 const TrackReference &track);

	void reveal_viewer_in_footage_viewer(OakEngineNode *r, const TimeRange &range);
	void reveal_viewer_in_project(OakEngineNode *r);

protected:
	virtual void resizeEvent(QResizeEvent *event) override;

	virtual void TimeChangedEvent(const Rational &) override;
	virtual void TimebaseChangedEvent(const Rational &) override;
	virtual void ScaleChangedEvent(const double &) override;

	virtual void ConnectNodeEvent(OakEngineNode *n) override;
	virtual void DisconnectNodeEvent(OakEngineNode *n) override;

	virtual const QVector<OakEngineBlock *> *get_snap_blocks() const override
	{
		return &added_blocks_;
	}

protected slots:
	virtual void SendCatchUpScrollEvent() override;

private:
	QVector<TimelineApp::EditToInfo> get_edit_to_info(const Rational &playhead_time,
													TimelineApp::MovementMode mode);

	void ripple_to(TimelineApp::MovementMode mode);

	void edit_to(TimelineApp::MovementMode mode);

	void update_viewports(const TrackReference::Type &type = TrackReference::k_none);

	bool paste_internal(bool insert);

	void synchronize_selected_clips_by_waveform_internal(bool allow_speed);

	TimelineAndTrackView *add_timeline_and_track_view(Qt::Alignment alignment);

	QHash<OakEngineNode *, OakEngineNode *>
	generate_existing_paste_map(void *clipboard);

	QRubberBand rubberband_;
	QVector<QPointF> rubberband_scene_pos_;
	TimelineWidgetSelections rubberband_old_selections_;
	QVector<OakEngineBlock *> rubberband_now_selected_;
	bool rubberband_enable_selecting_;
	bool rubberband_select_links_;

	TimelineWidgetSelections selections_;

	TimelineTool *get_active_tool();

	QVector<TimelineTool *> tools_;

	ImportTool *import_tool_;

	TimelineTool *active_tool_;

	QVector<TimelineViewGhostItem *> ghost_items_;

	QVector<TimelineAndTrackView *> views_;

	RationalSlider *timecode_label_;

	QMetaObject::Connection timecode_playhead_conn_;

	QVector<OakEngineBlock *> selected_blocks_;

	QVector<OakEngineBlock *> added_blocks_;

	QHash<OakEngineBlock *, QVector<int64_t>> block_subscriptions_;

	int deferred_scroll_value_;

	bool use_audio_time_units_;

	QSplitter *view_splitter_;

	QSlider *zoom_slider_;
	QSlider *track_height_slider_;

	void *subtitle_show_command_;
	OakEngineTrack *subtitle_tentative_track_;

	QTimer *signal_block_change_timer_;

	// Command userdata for splitter size changes
	struct SplitterSizesCmdData {
		QSplitter *splitter;
		QList<int> new_sizes;
		QList<int> old_sizes;
	};

	static void splitter_sizes_redo(void *userdata)
	{
		auto *d = static_cast<SplitterSizesCmdData *>(userdata);
		d->old_sizes = d->splitter->sizes();
		d->splitter->setSizes(d->new_sizes);
	}

	static void splitter_sizes_undo(void *userdata)
	{
		auto *d = static_cast<SplitterSizesCmdData *>(userdata);
		d->splitter->setSizes(d->old_sizes);
	}

	static void splitter_sizes_free(void *userdata)
	{
		delete static_cast<SplitterSizesCmdData *>(userdata);
	}

	static void *make_splitter_sizes_command(QSplitter *splitter, const QList<int> &sizes)
	{
		auto *d = new SplitterSizesCmdData;
		d->splitter = splitter;
		d->new_sizes = sizes;
		return oakengine_undo_command_create(nullptr, splitter_sizes_redo,
											 splitter_sizes_undo,
											 splitter_sizes_free, d);
	}

	void center_on(qreal scene_pos);

	void update_view_timebases();

	void nudge_internal(Rational amount);

	void move_to_playhead_internal(bool out);

private slots:
	void view_mouse_pressed(TimelineViewMouseEvent *event);
	void view_mouse_moved(TimelineViewMouseEvent *event);
	void view_mouse_released(TimelineViewMouseEvent *event);
	void view_mouse_double_clicked(TimelineViewMouseEvent *event);

	void view_drag_entered(TimelineViewMouseEvent *event);
	void view_drag_moved(TimelineViewMouseEvent *event);
	void view_drag_left(QDragLeaveEvent *event);
	void view_drag_dropped(TimelineViewMouseEvent *event);

	void track_updated(TrackReference::Type type);

	void block_updated(OakEngineBlock *block = nullptr);

	void update_horizontal_splitters();

	void update_timecode_width_from_splitters(QSplitter *s);

	void show_context_menu();

	void DeferredScrollAction();

	void show_sequence_dialog();

	void set_use_audio_time_units(bool use);

	void tool_changed();

	void addable_object_changed();

	void set_view_waveforms_enabled(bool e);

	void set_view_thumbnails_enabled(QAction *action);

	void frame_rate_changed();

	void sample_rate_changed();

	void signal_block_selection_change();

	void reveal_in_footage_viewer();
	void reveal_in_project();

	void set_selected_clips_autocaching(bool e);

	void cache_clips();
	void cache_clips_in_out();
	void cache_discard();

	void multicam_enabled_triggered(bool e);

	void force_update_rubber_band();

private:
	void add_block(OakEngineBlock *block);
	void remove_block(OakEngineBlock *blocks);

	void add_track(OakEngineTrack *track);
	void remove_track(OakEngineTrack *track);

	void track_index_changed(OakEngineTrack *track, int old, int now);
	void track_about_to_be_deleted(OakEngineTrack *track);
};

}

#endif // OAK_TIMELINEWIDGET_H
