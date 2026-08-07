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

#include "engineeventbridge.h"

#include <cstring>

#include "oakengine/node.h"

namespace olive
{

EngineEventBridge::EngineEventBridge(QObject *parent) : QObject(parent) {}

EngineEventBridge::~EngineEventBridge()
{
	foreach (int64_t id, subscriptions_) {
		oakengine_event_unsubscribe(id);
	}
}

int64_t EngineEventBridge::subscribe(void *handle, int32_t event_id)
{
	const int64_t id =
		oakengine_event_subscribe(handle, event_id, &on_engine_event, this);
	if (id > 0) {
		subscriptions_.append(id);
	}
	return id;
}

void EngineEventBridge::unsubscribe(int64_t id)
{
	if (oakengine_event_unsubscribe(id) == OAKENGINE_OK) {
		subscriptions_.removeAll(id);
	}
}

void EngineEventBridge::unsubscribe_all()
{
	foreach (int64_t id, subscriptions_) {
		oakengine_event_unsubscribe(id);
	}
	subscriptions_.clear();
}

void EngineEventBridge::on_engine_event(const oakengine_event *event,
										void *userdata)
{
	static_cast<EngineEventBridge *>(userdata)->dispatch(event);
}

void EngineEventBridge::dispatch(const oakengine_event *event)
{
	switch (event->id) {
	case OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED:
		emit project_modified_changed(event->a != 0);
		break;
	case OAKENGINE_EVENT_PROJECT_NAME_CHANGED:
		emit project_name_changed();
		break;
	case OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM:
		emit folder_begin_insert_item(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle), int(event->a));
		break;
	case OAKENGINE_EVENT_FOLDER_END_INSERT_ITEM:
		emit folder_end_insert_item(static_cast<OakEngineNode *>(event->source));
		break;
	case OAKENGINE_EVENT_FOLDER_BEGIN_REMOVE_ITEM:
		emit folder_begin_remove_item(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle), int(event->a));
		break;
	case OAKENGINE_EVENT_FOLDER_END_REMOVE_ITEM:
		emit folder_end_remove_item(static_cast<OakEngineNode *>(event->source));
		break;
	case OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED:
		emit sequence_track_added(static_cast<OakEngineTrack *>(event->handle),
								  int(event->a));
		break;
	case OAKENGINE_EVENT_SEQUENCE_TRACK_REMOVED:
		emit sequence_track_removed(
			static_cast<OakEngineTrack *>(event->handle), int(event->a));
		break;
	case OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED:
		emit sequence_track_list_changed(
			static_cast<OakEngineSequence *>(event->source), int(event->a));
		break;
	case OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED:
		emit sequence_track_height_changed(
			static_cast<OakEngineSequence *>(event->source),
			static_cast<OakEngineTrack *>(event->handle), int(event->a),
			int(event->b));
		break;
	case OAKENGINE_EVENT_SEQUENCE_SUBTITLES_CHANGED:
		emit sequence_subtitles_changed(
			static_cast<OakEngineSequence *>(event->source), event->a,
			event->b);
		break;
	case OAKENGINE_EVENT_TRACK_INDEX_CHANGED:
		emit track_index_changed(static_cast<OakEngineTrack *>(event->source),
								 int(event->a), int(event->b));
		break;
	case OAKENGINE_EVENT_TRACK_HEIGHT_CHANGED: {
		double h;
		memcpy(&h, &event->a, sizeof(h));
		emit track_height_changed(static_cast<OakEngineTrack *>(event->source),
								  h);
		break;
	}
	case OAKENGINE_EVENT_TRACK_BLOCKS_REFRESHED:
		emit track_blocks_refreshed(
			static_cast<OakEngineTrack *>(event->source));
		break;
	case OAKENGINE_EVENT_TRACK_MUTED_CHANGED:
		emit track_muted_changed(static_cast<OakEngineTrack *>(event->source),
								 event->a != 0);
		break;
	case OAKENGINE_EVENT_BLOCK_ENABLED_CHANGED:
		emit block_enabled_changed(
			static_cast<OakEngineBlock *>(event->source));
		break;
	case OAKENGINE_EVENT_BLOCK_PREVIEW_CHANGED:
		emit block_preview_changed(
			static_cast<OakEngineBlock *>(event->source));
		break;
	case OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED:
		emit marker_list_marker_added(
			static_cast<OakEngineMarkerList *>(event->source),
			static_cast<OakEngineMarker *>(event->handle));
		break;
	case OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED:
		emit marker_list_marker_removed(
			static_cast<OakEngineMarkerList *>(event->source),
			static_cast<OakEngineMarker *>(event->handle));
		break;
	case OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED:
		emit marker_list_marker_modified(
			static_cast<OakEngineMarkerList *>(event->source),
			static_cast<OakEngineMarker *>(event->handle));
		break;
	case OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED:
		emit workarea_range_changed(
			static_cast<OakEngineWorkarea *>(event->source));
		break;
	case OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED:
		emit workarea_enabled_changed(
			static_cast<OakEngineWorkarea *>(event->source), event->a != 0);
		break;
	case OAKENGINE_EVENT_TRACK_BLOCK_ADDED:
		emit track_block_added(static_cast<OakEngineBlock *>(event->handle),
							   event->a, event->b);
		break;
	case OAKENGINE_EVENT_TRACK_BLOCK_REMOVED:
		emit track_block_removed(static_cast<OakEngineBlock *>(event->handle),
								 event->a, event->b);
		break;
	case OAKENGINE_EVENT_SEQUENCE_MARKER_ADDED:
		emit sequence_marker_added(event->a);
		break;
	case OAKENGINE_EVENT_SEQUENCE_MARKER_REMOVED:
		emit sequence_marker_removed(event->a);
		break;
	case OAKENGINE_EVENT_SEQUENCE_MARKER_MODIFIED:
		emit sequence_marker_modified(event->a);
		break;
	case OAKENGINE_EVENT_SEQUENCE_WORKAREA_RANGE_CHANGED:
		emit sequence_workarea_range_changed(event->a, event->b);
		break;
	case OAKENGINE_EVENT_SEQUENCE_WORKAREA_ENABLED_CHANGED:
		emit sequence_workarea_enabled_changed(event->a != 0);
		break;
	case OAKENGINE_EVENT_NODE_LABEL_CHANGED:
		emit node_label_changed(static_cast<OakEngineNode *>(event->source),
								QString::fromUtf8(event->s ? event->s : ""));
		break;
	case OAKENGINE_EVENT_NODE_INPUT_VALUE_CHANGED:
		emit node_input_value_changed(
			static_cast<OakEngineNode *>(event->source),
			QString::fromUtf8(event->s ? event->s : ""), int(event->a),
			event->b, event->c);
		break;
	case OAKENGINE_EVENT_NODE_INPUT_CONNECTED:
		emit node_input_connected(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle),
			QString::fromUtf8(event->s ? event->s : ""), int(event->a));
		break;
	case OAKENGINE_EVENT_NODE_INPUT_DISCONNECTED:
		emit node_input_disconnected(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle),
			QString::fromUtf8(event->s ? event->s : ""), int(event->a));
		break;
	case OAKENGINE_EVENT_NODE_INPUT_FLAGS_CHANGED:
		emit node_input_flags_changed(
			static_cast<OakEngineNode *>(event->source),
			QString::fromUtf8(event->s ? event->s : ""), event->a);
		break;
	case OAKENGINE_EVENT_NODE_INPUT_PROPERTY_CHANGED:
		emit node_input_property_changed(
			static_cast<OakEngineNode *>(event->source),
			QString::fromUtf8(event->s ? event->s : ""));
		break;
	case OAKENGINE_EVENT_NODE_INPUT_DATA_TYPE_CHANGED:
		emit node_input_data_type_changed(
			static_cast<OakEngineNode *>(event->source),
			QString::fromUtf8(event->s ? event->s : ""), int(event->a));
		break;
	case OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED:
		emit node_input_array_size_changed(
			static_cast<OakEngineNode *>(event->source),
			QString::fromUtf8(event->s ? event->s : ""), int(event->a),
			int(event->b));
		break;
	case OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED:
		emit node_keyframe_enable_changed(
			static_cast<OakEngineNode *>(event->source),
			QString::fromUtf8(event->s ? event->s : ""), int(event->a),
			event->b != 0);
		break;
	case OAKENGINE_EVENT_NODE_KEYFRAME_ADDED:
		emit node_keyframe_added(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineKeyframe *>(event->handle),
			QString::fromUtf8(event->s ? event->s : ""), int(event->a),
			int(event->b));
		break;
	case OAKENGINE_EVENT_NODE_KEYFRAME_REMOVED:
		emit node_keyframe_removed(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineKeyframe *>(event->handle),
			QString::fromUtf8(event->s ? event->s : ""), int(event->a),
			int(event->b));
		break;
	case OAKENGINE_EVENT_NODE_KEYFRAME_TIME_CHANGED:
		emit node_keyframe_time_changed(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineKeyframe *>(event->handle));
		break;
	case OAKENGINE_EVENT_NODE_KEYFRAME_TYPE_CHANGED:
		emit node_keyframe_type_changed(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineKeyframe *>(event->handle));
		break;
	case OAKENGINE_EVENT_NODE_KEYFRAME_VALUE_CHANGED:
		emit node_keyframe_value_changed(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineKeyframe *>(event->handle));
		break;
	case OAKENGINE_EVENT_NODE_NODE_ADDED_TO_CONTEXT:
		emit node_node_added_to_context(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle));
		break;
	case OAKENGINE_EVENT_NODE_NODE_REMOVED_FROM_CONTEXT:
		emit node_node_removed_from_context(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle));
		break;
	case OAKENGINE_EVENT_NODE_MESSAGE_COUNT_CHANGED:
		emit node_message_count_changed(
			static_cast<OakEngineNode *>(event->source));
		break;
	case OAKENGINE_EVENT_NODE_LINKS_CHANGED:
		emit node_links_changed(static_cast<OakEngineNode *>(event->source));
		break;
	case OAKENGINE_EVENT_NODE_COLOR_CHANGED:
		emit node_color_changed(static_cast<OakEngineNode *>(event->source));
		break;
	case OAKENGINE_EVENT_NODE_INPUT_ADDED:
		emit node_input_added(
			static_cast<OakEngineNode *>(event->source),
			QString::fromUtf8(event->s ? event->s : ""));
		break;
	case OAKENGINE_EVENT_NODE_INPUT_REMOVED:
		emit node_input_removed(
			static_cast<OakEngineNode *>(event->source),
			QString::fromUtf8(event->s ? event->s : ""));
		break;
	case OAKENGINE_EVENT_NODE_REMOVED_FROM_GRAPH:
		emit node_removed_from_graph(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle));
		break;
	case OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED:
		emit group_input_passthrough_added(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle),
			QString::fromUtf8(event->s ? event->s : ""),
			static_cast<int>(event->a));
		break;
	case OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_REMOVED:
		emit group_input_passthrough_removed(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle),
			QString::fromUtf8(event->s ? event->s : ""),
			static_cast<int>(event->a));
		break;
	case OAKENGINE_EVENT_GROUP_OUTPUT_PASSTHROUGH_CHANGED:
		emit group_output_passthrough_changed(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle));
		break;
	case OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED: {
		double x, y;
		memcpy(&x, &event->a, sizeof(x));
		memcpy(&y, &event->b, sizeof(y));
		emit node_context_position_changed(
			static_cast<OakEngineNode *>(event->source),
			static_cast<OakEngineNode *>(event->handle), x, y);
		break;
	}
	case OAKENGINE_EVENT_VIEWER_LENGTH_CHANGED:
		emit viewer_length_changed(static_cast<OakEngineNode *>(event->source),
								   event->a, event->b);
		break;
	case OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED:
		emit viewer_playhead_changed(
			static_cast<OakEngineNode *>(event->source), event->a, event->b);
		break;
	case OAKENGINE_EVENT_VIEWER_FRAME_RATE_CHANGED:
		emit viewer_frame_rate_changed(
			static_cast<OakEngineNode *>(event->source), event->a, event->b);
		break;
	case OAKENGINE_EVENT_VIEWER_SIZE_CHANGED:
		emit viewer_size_changed(static_cast<OakEngineNode *>(event->source),
								 int(event->a), int(event->b));
		break;
	case OAKENGINE_EVENT_VIEWER_PIXEL_ASPECT_CHANGED:
		emit viewer_pixel_aspect_changed(
			static_cast<OakEngineNode *>(event->source), event->a, event->b);
		break;
	case OAKENGINE_EVENT_VIEWER_INTERLACING_CHANGED:
		emit viewer_interlacing_changed(
			static_cast<OakEngineNode *>(event->source), int(event->a));
		break;
	case OAKENGINE_EVENT_VIEWER_VIDEO_PARAMS_CHANGED:
		emit viewer_video_params_changed(
			static_cast<OakEngineNode *>(event->source));
		break;
	case OAKENGINE_EVENT_VIEWER_AUDIO_PARAMS_CHANGED:
		emit viewer_audio_params_changed(
			static_cast<OakEngineNode *>(event->source));
		break;
	case OAKENGINE_EVENT_VIEWER_TEXTURE_INPUT_CHANGED:
		emit viewer_texture_input_changed(
			static_cast<OakEngineNode *>(event->source));
		break;
	case OAKENGINE_EVENT_VIEWER_SAMPLE_RATE_CHANGED:
		emit viewer_sample_rate_changed(
			static_cast<OakEngineNode *>(event->source), int(event->a));
		break;
	case OAKENGINE_EVENT_VIEWER_CONNECTED_WAVEFORM_CHANGED:
		emit viewer_connected_waveform_changed(
			static_cast<OakEngineNode *>(event->source));
		break;
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_ADDED:
		emit task_manager_task_added(
			static_cast<OakEngineTask *>(event->handle),
			QString::fromUtf8(event->s ? event->s : ""));
		break;
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED:
		emit task_manager_task_removed(
			static_cast<OakEngineTask *>(event->handle));
		break;
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_FAILED:
		emit task_manager_task_failed(
			static_cast<OakEngineTask *>(event->handle));
		break;
	case OAKENGINE_EVENT_TASK_MANAGER_LIST_CHANGED:
		emit task_manager_list_changed();
		break;
	case OAKENGINE_EVENT_TASK_STARTED:
		emit task_started(static_cast<OakEngineTask *>(event->source),
						  event->a);
		break;
	case OAKENGINE_EVENT_TASK_PROGRESS: {
		double d;
		memcpy(&d, &event->a, sizeof(d));
		emit task_progress(static_cast<OakEngineTask *>(event->source), d);
		break;
	}
	case OAKENGINE_EVENT_TASK_FINISHED:
		emit task_finished(static_cast<OakEngineTask *>(event->source),
						   event->a != 0);
		break;
	case OAKENGINE_EVENT_UNDO_INDEX_CHANGED:
		emit undo_index_changed(int(event->a));
		break;
	case OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED:
		emit color_manager_config_changed(
			static_cast<OakEngineColorManager *>(event->source));
		break;
	case OAKENGINE_EVENT_COLOR_MANAGER_REFERENCE_SPACE_CHANGED:
		emit color_manager_reference_space_changed(
			static_cast<OakEngineColorManager *>(event->source));
		break;
	case OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED:
		emit audio_output_params_changed();
		break;
	case OAKENGINE_EVENT_PLAYBACK_CACHE_INVALIDATED:
		emit playback_cache_invalidated(event->source, event->a, event->b);
		break;
	case OAKENGINE_EVENT_PLAYBACK_CACHE_VALIDATED:
		emit playback_cache_validated(event->source, event->a, event->b);
		break;
	case OAKENGINE_EVENT_FRAME_CACHE_INVALIDATED:
		emit frame_cache_invalidated(event->source, event->a, event->b);
		break;
	default:
		break;
	}
}

}
