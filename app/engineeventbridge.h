/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2026 Oak Team

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

#ifndef ENGINEEVENTBRIDGE_H
#define ENGINEEVENTBRIDGE_H

#include <QObject>
#include <QVector>

#include "oakengine/events.h"

#include "oakengine/task.h"
struct OakEngineColorManager;

namespace olive
{

/**
 * @brief Qt-signal adapter over the liboakengine event C ABI
 * (oakengine/events.h).
 *
 * The facade delivers engine change notifications as C callbacks; the
 * application consumes Qt signals. EngineEventBridge sits in between:
 * subscribe() registers a C callback on an engine handle, and the bridge
 * re-emits the event as the matching typed Qt signal. This is the standard
 * replacement for connect(engineObject, &EngineClass::signal, ...) at the
 * remaining app -> engine connection points (see
 * docs/zh/facade-migration-roadmap.md).
 *
 * Semantics match the connections they replace: the engine invokes the C
 * callback synchronously on the emitting thread (Qt::DirectConnection
 * equivalent) and the bridge emits its Qt signals from that same callback,
 * so receivers are still called synchronously on the engine object's
 * thread (the GUI thread in practice).
 *
 * The bridge owns its subscriptions: destroying it unsubscribes
 * everything. Subscriptions also die automatically with the observed
 * engine object (the facade drops them on QObject::destroyed), so a
 * project/sequence teardown never leaves a dangling callback into the app.
 */
class EngineEventBridge : public QObject {
	Q_OBJECT
public:
	explicit EngineEventBridge(QObject *parent = nullptr);

	~EngineEventBridge() override;

	/**
	 * @brief Subscribe to `event_id` (OAKENGINE_EVENT_*) on `handle` and
	 * return the subscription id (> 0), or 0 on failure.
	 *
	 * `handle` is a facade handle (an engine Node or Project reinterpreted
	 * as OakEngineNode / OakEngineProject etc., per the event table in
	 * oakengine/events.h). Each matching engine change is re-emitted as
	 * the corresponding Qt signal of this bridge.
	 */
	int64_t subscribe(void *handle, int32_t event_id);

	/**
	 * @brief Cancel a subscription returned by subscribe(). Unknown or
	 * already-dead ids are ignored.
	 */
	void unsubscribe(int64_t id);

	/**
	 * @brief Cancel every live subscription (but keep the Qt signal
	 * connections). Use when the observed engine object set changes, e.g.
	 * switching sequences, so stale subscriptions don't pile up.
	 */
	void unsubscribe_all();

signals:
	void project_modified_changed(bool modified);
	void project_name_changed();

	void folder_begin_insert_item(OakEngineNode *folder,
								  OakEngineNode *child, int index);
	void folder_end_insert_item(OakEngineNode *folder);
	void folder_begin_remove_item(OakEngineNode *folder,
								  OakEngineNode *child, int index);
	void folder_end_remove_item(OakEngineNode *folder);

	void sequence_track_added(OakEngineTrack *track, int track_type);
	void sequence_track_removed(OakEngineTrack *track, int track_type);
	void sequence_track_list_changed(OakEngineSequence *source, int track_type);
	void sequence_track_height_changed(OakEngineSequence *source,
									   OakEngineTrack *track, int track_type,
									   int height_px);
	void sequence_subtitles_changed(OakEngineSequence *source, qint64 in_ts,
									qint64 out_ts);

	void track_block_added(OakEngineBlock *block, qint64 in_ts,
						   qint64 out_ts);
	void track_block_removed(OakEngineBlock *block, qint64 in_ts,
							 qint64 out_ts);
	void track_index_changed(OakEngineTrack *source, int old_index,
							 int new_index);
	void track_height_changed(OakEngineTrack *source, double height);
	void track_blocks_refreshed(OakEngineTrack *source);
	void track_muted_changed(OakEngineTrack *source, bool muted);

	void block_enabled_changed(OakEngineBlock *source);
	void block_preview_changed(OakEngineBlock *source);

	void sequence_marker_added(qint64 time_ts);
	void sequence_marker_removed(qint64 time_ts);
	void sequence_marker_modified(qint64 time_ts);

	void marker_list_marker_added(OakEngineMarkerList *source,
								  OakEngineMarker *marker);
	void marker_list_marker_removed(OakEngineMarkerList *source,
									OakEngineMarker *marker);
	void marker_list_marker_modified(OakEngineMarkerList *source,
									 OakEngineMarker *marker);

	void sequence_workarea_range_changed(qint64 in_ts, qint64 out_ts);
	void sequence_workarea_enabled_changed(bool enabled);

	void workarea_range_changed(OakEngineWorkarea *source);
	void workarea_enabled_changed(OakEngineWorkarea *source, bool enabled);

