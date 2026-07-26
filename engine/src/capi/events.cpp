/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "oakengine/events.h"

#include <atomic>

#include <cstring>

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>

#include "audio/audiomanager.h"
#include "coreengine.h"
#include "node/keyframe.h"
#include "oakengine/node.h"
#include "node/block/block.h"
#include "node/color/colormanager/colormanager.h"
#include "node/group/group.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/sequence/sequence.h"
#include "render/framehashcache.h"
#include "render/playbackcache.h"
#include "task/task.h"
#include "task/taskmanager.h"
#include "timeline/timelinemarker.h"
#include "timeline/timelineworkarea.h"
#include "undo/undostack.h"

namespace
{

// Subscription registry: id -> connections. Callbacks capture the function
// pointer and userdata directly, so delivery never touches the registry;
// the map only tracks lifecycle (unsubscribe, sender teardown).
struct Subscription {
	QVector<QMetaObject::Connection> connections;
};

QMutex g_registry_mutex;
QHash<int64_t, Subscription> g_registry;
std::atomic<int64_t> g_next_id{1};

// The observed engine object died: drop the registry entry. Qt has already
// torn down the connections themselves.
void drop_subscription(int64_t id)
{
	QMutexLocker locker(&g_registry_mutex);
	g_registry.remove(id);
}

void invoke(oakengine_event_fn fn, void *userdata, int32_t id, void *source,
			int64_t a, int64_t b, void *related, int64_t c = 0,
			const char *s = nullptr)
{
	oakengine_event event;
	event.id = id;
	event.reserved = 0;
	event.a = a;
	event.b = b;
	event.c = c;
	event.source = source;
	event.handle = related;
	event.s = s;
	fn(&event, userdata);
}

// Frame-timestamp timebase for node events: the frame rate of the
// project's first sequence, or the engine default (1001/30000 s per
// frame). Same convention as node.cpp's project_time_base().
olive::Rational node_frame_time_base(const olive::Node *node)
{
	if (const olive::Project *p =
			olive::Project::get_project_from_object(node)) {
		for (olive::Node *n : p->nodes()) {
			if (const olive::Sequence *s =
					dynamic_cast<olive::Sequence *>(n)) {
				const olive::Rational fr = s->get_video_params().frame_rate();
				if (!fr.isNull() && !fr.isNaN()) {
					return fr.flipped();
				}
			}
		}
	}
	return olive::Rational(1001, 30000);
}

// NodeValue::Type -> facade value type (same mapping as node.cpp).
int node_value_type_to_c(olive::NodeValue::Type t)
{
	switch (t) {
	case olive::NodeValue::k_int:
		return OAK_NODE_VALUE_INT;
	case olive::NodeValue::k_float:
		return OAK_NODE_VALUE_FLOAT;
	case olive::NodeValue::k_boolean:
		return OAK_NODE_VALUE_BOOL;
	case olive::NodeValue::k_rational:
		return OAK_NODE_VALUE_RATIONAL;
	case olive::NodeValue::k_color:
		return OAK_NODE_VALUE_COLOR;
	case olive::NodeValue::k_vec2:
		return OAK_NODE_VALUE_VEC2;
	case olive::NodeValue::k_vec3:
		return OAK_NODE_VALUE_VEC3;
	case olive::NodeValue::k_vec4:
		return OAK_NODE_VALUE_VEC4;
	case olive::NodeValue::k_combo:
		return OAK_NODE_VALUE_COMBO;
	case olive::NodeValue::k_file:
		return OAK_NODE_VALUE_STRING;
	case olive::NodeValue::k_text:
		return OAK_NODE_VALUE_TEXT;
	case olive::NodeValue::k_font:
		return OAK_NODE_VALUE_FONT;
	case olive::NodeValue::k_str_combo:
		return OAK_NODE_VALUE_STR_COMBO;
	case olive::NodeValue::k_binary:
		return OAK_NODE_VALUE_BINARY;
	case olive::NodeValue::k_bezier:
		return OAK_NODE_VALUE_BEZIER;
	default:
		return OAK_NODE_VALUE_NONE;
}
}

// Wire the node-family events (handle validated as a Node). Appended to
// `conns`; returns false when nothing matched.
bool connect_node_event(olive::Node *node, int32_t event_id,
						oakengine_event_fn fn, void *userdata,
						QVector<QMetaObject::Connection> *conns)
{
	using namespace olive;

	switch (event_id) {
	case OAKENGINE_EVENT_NODE_LABEL_CHANGED:
		conns->append(QObject::connect(
			node, &Node::label_changed, node,
			[fn, userdata, node](const QString &label) {
				const QByteArray utf = label.toUtf8();
				invoke(fn, userdata, OAKENGINE_EVENT_NODE_LABEL_CHANGED, node,
					   0, 0, nullptr, 0, utf.constData());
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_INPUT_VALUE_CHANGED:
		conns->append(QObject::connect(
			node, &Node::value_changed, node,
			[fn, userdata, node](const NodeInput &input,
								 const TimeRange &range) {
				const Rational tb = node_frame_time_base(node);
				const QByteArray utf = input.input().toUtf8();
				invoke(fn, userdata,
					   OAKENGINE_EVENT_NODE_INPUT_VALUE_CHANGED, node,
					   input.element(),
					   core::Timecode::time_to_timestamp(
						   range.in(), tb, core::Timecode::k_round),
					   nullptr,
					   core::Timecode::time_to_timestamp(
						   range.out(), tb, core::Timecode::k_round),
					   utf.constData());
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_INPUT_CONNECTED:
	case OAKENGINE_EVENT_NODE_INPUT_DISCONNECTED: {
		const bool connected =
			event_id == OAKENGINE_EVENT_NODE_INPUT_CONNECTED;
		auto deliver = [fn, userdata, node, connected, event_id](
						   Node *output, const NodeInput &input) {
			const QByteArray utf = input.input().toUtf8();
			invoke(fn, userdata, event_id, node, input.element(), 0, output, 0,
				   utf.constData());
		};
		if (connected) {
			conns->append(QObject::connect(node, &Node::input_connected, node,
										   deliver, Qt::DirectConnection));
		} else {
			conns->append(QObject::connect(node, &Node::input_disconnected,
										   node, deliver,
										   Qt::DirectConnection));
		}
		return true;
	}
	case OAKENGINE_EVENT_NODE_INPUT_FLAGS_CHANGED:
		conns->append(QObject::connect(
			node, &Node::input_flags_changed, node,
			[fn, userdata, node](const QString &input,
								 const InputFlags &flags) {
				const QByteArray utf = input.toUtf8();
				invoke(fn, userdata, OAKENGINE_EVENT_NODE_INPUT_FLAGS_CHANGED,
					   node, int64_t(flags.value()), 0, nullptr, 0,
					   utf.constData());
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_INPUT_PROPERTY_CHANGED:
		conns->append(QObject::connect(
			node, &Node::input_property_changed, node,
			[fn, userdata, node](const QString &input, const QString &,
								 const QVariant &) {
				const QByteArray utf = input.toUtf8();
				invoke(fn, userdata,
					   OAKENGINE_EVENT_NODE_INPUT_PROPERTY_CHANGED, node, 0, 0,
					   nullptr, 0, utf.constData());
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_INPUT_DATA_TYPE_CHANGED:
		conns->append(QObject::connect(
			node, &Node::input_data_type_changed, node,
			[fn, userdata, node](const QString &input, NodeValue::Type type) {
				const QByteArray utf = input.toUtf8();
				invoke(fn, userdata,
					   OAKENGINE_EVENT_NODE_INPUT_DATA_TYPE_CHANGED, node,
					   node_value_type_to_c(type), 0, nullptr, 0,
					   utf.constData());
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED:
		conns->append(QObject::connect(
			node, &Node::input_array_size_changed, node,
			[fn, userdata, node](const QString &input, int old_size,
								 int new_size) {
				const QByteArray utf = input.toUtf8();
				invoke(fn, userdata,
					   OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED, node,
					   old_size, new_size, nullptr, 0, utf.constData());
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED:
		conns->append(QObject::connect(
			node, &Node::keyframe_enable_changed, node,
			[fn, userdata, node](const NodeInput &input, bool enabled) {
				const QByteArray utf = input.input().toUtf8();
				invoke(fn, userdata,
					   OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED, node,
					   input.element(), enabled ? 1 : 0, nullptr, 0,
					   utf.constData());
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_KEYFRAME_ADDED:
	case OAKENGINE_EVENT_NODE_KEYFRAME_REMOVED: {
		auto deliver = [fn, userdata, node, event_id](OakEngineKeyframe *k) {
			auto *key = reinterpret_cast<NodeKeyframe *>(k);
			const QByteArray utf = key->input().toUtf8();
			invoke(fn, userdata, event_id, node, key->element(), key->track(),
				   k, 0, utf.constData());
		};
		if (event_id == OAKENGINE_EVENT_NODE_KEYFRAME_ADDED) {
			conns->append(QObject::connect(node, &Node::keyframe_added, node,
										   deliver, Qt::DirectConnection));
		} else {
			conns->append(QObject::connect(node, &Node::keyframe_removed,
										   node, deliver,
										   Qt::DirectConnection));
		}
		return true;
	}
	case OAKENGINE_EVENT_NODE_KEYFRAME_TIME_CHANGED:
	case OAKENGINE_EVENT_NODE_KEYFRAME_TYPE_CHANGED:
	case OAKENGINE_EVENT_NODE_KEYFRAME_VALUE_CHANGED: {
		auto deliver = [fn, userdata, node, event_id](OakEngineKeyframe *k) {
			invoke(fn, userdata, event_id, node, 0, 0, k);
		};
		if (event_id == OAKENGINE_EVENT_NODE_KEYFRAME_TIME_CHANGED) {
			conns->append(QObject::connect(node, &Node::keyframe_time_changed,
										   node, deliver,
										   Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_NODE_KEYFRAME_TYPE_CHANGED) {
			conns->append(QObject::connect(node, &Node::keyframe_type_changed,
										   node, deliver,
										   Qt::DirectConnection));
		} else {
			conns->append(QObject::connect(node,
										   &Node::keyframe_value_changed,
										   node, deliver,
										   Qt::DirectConnection));
		}
		return true;
	}
	case OAKENGINE_EVENT_NODE_NODE_ADDED_TO_CONTEXT:
	case OAKENGINE_EVENT_NODE_NODE_REMOVED_FROM_CONTEXT: {
		auto deliver = [fn, userdata, node, event_id](Node *child) {
			invoke(fn, userdata, event_id, node, 0, 0, child);
		};
		if (event_id == OAKENGINE_EVENT_NODE_NODE_ADDED_TO_CONTEXT) {
			conns->append(QObject::connect(node, &Node::node_added_to_context,
										   node, deliver,
										   Qt::DirectConnection));
		} else {
			conns->append(QObject::connect(node,
										   &Node::node_removed_from_context,
										   node, deliver,
										   Qt::DirectConnection));
		}
		return true;
	}
	case OAKENGINE_EVENT_NODE_MESSAGE_COUNT_CHANGED:
		conns->append(QObject::connect(
			node, &Node::message_count_changed, node,
			[fn, userdata, node]() {
				invoke(fn, userdata, OAKENGINE_EVENT_NODE_MESSAGE_COUNT_CHANGED,
					   node, 0, 0, nullptr);
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED:
	case OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_REMOVED: {
		auto *group = dynamic_cast<NodeGroup *>(node);
		if (!group) {
			return false;
		}
		auto deliver = [fn, userdata, node, event_id](NodeGroup *,
													const NodeInput &input) {
			const QByteArray id = input.input().toUtf8();
			invoke(fn, userdata, event_id, node, input.element(), 0,
				   input.node(), 0, id.constData());
		};
		if (event_id == OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED) {
			conns->append(QObject::connect(
				group, &NodeGroup::input_passthrough_added, group, deliver,
				Qt::DirectConnection));
		} else {
			conns->append(QObject::connect(
				group, &NodeGroup::input_passthrough_removed, group, deliver,
				Qt::DirectConnection));
		}
		return true;
	}
	case OAKENGINE_EVENT_GROUP_OUTPUT_PASSTHROUGH_CHANGED: {
		auto *group = dynamic_cast<NodeGroup *>(node);
		if (!group) {
			return false;
		}
		conns->append(QObject::connect(
			group, &NodeGroup::output_passthrough_changed, group,
			[fn, userdata, node](NodeGroup *, Node *output) {
				invoke(fn, userdata,
					   OAKENGINE_EVENT_GROUP_OUTPUT_PASSTHROUGH_CHANGED, node,
					   0, 0, output);
			},
			Qt::DirectConnection));
		return true;
	}
	case OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED:
		conns->append(QObject::connect(
			node, &Node::node_position_in_context_changed, node,
			[fn, userdata, node](Node *child, const QPointF &pos) {
				int64_t xb, yb;
				const double x = pos.x(), y = pos.y();
				memcpy(&xb, &x, sizeof(xb));
				memcpy(&yb, &y, sizeof(yb));
				invoke(fn, userdata,
					   OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED, node, xb,
					   yb, child);
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_LINKS_CHANGED:
		conns->append(QObject::connect(
			node, &Node::links_changed, node,
			[fn, userdata, node]() {
				invoke(fn, userdata, OAKENGINE_EVENT_NODE_LINKS_CHANGED, node,
					   0, 0, nullptr);
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_COLOR_CHANGED:
		conns->append(QObject::connect(
			node, &Node::color_changed, node,
			[fn, userdata, node]() {
				invoke(fn, userdata, OAKENGINE_EVENT_NODE_COLOR_CHANGED, node,
					   0, 0, nullptr);
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_INPUT_ADDED:
		conns->append(QObject::connect(
			node, &Node::input_added, node,
			[fn, userdata, node](const QString &id) {
				QByteArray utf = id.toUtf8();
				invoke(fn, userdata, OAKENGINE_EVENT_NODE_INPUT_ADDED, node,
					   0, 0, nullptr, 0, utf.constData());
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_INPUT_REMOVED:
		conns->append(QObject::connect(
			node, &Node::input_removed, node,
			[fn, userdata, node](const QString &id) {
				QByteArray utf = id.toUtf8();
				invoke(fn, userdata, OAKENGINE_EVENT_NODE_INPUT_REMOVED, node,
					   0, 0, nullptr, 0, utf.constData());
			},
			Qt::DirectConnection));
		return true;
	case OAKENGINE_EVENT_NODE_REMOVED_FROM_GRAPH:
		conns->append(QObject::connect(
			node, &Node::removed_from_graph, node,
			[fn, userdata, node](olive::Project *project) {
				invoke(fn, userdata, OAKENGINE_EVENT_NODE_REMOVED_FROM_GRAPH,
					   node,
					   0, 0, reinterpret_cast<void *>(project));
			},
			Qt::DirectConnection));
		return true;
	default:
		return false;
	}
}

// The sequence's frame duration as a Rational timebase, like timeline.cpp.
bool time_base_of(const olive::Sequence *s, olive::Rational *out)
{
	const olive::Rational frame_rate = s->get_video_params().frame_rate();
	if (frame_rate.isNull() || frame_rate.isNaN()) {
		return false;
	}
	*out = frame_rate.flipped();
	return true;
}

int64_t time_to_ts(const olive::Rational &time, const olive::Rational &tb)
{
	return olive::core::Timecode::time_to_timestamp(
		time, tb, olive::core::Timecode::k_round);
}

// Block range as frame timestamps in the track's sequence timebase; -1/-1
// when the block is not on a sequenced track at emission time.
void block_timestamps(const olive::Block *block, int64_t *in_ts,
					  int64_t *out_ts)
{
	*in_ts = -1;
	*out_ts = -1;
	if (!block || !block->track() || !block->track()->sequence()) {
		return;
	}
	olive::Rational tb;
	if (!time_base_of(block->track()->sequence(), &tb)) {
		return;
	}
	*in_ts = time_to_ts(block->in(), tb);
	*out_ts = time_to_ts(block->out(), tb);
}

int64_t marker_timestamp(const olive::Sequence *seq,
						 const olive::TimelineMarker *marker)
{
	olive::Rational tb;
	if (!marker || !time_base_of(seq, &tb)) {
		return -1;
	}
	return time_to_ts(marker->time().in(), tb);
}

// Wire the connections for one subscription. `obj` is the validated engine
// object (already cast-checked). Returns the connection list, empty when
// the event family does not match `obj`.
QVector<QMetaObject::Connection> connect_event(
	QObject *obj, int32_t event_id, oakengine_event_fn fn, void *userdata)
{
	using namespace olive;

	QVector<QMetaObject::Connection> conns;

	switch (event_id) {
	case OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED: {
		auto *project = dynamic_cast<Project *>(obj);
		if (!project) {
			break;
		}
		conns.append(QObject::connect(
			project, &Project::modified_changed, project,
			[fn, userdata, project](bool modified) {
				invoke(fn, userdata, OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED,
					   project, modified ? 1 : 0, 0, nullptr);
			},
			Qt::DirectConnection));
		break;
	}
	case OAKENGINE_EVENT_PROJECT_NAME_CHANGED: {
		auto *project = dynamic_cast<Project *>(obj);
		if (!project) {
			break;
		}
		conns.append(QObject::connect(
			project, &Project::name_changed, project,
			[fn, userdata, project]() {
				invoke(fn, userdata, OAKENGINE_EVENT_PROJECT_NAME_CHANGED,
					   project, 0, 0, nullptr);
			},
			Qt::DirectConnection));
		break;
	}
	case OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM:
	case OAKENGINE_EVENT_FOLDER_END_INSERT_ITEM:
	case OAKENGINE_EVENT_FOLDER_BEGIN_REMOVE_ITEM:
	case OAKENGINE_EVENT_FOLDER_END_REMOVE_ITEM: {
		auto *folder = dynamic_cast<Folder *>(obj);
		if (!folder) {
			break;
		}
		if (event_id == OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM) {
			conns.append(QObject::connect(
				folder, &Folder::begin_insert_item, folder,
				[fn, userdata, folder](Node *child, int index) {
					invoke(fn, userdata, OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM,
						   folder, index, 0, child);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_FOLDER_END_INSERT_ITEM) {
			conns.append(QObject::connect(
				folder, &Folder::end_insert_item, folder,
				[fn, userdata, folder]() {
					invoke(fn, userdata, OAKENGINE_EVENT_FOLDER_END_INSERT_ITEM,
						   folder, 0, 0, nullptr);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_FOLDER_BEGIN_REMOVE_ITEM) {
			conns.append(QObject::connect(
				folder, &Folder::begin_remove_item, folder,
				[fn, userdata, folder](Node *child, int index) {
					invoke(fn, userdata, OAKENGINE_EVENT_FOLDER_BEGIN_REMOVE_ITEM,
						   folder, index, 0, child);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				folder, &Folder::end_remove_item, folder,
				[fn, userdata, folder]() {
					invoke(fn, userdata, OAKENGINE_EVENT_FOLDER_END_REMOVE_ITEM,
						   folder, 0, 0, nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED:
	case OAKENGINE_EVENT_SEQUENCE_TRACK_REMOVED: {
		auto *seq = dynamic_cast<Sequence *>(obj);
		if (!seq) {
			break;
		}
		if (event_id == OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED) {
			conns.append(QObject::connect(
				seq, &Sequence::track_added, seq,
				[fn, userdata, seq](Track *track) {
					invoke(fn, userdata, OAKENGINE_EVENT_SEQUENCE_TRACK_ADDED,
						   seq, track ? int(track->type()) : -1, 0, track);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				seq, &Sequence::track_removed, seq,
				[fn, userdata, seq](Track *track) {
					invoke(fn, userdata, OAKENGINE_EVENT_SEQUENCE_TRACK_REMOVED,
						   seq, track ? int(track->type()) : -1, 0, track);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_TRACK_BLOCK_ADDED:
	case OAKENGINE_EVENT_TRACK_BLOCK_REMOVED: {
		auto *track = dynamic_cast<Track *>(obj);
		if (!track) {
			break;
		}
		if (event_id == OAKENGINE_EVENT_TRACK_BLOCK_ADDED) {
			conns.append(QObject::connect(
				track, &Track::block_added, track,
				[fn, userdata, track](Block *block) {
					int64_t in_ts, out_ts;
					block_timestamps(block, &in_ts, &out_ts);
					invoke(fn, userdata, OAKENGINE_EVENT_TRACK_BLOCK_ADDED,
						   track, in_ts, out_ts, block);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				track, &Track::block_removed, track,
				[fn, userdata, track](Block *block) {
					int64_t in_ts, out_ts;
					block_timestamps(block, &in_ts, &out_ts);
					invoke(fn, userdata, OAKENGINE_EVENT_TRACK_BLOCK_REMOVED,
						   track, in_ts, out_ts, block);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_TRACK_INDEX_CHANGED:
	case OAKENGINE_EVENT_TRACK_HEIGHT_CHANGED:
	case OAKENGINE_EVENT_TRACK_BLOCKS_REFRESHED:
	case OAKENGINE_EVENT_TRACK_MUTED_CHANGED: {
		auto *track = dynamic_cast<Track *>(obj);
		if (!track) {
			break;
		}
		if (event_id == OAKENGINE_EVENT_TRACK_INDEX_CHANGED) {
			conns.append(QObject::connect(
				track, &Track::index_changed, track,
				[fn, userdata, track](int old_index, int new_index) {
					invoke(fn, userdata, OAKENGINE_EVENT_TRACK_INDEX_CHANGED,
						   track, old_index, new_index, nullptr);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_TRACK_HEIGHT_CHANGED) {
			conns.append(QObject::connect(
				track, &Track::track_height_changed, track,
				[fn, userdata, track](qreal height) {
					int64_t bits;
					const double h = double(height);
					memcpy(&bits, &h, sizeof(bits));
					invoke(fn, userdata, OAKENGINE_EVENT_TRACK_HEIGHT_CHANGED,
						   track, bits, 0, nullptr);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_TRACK_BLOCKS_REFRESHED) {
			conns.append(QObject::connect(
				track, &Track::blocks_refreshed, track,
				[fn, userdata, track]() {
					invoke(fn, userdata, OAKENGINE_EVENT_TRACK_BLOCKS_REFRESHED,
						   track, 0, 0, nullptr);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				track, &Track::muted_changed, track,
				[fn, userdata, track](bool muted) {
					invoke(fn, userdata, OAKENGINE_EVENT_TRACK_MUTED_CHANGED,
						   track, muted ? 1 : 0, 0, nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_BLOCK_ENABLED_CHANGED:
	case OAKENGINE_EVENT_BLOCK_PREVIEW_CHANGED: {
		auto *block = dynamic_cast<Block *>(obj);
		if (!block) {
			break;
		}
		if (event_id == OAKENGINE_EVENT_BLOCK_ENABLED_CHANGED) {
			conns.append(QObject::connect(
				block, &Block::enabled_changed, block,
				[fn, userdata, block]() {
					invoke(fn, userdata, OAKENGINE_EVENT_BLOCK_ENABLED_CHANGED,
						   block, 0, 0, nullptr);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				block, &Block::preview_changed, block,
				[fn, userdata, block]() {
					invoke(fn, userdata, OAKENGINE_EVENT_BLOCK_PREVIEW_CHANGED,
						   block, 0, 0, nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED:
	case OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED: {
		auto *seq = dynamic_cast<Sequence *>(obj);
		if (!seq) {
			break;
		}
		for (int type = 0; type < 3; type++) {
			TrackList *list = seq->track_list(static_cast<Track::Type>(type));
			if (!list) {
				continue;
			}
			if (event_id == OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED) {
				conns.append(QObject::connect(
					list, &TrackList::track_list_changed, seq,
					[fn, userdata, seq, type]() {
						invoke(fn, userdata,
							   OAKENGINE_EVENT_SEQUENCE_TRACK_LIST_CHANGED, seq,
							   type, 0, nullptr);
					},
					Qt::DirectConnection));
			} else {
				conns.append(QObject::connect(
					list, &TrackList::track_height_changed, seq,
					[fn, userdata, seq, type](Track *track, int height) {
						invoke(fn, userdata,
							   OAKENGINE_EVENT_SEQUENCE_TRACK_HEIGHT_CHANGED,
							   seq, type, height, track);
					},
					Qt::DirectConnection));
			}
		}
		break;
	}
	case OAKENGINE_EVENT_SEQUENCE_SUBTITLES_CHANGED: {
		auto *seq = dynamic_cast<Sequence *>(obj);
		if (!seq) {
			break;
		}
		conns.append(QObject::connect(
			seq, &Sequence::subtitles_changed, seq,
			[fn, userdata, seq](const TimeRange &range) {
				olive::Rational tb;
				int64_t in_ts = -1, out_ts = -1;
				if (time_base_of(seq, &tb)) {
					in_ts = time_to_ts(range.in(), tb);
					out_ts = time_to_ts(range.out(), tb);
				}
				invoke(fn, userdata, OAKENGINE_EVENT_SEQUENCE_SUBTITLES_CHANGED,
					   seq, in_ts, out_ts, nullptr);
			},
			Qt::DirectConnection));
		break;
	}
	case OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED:
	case OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED:
	case OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED: {
		auto *markers = dynamic_cast<TimelineMarkerList *>(obj);
		if (!markers) {
			break;
		}
		auto deliver = [fn, userdata, markers, event_id](
						   TimelineMarker *marker) {
			invoke(fn, userdata, event_id, markers, 0, 0, marker);
		};
		if (event_id == OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED) {
			conns.append(QObject::connect(markers,
										  &TimelineMarkerList::marker_added,
										  markers, deliver,
										  Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED) {
			conns.append(QObject::connect(markers,
										  &TimelineMarkerList::marker_removed,
										  markers, deliver,
										  Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(markers,
										  &TimelineMarkerList::marker_modified,
										  markers, deliver,
										  Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED:
	case OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED: {
		auto *workarea = dynamic_cast<TimelineWorkArea *>(obj);
		if (!workarea) {
			break;
		}
		if (event_id == OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED) {
			conns.append(QObject::connect(
				workarea, &TimelineWorkArea::range_changed, workarea,
				[fn, userdata, workarea](const TimeRange &) {
					invoke(fn, userdata, OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED,
						   workarea, 0, 0, nullptr);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				workarea, &TimelineWorkArea::enabled_changed, workarea,
				[fn, userdata, workarea](bool enabled) {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED, workarea,
						   enabled ? 1 : 0, 0, nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_SEQUENCE_MARKER_ADDED:
	case OAKENGINE_EVENT_SEQUENCE_MARKER_REMOVED:
	case OAKENGINE_EVENT_SEQUENCE_MARKER_MODIFIED: {
		auto *seq = dynamic_cast<Sequence *>(obj);
		if (!seq) {
			break;
		}
		TimelineMarkerList *markers = seq->get_markers();
		if (event_id == OAKENGINE_EVENT_SEQUENCE_MARKER_ADDED) {
			conns.append(QObject::connect(
				markers, &TimelineMarkerList::marker_added, seq,
				[fn, userdata, seq](TimelineMarker *marker) {
					invoke(fn, userdata, OAKENGINE_EVENT_SEQUENCE_MARKER_ADDED,
						   seq, marker_timestamp(seq, marker), 0, nullptr);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_SEQUENCE_MARKER_REMOVED) {
			conns.append(QObject::connect(
				markers, &TimelineMarkerList::marker_removed, seq,
				[fn, userdata, seq](TimelineMarker *marker) {
					invoke(fn, userdata, OAKENGINE_EVENT_SEQUENCE_MARKER_REMOVED,
						   seq, marker_timestamp(seq, marker), 0, nullptr);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				markers, &TimelineMarkerList::marker_modified, seq,
				[fn, userdata, seq](TimelineMarker *marker) {
					invoke(fn, userdata, OAKENGINE_EVENT_SEQUENCE_MARKER_MODIFIED,
						   seq, marker_timestamp(seq, marker), 0, nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_SEQUENCE_WORKAREA_RANGE_CHANGED:
	case OAKENGINE_EVENT_SEQUENCE_WORKAREA_ENABLED_CHANGED: {
		auto *seq = dynamic_cast<Sequence *>(obj);
		if (!seq) {
			break;
		}
		TimelineWorkArea *workarea = seq->get_work_area();
		if (event_id == OAKENGINE_EVENT_SEQUENCE_WORKAREA_RANGE_CHANGED) {
			conns.append(QObject::connect(
				workarea, &TimelineWorkArea::range_changed, seq,
				[fn, userdata, seq](const TimeRange &range) {
					olive::Rational tb;
					int64_t in_ts = -1, out_ts = -1;
					if (time_base_of(seq, &tb)) {
						in_ts = time_to_ts(range.in(), tb);
						out_ts = time_to_ts(range.out(), tb);
					}
					invoke(fn, userdata,
						   OAKENGINE_EVENT_SEQUENCE_WORKAREA_RANGE_CHANGED, seq,
						   in_ts, out_ts, nullptr);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				workarea, &TimelineWorkArea::enabled_changed, seq,
				[fn, userdata, seq](bool enabled) {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_SEQUENCE_WORKAREA_ENABLED_CHANGED,
						   seq, enabled ? 1 : 0, 0, nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED:
	case OAKENGINE_EVENT_COLOR_MANAGER_REFERENCE_SPACE_CHANGED: {
		auto *cm = dynamic_cast<ColorManager *>(obj);
		if (!cm) {
			break;
		}
		if (event_id == OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED) {
			conns.append(QObject::connect(
				cm, &ColorManager::config_changed, cm,
				[fn, userdata, cm](const QString &) {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED, cm, 0,
						   0, nullptr);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				cm, &ColorManager::reference_space_changed, cm,
				[fn, userdata, cm](const QString &) {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_COLOR_MANAGER_REFERENCE_SPACE_CHANGED,
						   cm, 0, 0, nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_VIEWER_LENGTH_CHANGED:
	case OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED:
	case OAKENGINE_EVENT_VIEWER_FRAME_RATE_CHANGED:
	case OAKENGINE_EVENT_VIEWER_SIZE_CHANGED:
	case OAKENGINE_EVENT_VIEWER_PIXEL_ASPECT_CHANGED:
	case OAKENGINE_EVENT_VIEWER_INTERLACING_CHANGED:
	case OAKENGINE_EVENT_VIEWER_VIDEO_PARAMS_CHANGED:
	case OAKENGINE_EVENT_VIEWER_AUDIO_PARAMS_CHANGED:
	case OAKENGINE_EVENT_VIEWER_TEXTURE_INPUT_CHANGED:
	case OAKENGINE_EVENT_VIEWER_SAMPLE_RATE_CHANGED:
	case OAKENGINE_EVENT_VIEWER_CONNECTED_WAVEFORM_CHANGED: {
		auto *viewer = dynamic_cast<ViewerOutput *>(obj);
		if (!viewer) {
			break;
		}
		// Rational payloads are a = numerator, b = denominator (seconds).
		auto deliver_rational = [fn, userdata, viewer, event_id](
									const Rational &r) {
			invoke(fn, userdata, event_id, viewer, r.numerator(),
				   r.denominator(), nullptr);
		};
		if (event_id == OAKENGINE_EVENT_VIEWER_LENGTH_CHANGED) {
			conns.append(QObject::connect(viewer, &ViewerOutput::length_changed,
										  viewer, deliver_rational,
										  Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED) {
			conns.append(QObject::connect(viewer,
										  &ViewerOutput::playhead_changed,
										  viewer, deliver_rational,
										  Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_VIEWER_FRAME_RATE_CHANGED) {
			conns.append(QObject::connect(viewer,
										  &ViewerOutput::frame_rate_changed,
										  viewer, deliver_rational,
										  Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_VIEWER_SIZE_CHANGED) {
			conns.append(QObject::connect(
				viewer, &ViewerOutput::size_changed, viewer,
				[fn, userdata, viewer](int width, int height) {
					invoke(fn, userdata, OAKENGINE_EVENT_VIEWER_SIZE_CHANGED,
						   viewer, width, height, nullptr);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_VIEWER_PIXEL_ASPECT_CHANGED) {
			conns.append(QObject::connect(viewer,
										  &ViewerOutput::pixel_aspect_changed,
										  viewer, deliver_rational,
										  Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_VIEWER_INTERLACING_CHANGED) {
			conns.append(QObject::connect(
				viewer, &ViewerOutput::interlacing_changed, viewer,
				[fn, userdata, viewer](VideoParams::Interlacing mode) {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_VIEWER_INTERLACING_CHANGED, viewer,
						   int64_t(mode), 0, nullptr);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_VIEWER_VIDEO_PARAMS_CHANGED) {
			conns.append(QObject::connect(
				viewer, &ViewerOutput::video_params_changed, viewer,
				[fn, userdata, viewer]() {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_VIEWER_VIDEO_PARAMS_CHANGED, viewer,
						   0, 0, nullptr);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_VIEWER_AUDIO_PARAMS_CHANGED) {
			conns.append(QObject::connect(
				viewer, &ViewerOutput::audio_params_changed, viewer,
				[fn, userdata, viewer]() {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_VIEWER_AUDIO_PARAMS_CHANGED, viewer,
						   0, 0, nullptr);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_VIEWER_TEXTURE_INPUT_CHANGED) {
			conns.append(QObject::connect(
				viewer, &ViewerOutput::texture_input_changed, viewer,
				[fn, userdata, viewer]() {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_VIEWER_TEXTURE_INPUT_CHANGED, viewer,
						   0, 0, nullptr);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_VIEWER_SAMPLE_RATE_CHANGED) {
			conns.append(QObject::connect(
				viewer, &ViewerOutput::sample_rate_changed, viewer,
				[fn, userdata, viewer](int sr) {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_VIEWER_SAMPLE_RATE_CHANGED, viewer,
						   sr, 0, nullptr);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				viewer, &ViewerOutput::connected_waveform_changed, viewer,
				[fn, userdata, viewer]() {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_VIEWER_CONNECTED_WAVEFORM_CHANGED,
						   viewer, 0, 0, nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_ADDED:
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED:
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_FAILED:
	case OAKENGINE_EVENT_TASK_MANAGER_LIST_CHANGED: {
		auto *manager = dynamic_cast<TaskManager *>(obj);
		if (!manager) {
			break;
		}
		if (event_id == OAKENGINE_EVENT_TASK_MANAGER_TASK_ADDED) {
			conns.append(QObject::connect(
				manager, &TaskManager::task_added, manager,
				[fn, userdata, manager](Task *t) {
					const QByteArray title = t->get_title().toUtf8();
					invoke(fn, userdata,
						   OAKENGINE_EVENT_TASK_MANAGER_TASK_ADDED, manager, 0, 0,
						   t, 0, title.constData());
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED) {
			conns.append(QObject::connect(
				manager, &TaskManager::task_removed, manager,
				[fn, userdata, manager](Task *t) {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED, manager, 0,
						   0, t);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_TASK_MANAGER_TASK_FAILED) {
			conns.append(QObject::connect(
				manager, &TaskManager::task_failed, manager,
				[fn, userdata, manager](Task *t) {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_TASK_MANAGER_TASK_FAILED, manager, 0,
						   0, t);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				manager, &TaskManager::task_list_changed, manager,
				[fn, userdata, manager]() {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_TASK_MANAGER_LIST_CHANGED, manager, 0,
						   0, nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_TASK_STARTED:
	case OAKENGINE_EVENT_TASK_PROGRESS:
	case OAKENGINE_EVENT_TASK_FINISHED: {
		auto *task = dynamic_cast<Task *>(obj);
		if (!task) {
			break;
		}
		if (event_id == OAKENGINE_EVENT_TASK_STARTED) {
			conns.append(QObject::connect(
				task, &Task::started, task,
				[fn, userdata, task](qint64 start_time) {
					invoke(fn, userdata, OAKENGINE_EVENT_TASK_STARTED, task,
						   start_time, 0, nullptr);
				},
				Qt::DirectConnection));
		} else if (event_id == OAKENGINE_EVENT_TASK_PROGRESS) {
			conns.append(QObject::connect(
				task, &Task::progress_changed, task,
				[fn, userdata, task](double d) {
					int64_t bits;
					static_assert(sizeof(bits) == sizeof(d));
					memcpy(&bits, &d, sizeof(bits));
					invoke(fn, userdata, OAKENGINE_EVENT_TASK_PROGRESS, task,
						   bits, 0, nullptr);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				task, &Task::finished, task,
				[fn, userdata](Task *t, bool succeeded) {
					invoke(fn, userdata, OAKENGINE_EVENT_TASK_FINISHED, t,
						   succeeded ? 1 : 0, 0, nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_UNDO_INDEX_CHANGED: {
		auto *undo_stack = dynamic_cast<UndoStack *>(obj);
		if (!undo_stack) {
			break;
		}
		conns.append(QObject::connect(
			undo_stack, &UndoStack::index_changed, undo_stack,
			[fn, userdata, undo_stack](int i) {
				invoke(fn, userdata, OAKENGINE_EVENT_UNDO_INDEX_CHANGED,
					   undo_stack, i, 0, nullptr);
			},
			Qt::DirectConnection));
		break;
	}
	case OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED: {
		auto *audio_manager = dynamic_cast<AudioManager *>(obj);
		if (!audio_manager) {
			break;
		}
		conns.append(QObject::connect(
			audio_manager, &AudioManager::output_params_changed, audio_manager,
			[fn, userdata, audio_manager]() {
				invoke(fn, userdata,
					   OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED,
					   audio_manager, 0, 0, nullptr);
			},
			Qt::DirectConnection));
		break;
	}
	case OAKENGINE_EVENT_PLAYBACK_CACHE_INVALIDATED:
	case OAKENGINE_EVENT_PLAYBACK_CACHE_VALIDATED: {
		auto *cache = dynamic_cast<PlaybackCache *>(obj);
		if (!cache) {
			break;
		}
		if (event_id == OAKENGINE_EVENT_PLAYBACK_CACHE_INVALIDATED) {
			conns.append(QObject::connect(
				cache, &PlaybackCache::invalidated, cache,
				[fn, userdata, cache](const TimeRange &r) {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_PLAYBACK_CACHE_INVALIDATED,
						   cache, r.in().numerator(), r.in().denominator(),
						   nullptr);
				},
				Qt::DirectConnection));
		} else {
			conns.append(QObject::connect(
				cache, &PlaybackCache::validated, cache,
				[fn, userdata, cache](const TimeRange &r) {
					invoke(fn, userdata,
						   OAKENGINE_EVENT_PLAYBACK_CACHE_VALIDATED,
						   cache, r.in().numerator(), r.in().denominator(),
						   nullptr);
				},
				Qt::DirectConnection));
		}
		break;
	}
	case OAKENGINE_EVENT_FRAME_CACHE_INVALIDATED: {
		auto *cache = dynamic_cast<FrameHashCache *>(obj);
		if (!cache) {
			break;
		}
		conns.append(QObject::connect(
			cache, &PlaybackCache::invalidated, cache,
			[fn, userdata, cache](const TimeRange &r) {
				invoke(fn, userdata,
					   OAKENGINE_EVENT_FRAME_CACHE_INVALIDATED,
					   cache, r.in().numerator(), r.in().denominator(),
					   nullptr);
			},
			Qt::DirectConnection));
		break;
	}
	default:
		break;
	}

	if (conns.isEmpty()) {
		if (auto *node = dynamic_cast<Node *>(obj)) {
			connect_node_event(node, event_id, fn, userdata, &conns);
		}
	}

	return conns;
}

} // namespace

extern "C" int64_t oakengine_event_subscribe(void *handle, int32_t event_id,
											 oakengine_event_fn fn,
											 void *userdata)
{
	if (!handle || !fn) {
		return 0;
	}

	// Every facade handle is the engine QObject pointer itself (see
	// timeline.cpp/project.cpp wrap()); dynamic_cast from QObject* both
	// validates the family match and is safe across the Node/Project split.
	auto *obj = reinterpret_cast<QObject *>(handle);

	QVector<QMetaObject::Connection> conns =
		connect_event(obj, event_id, fn, userdata);
	if (conns.isEmpty()) {
		return 0;
	}

	// Drop the registry entry automatically when the observed object dies so
	// a stale subscription id is never a dangling engine pointer. Qt removes
	// the signal connections itself; only the map entry needs cleanup.
	const int64_t id = g_next_id.fetch_add(1);
	conns.append(QObject::connect(obj, &QObject::destroyed, obj,
								  [id]() { drop_subscription(id); },
								  Qt::DirectConnection));

	QMutexLocker locker(&g_registry_mutex);
	g_registry.insert(id, Subscription{std::move(conns)});
	return id;
}

extern "C" int oakengine_event_unsubscribe(int64_t id)
{
	if (id <= 0) {
		return OAKENGINE_E_INVALID;
	}
	QMutexLocker locker(&g_registry_mutex);
	const auto it = g_registry.find(id);
	if (it == g_registry.end()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	for (const QMetaObject::Connection &conn : it->connections) {
		QObject::disconnect(conn);
	}
	g_registry.erase(it);
	return OAKENGINE_OK;
}