	/* Node family (source is the subscribed OakEngineNode*). Input ids are
	 * copied out of the event during the callback. */
	void node_label_changed(OakEngineNode *source, const QString &label);
	void node_input_value_changed(OakEngineNode *source, const QString &input,
								  int element, qint64 in_ts, qint64 out_ts);
	void node_input_connected(OakEngineNode *source, OakEngineNode *output,
							  const QString &input, int element);
	void node_input_disconnected(OakEngineNode *source, OakEngineNode *output,
								 const QString &input, int element);
	void node_input_flags_changed(OakEngineNode *source, const QString &input,
								  qint64 flags);
	void node_input_property_changed(OakEngineNode *source,
									 const QString &input);
	void node_input_data_type_changed(OakEngineNode *source,
									  const QString &input, int type);
	void node_input_array_size_changed(OakEngineNode *source,
									   const QString &input, int old_size,
									   int new_size);
	void node_keyframe_enable_changed(OakEngineNode *source,
									  const QString &input, int element,
									  bool enabled);
	void node_keyframe_added(OakEngineNode *source, OakEngineKeyframe *key,
							 const QString &input, int element, int track);
	void node_keyframe_removed(OakEngineNode *source, OakEngineKeyframe *key,
							   const QString &input, int element, int track);
	void node_keyframe_time_changed(OakEngineNode *source,
									OakEngineKeyframe *key);
	void node_keyframe_type_changed(OakEngineNode *source,
									OakEngineKeyframe *key);
	void node_keyframe_value_changed(OakEngineNode *source,
									 OakEngineKeyframe *key);
	void node_node_added_to_context(OakEngineNode *source,
									OakEngineNode *node);
	void node_node_removed_from_context(OakEngineNode *source,
										OakEngineNode *node);
	void node_message_count_changed(OakEngineNode *source);
	void node_links_changed(OakEngineNode *source);
	void node_color_changed(OakEngineNode *source);
	void node_input_added(OakEngineNode *source, const QString &input_id);
	void node_input_removed(OakEngineNode *source, const QString &input_id);
	void node_removed_from_graph(OakEngineNode *source,
								 OakEngineNode *project);

	/* Group family (source is the group node). */
	void group_input_passthrough_added(OakEngineNode *source,
									   OakEngineNode *node,
									   const QString &input, int element);
	void group_input_passthrough_removed(OakEngineNode *source,
										 OakEngineNode *node,
										 const QString &input, int element);
	void group_output_passthrough_changed(OakEngineNode *source,
										  OakEngineNode *output);

	/* Context position (source is the context node). */
	void node_context_position_changed(OakEngineNode *source,
									   OakEngineNode *node, double x,
									   double y);

	/* Viewer family (source is the subscribed viewer OakEngineNode*).
	 * Rational payloads (seconds) are delivered as num/den pairs. */
	void viewer_length_changed(OakEngineNode *source, qint64 num, qint64 den);
	void viewer_playhead_changed(OakEngineNode *source, qint64 num,
								 qint64 den);
	void viewer_frame_rate_changed(OakEngineNode *source, qint64 num,
								   qint64 den);
	void viewer_size_changed(OakEngineNode *source, int w, int h);
	void viewer_pixel_aspect_changed(OakEngineNode *source, qint64 num,
									 qint64 den);
	void viewer_interlacing_changed(OakEngineNode *source, int mode);
	void viewer_video_params_changed(OakEngineNode *source);
	void viewer_audio_params_changed(OakEngineNode *source);
	void viewer_texture_input_changed(OakEngineNode *source);
	void viewer_sample_rate_changed(OakEngineNode *source, int sr);
	void viewer_connected_waveform_changed(OakEngineNode *source);

	/* Task manager family (title is copied out of the event). */
	void task_manager_task_added(OakEngineTask *task, const QString &title);
	void task_manager_task_removed(OakEngineTask *task);
	void task_manager_task_failed(OakEngineTask *task);
	void task_manager_list_changed();

	/* Task family (source is the subscribed OakEngineTask*). */
	void task_started(OakEngineTask *source, qint64 start_time);
	void task_progress(OakEngineTask *source, double progress);
	void task_finished(OakEngineTask *source, bool succeeded);

	/* Undo stack family. */
	void undo_index_changed(int index);

	/* ColorManager/OCIO family. */
	void color_manager_config_changed(OakEngineColorManager *source);
	void color_manager_reference_space_changed(
		OakEngineColorManager *source);

	/* AudioManager family. */
	void audio_output_params_changed();

	/* Playback cache / frame cache family (B9c). */
	void playback_cache_invalidated(void *cache, qint64 a, qint64 b);
	void playback_cache_validated(void *cache, qint64 a, qint64 b);
	void frame_cache_invalidated(void *cache, qint64 a, qint64 b);

private:
	// C callback entry point; `userdata` is the EngineEventBridge.
	static void on_engine_event(const oakengine_event *event,
								void *userdata);

	void dispatch(const oakengine_event *event);

	QVector<int64_t> subscriptions_;
};

}

#endif // ENGINEEVENTBRIDGE_H
