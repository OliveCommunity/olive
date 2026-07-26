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

#include "oakengine/undo.h"

#include <cstring>

#include <QAction>
#include <QByteArray>
#include <QString>
#include <QVector>

#include "olive/core/util/timecodefunctions.h"
#include "coreengine.h"
#include "node/block/block.h"
#include "node/block/clip/clip.h"
#include "node/block/transition/transition.h"
#include "node/nodeundo.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "node/value.h"
#include "timeline/timelinecommon.h"
#include "timeline/timelineundogeneral.h"
#include "timeline/timelineundopointer.h"
#include "timeline/timelineundoripple.h"
#include "timeline/timelineundosplit.h"
#include "undo/undocommand.h"
#include "undo/undostack.h"
#include "undointernal.h"

namespace
{

olive::UndoStack *stack()
{
	if (olive::EngineCore *core = olive::EngineCore::instance()) {
		return core->undo_stack();
	}
	return nullptr;
}

// buf/size string writer (same convention as capi/project.cpp).
int write_string(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf8 = s.toUtf8();
	const int len = int(utf8.size());
	if (buf && buf_size > 0) {
		const int n = qMin(len, buf_size - 1);
		std::memcpy(buf, utf8.constData(), size_t(n));
		buf[n] = '\0';
	}
	return len;
}

} // namespace

namespace
{

// Current open undo group.  Owned by this TU; the facade owns it between
// group_begin and group_end/group_abort.
olive::MultiUndoCommand *g_undo_group = nullptr;
QString g_undo_group_name;

} // namespace

olive::MultiUndoCommand *oakengine_undo_group_current(void)
{
	return g_undo_group;
}

// Shared helper used by all capi TU push_or_run() locals.
void oakengine_undo_push_or_run(olive::UndoCommand *command, const QString &name)
{
	if (olive::MultiUndoCommand *group = g_undo_group) {
		group->add_child(command);
		command->redo_now();
	} else if (olive::UndoStack *s = stack()) {
		s->push(command, name);
	} else {
		command->redo_now();
		delete command;
	}
}

extern "C" void *oakengine_undo_handle(void)
{
	return stack();
}

extern "C" int oakengine_undo_push(void *command, const char *name)
{
	if (!command) {
		return OAKENGINE_E_INVALID;
	}
	auto *cmd = static_cast<olive::UndoCommand *>(command);
	const QString label = name ? QString::fromUtf8(name) : QString();
	if (olive::MultiUndoCommand *group = g_undo_group) {
		group->add_child(cmd);
		cmd->redo_now();
	} else if (olive::UndoStack *s = stack()) {
		s->push(cmd, label);
	} else {
		cmd->redo_now();
		delete cmd;
	}
	return OAKENGINE_OK;
}

extern "C" int oakengine_undo_group_begin(const char *name)
{
	if (g_undo_group) {
		return OAKENGINE_E_STATE;
	}
	g_undo_group = new olive::MultiUndoCommand();
	g_undo_group_name = name ? QString::fromUtf8(name) : QString();
	return OAKENGINE_OK;
}

extern "C" int oakengine_undo_group_end(void)
{
	if (!g_undo_group) {
		return OAKENGINE_E_STATE;
	}
	olive::MultiUndoCommand *group = g_undo_group;
	g_undo_group = nullptr;

	QString name = g_undo_group_name;
	g_undo_group_name.clear();

	olive::UndoStack *s = stack();
	if (!s) {
		// No stack: just redo/undo nothing and delete.
		delete group;
		return OAKENGINE_OK;
	}

	// Empty group is discarded by push_pre_executed (mirrors push).
	s->push_pre_executed(group, name);
	return OAKENGINE_OK;
}

extern "C" int oakengine_undo_group_abort(void)
{
	if (!g_undo_group) {
		return OAKENGINE_E_STATE;
	}
	olive::MultiUndoCommand *group = g_undo_group;
	g_undo_group = nullptr;
	g_undo_group_name.clear();

	group->undo_now();
	delete group;
	return OAKENGINE_OK;
}

extern "C" int oakengine_undo_command_redo_now(void *command)
{
	if (!command) {
		return OAKENGINE_E_INVALID;
	}
	static_cast<olive::UndoCommand *>(command)->redo_now();
	return OAKENGINE_OK;
}

extern "C" int oakengine_undo_command_undo_now(void *command)
{
	if (!command) {
		return OAKENGINE_E_INVALID;
	}
	static_cast<olive::UndoCommand *>(command)->undo_now();
	return OAKENGINE_OK;
}

namespace
{

class CustomUndoCommand : public olive::UndoCommand {
public:
	CustomUndoCommand(const QString &name,
					  oakengine_undo_command_redo_fn redo_cb,
					  oakengine_undo_command_undo_fn undo_cb,
					  oakengine_undo_command_free_fn free_cb,
					  void *userdata)
		: name_(name)
		, redo_fn_(redo_cb)
		, undo_fn_(undo_cb)
		, free_fn_(free_cb)
		, userdata_(userdata)
	{
	}

	virtual ~CustomUndoCommand() override
	{
		if (free_fn_) {
			free_fn_(userdata_);
		}
	}

	virtual olive::Project *get_relevant_project() const override
	{
		return nullptr;
	}

protected:
	virtual void redo() override
	{
		if (redo_fn_) {
			redo_fn_(userdata_);
		}
	}

	virtual void undo() override
	{
		if (undo_fn_) {
			undo_fn_(userdata_);
		}
	}

private:
	QString name_;
	oakengine_undo_command_redo_fn redo_fn_;
	oakengine_undo_command_undo_fn undo_fn_;
	oakengine_undo_command_free_fn free_fn_;
	void *userdata_;
};

} // namespace

namespace
{

// Map oak_node_value_type -> olive::NodeValue::Type (mirrors node.cpp).
olive::NodeValue::Type from_c_type(int t)
{
	switch (t) {
	case 0: return olive::NodeValue::k_none;
	case 1: return olive::NodeValue::k_int;
	case 2: return olive::NodeValue::k_float;
	case 3: return olive::NodeValue::k_boolean;
	case 4: return olive::NodeValue::k_rational;
	case 5: return olive::NodeValue::k_color;
	case 6: return olive::NodeValue::k_vec2;
	case 7: return olive::NodeValue::k_vec3;
	case 8: return olive::NodeValue::k_vec4;
	case 9: return olive::NodeValue::k_combo;
	case 10: return olive::NodeValue::k_file;
	case 11: return olive::NodeValue::k_text;
	case 12: return olive::NodeValue::k_font;
	case 13: return olive::NodeValue::k_str_combo;
	case 14: return olive::NodeValue::k_binary;
	case 15: return olive::NodeValue::k_bezier;
	case 16: return olive::NodeValue::k_texture;
	case 17: return olive::NodeValue::k_samples;
	case 18: return olive::NodeValue::k_video_params;
	case 19: return olive::NodeValue::k_audio_params;
	default: return olive::NodeValue::k_none;
	}
}

const olive::Sequence *sequence_from_block(const olive::Block *block)
{
	if (!block || !block->track()) {
		return nullptr;
	}
	return block->track()->sequence();
}

olive::Rational sequence_time_base(const olive::Sequence *seq)
{
	if (seq) {
		const olive::Rational fr = seq->get_video_params().frame_rate();
		if (!fr.isNull() && !fr.isNaN()) {
			return fr.flipped();
		}
	}
	return olive::Rational(1001, 30000);
}

olive::Rational ts_to_time(int64_t ts, const olive::Rational &tb)
{
	return olive::core::Timecode::timestamp_to_time(ts, tb);
}

olive::Timeline::MovementMode to_movement_mode(int mode)
{
	switch (mode) {
	case 1: return olive::Timeline::k_move;
	case 2: return olive::Timeline::k_trim_in;
	case 3: return olive::Timeline::k_trim_out;
	default: return olive::Timeline::k_none;
	}
}

} // namespace

extern "C" void *oakengine_undo_command_create(
		const char *name,
		oakengine_undo_command_redo_fn redo,
		oakengine_undo_command_undo_fn undo,
		oakengine_undo_command_free_fn free_fn,
		void *userdata)
{
	return new CustomUndoCommand(
			name ? QString::fromUtf8(name) : QString(),
			redo, undo, free_fn, userdata);
}

extern "C" void *oakengine_undo_command_create_multi(void)
{
	return new olive::MultiUndoCommand();
}

extern "C" void *oakengine_node_add_command(void *project, void *node)
{
	if (!project || !node) {
		return nullptr;
	}
	return new olive::NodeAddCommand(
		reinterpret_cast<olive::Project *>(project),
		reinterpret_cast<olive::Node *>(node));
}

extern "C" void *oakengine_node_set_position_command(
		void *node, void *context, double x, double y, int expanded)
{
	if (!node || !context) {
		return nullptr;
	}
	return new olive::NodeSetPositionCommand(
		reinterpret_cast<olive::Node *>(node),
		reinterpret_cast<olive::Node *>(context),
		olive::Node::Position(QPointF(x, y), expanded != 0));
}

extern "C" void *oakengine_node_remove_position_command(
		void *node, void *context)
{
	if (!node || !context) {
		return nullptr;
	}
	return new olive::NodeRemovePositionFromContextCommand(
		reinterpret_cast<olive::Node *>(node),
		reinterpret_cast<olive::Node *>(context));
}

extern "C" void *oakengine_node_set_value_hint_command(
		void *node, const char *input, int element, int type, int index,
		const char *tag)
{
	if (!node || !input) {
		return nullptr;
	}
	olive::Node *n = reinterpret_cast<olive::Node *>(node);
	const QString id = QString::fromUtf8(input);
	if (!n->inputs().contains(id)) {
		return nullptr;
	}
	olive::NodeValue::Type nv_type = olive::NodeValue::k_none;
	if (type >= 0) {
		nv_type = from_c_type(type);
		if (nv_type == olive::NodeValue::k_none && type != 0) {
			return nullptr;
		}
	}
	QVector<olive::NodeValue::Type> types;
	if (nv_type != olive::NodeValue::k_none) {
		types.append(nv_type);
	}
	return new olive::NodeSetValueHintCommand(
		n, id, element,
		olive::Node::ValueHint(types, index, QString::fromUtf8(tag ? tag : "")));
}

extern "C" void *oakengine_node_remove_and_disconnect_command(void *node)
{
	if (!node) {
		return nullptr;
	}
	return new olive::NodeRemoveAndDisconnectCommand(
		reinterpret_cast<olive::Node *>(node));
}

extern "C" void *oakengine_track_place_block_command(
		void *track_list, int track_index, void *block, int64_t in_ts)
{
	if (!track_list || !block || in_ts < 0) {
		return nullptr;
	}
	olive::TrackList *list = reinterpret_cast<olive::TrackList *>(track_list);
	olive::Block *b = reinterpret_cast<olive::Block *>(block);
	const olive::Rational tb = sequence_time_base(list->parent());
	return new olive::TrackPlaceBlockCommand(
		list, track_index, b, ts_to_time(in_ts, tb));
}

extern "C" void *oakengine_track_replace_block_with_gap_command(
		void *track, void *block, int handle_transitions)
{
	if (!track || !block) {
		return nullptr;
	}
	return new olive::TrackReplaceBlockWithGapCommand(
		reinterpret_cast<olive::Track *>(track),
		reinterpret_cast<olive::Block *>(block),
		handle_transitions != 0);
}

extern "C" void *oakengine_block_trim_command(
		void *track, void *block, int64_t new_length_num, int64_t new_length_den,
		int movement_mode, int roll_edit)
{
	if (!track || !block || new_length_den == 0 ||
		movement_mode < 0 || movement_mode > 3) {
		return nullptr;
	}
	olive::Track *t = reinterpret_cast<olive::Track *>(track);
	olive::Block *b = reinterpret_cast<olive::Block *>(block);
	auto *cmd = new olive::BlockTrimCommand(
		t, b,
		olive::Rational(static_cast<int>(new_length_num),
						static_cast<int>(new_length_den)),
		to_movement_mode(movement_mode));
	cmd->set_trim_is_a_roll_edit(roll_edit != 0);
	return cmd;
}

extern "C" void *oakengine_transition_remove_command(
		void *transition, int remove_from_graph)
{
	if (!transition) {
		return nullptr;
	}
	return new olive::TransitionRemoveCommand(
		reinterpret_cast<olive::TransitionBlock *>(transition),
		remove_from_graph != 0);
}

extern "C" void *oakengine_track_slide_command(
		void *track, void *const *blocks, int block_count,
		void *in_adjacent, void *out_adjacent,
		int64_t movement_num, int64_t movement_den)
{
	if (!track || !blocks || block_count <= 0 || movement_den == 0) {
		return nullptr;
	}
	olive::Track *t = reinterpret_cast<olive::Track *>(track);
	QList<olive::Block *> block_list;
	block_list.reserve(block_count);
	for (int i = 0; i < block_count; i++) {
		if (!blocks[i]) {
			return nullptr;
		}
		block_list.append(reinterpret_cast<olive::Block *>(blocks[i]));
	}
	return new olive::TrackSlideCommand(
		t, block_list,
		reinterpret_cast<olive::Block *>(in_adjacent),
		reinterpret_cast<olive::Block *>(out_adjacent),
		olive::Rational(static_cast<int>(movement_num),
						static_cast<int>(movement_den)));
}

extern "C" void *oakengine_block_split_preserving_links_command(
		void *const *blocks, int count, int64_t point_ts)
{
	if (!blocks || count <= 0 || point_ts < 0) {
		return nullptr;
	}
	QVector<olive::Block *> block_vec;
	block_vec.reserve(count);
	const olive::Sequence *seq = nullptr;
	for (int i = 0; i < count; i++) {
		if (!blocks[i]) {
			return nullptr;
		}
		olive::Block *b = reinterpret_cast<olive::Block *>(blocks[i]);
		if (!seq) {
			seq = sequence_from_block(b);
		}
		block_vec.append(b);
	}
	const olive::Rational tb = sequence_time_base(seq);
	const olive::Rational point = ts_to_time(point_ts, tb);
	// BlockSplitPreservingLinksCommand takes a list of times, one per block.
	QList<olive::Rational> times;
	times.reserve(count);
	for (int i = 0; i < count; i++) {
		times.append(point);
	}
	return new olive::BlockSplitPreservingLinksCommand(block_vec, times);
}

extern "C" void *oakengine_block_split_get_split(
		void *command, void *block, int time_index)
{
	if (!command || !block) {
		return nullptr;
	}
	auto *cmd = reinterpret_cast<olive::BlockSplitPreservingLinksCommand *>(
		command);
	return cmd->get_split(reinterpret_cast<olive::Block *>(block), time_index);
}

extern "C" void *oakengine_block_resize_with_media_in_command(
		void *block, int64_t length_num, int64_t length_den)
{
	if (!block || length_den == 0) {
		return nullptr;
	}
	olive::Block *b = reinterpret_cast<olive::Block *>(block);
	return new olive::BlockResizeWithMediaInCommand(
		b, olive::Rational(static_cast<int>(length_num),
						   static_cast<int>(length_den)));
}

extern "C" void *oakengine_block_set_media_in_command(
		void *block, int64_t media_in_num, int64_t media_in_den)
{
	if (!block || media_in_den == 0) {
		return nullptr;
	}
	olive::ClipBlock *clip = reinterpret_cast<olive::ClipBlock *>(block);
	return new olive::BlockSetMediaInCommand(
		clip, olive::Rational(static_cast<int>(media_in_num),
							  static_cast<int>(media_in_den)));
}

extern "C" void *oakengine_timeline_ripple_delete_gaps_command(
		void *sequence, const int64_t *range_in_ts, const int64_t *range_out_ts,
		const int *track_types, const int *track_indexes, int range_count)
{
	if (!sequence || !range_in_ts || !range_out_ts ||
		!track_types || !track_indexes || range_count <= 0) {
		return nullptr;
	}
	olive::Sequence *seq = reinterpret_cast<olive::Sequence *>(sequence);
	const olive::Rational tb = sequence_time_base(seq);
	olive::TimelineRippleDeleteGapsAtRegionsCommand::RangeList ranges;
	ranges.reserve(range_count);
	for (int i = 0; i < range_count; i++) {
		if (range_in_ts[i] < 0 || range_out_ts[i] <= range_in_ts[i] ||
			track_types[i] < 0 || track_types[i] > 2 ||
			track_indexes[i] < 0) {
			return nullptr;
		}
		olive::TrackList *list = seq->track_list(
			static_cast<olive::Track::Type>(track_types[i]));
		if (!list || track_indexes[i] >= list->get_track_count()) {
			return nullptr;
		}
		olive::Track *track = list->get_track_at(track_indexes[i]);
		ranges.append(qMakePair(
			track,
			olive::TimeRange(ts_to_time(range_in_ts[i], tb),
							 ts_to_time(range_out_ts[i], tb))));
	}
	return new olive::TimelineRippleDeleteGapsAtRegionsCommand(seq, ranges);
}

extern "C" void *oakengine_track_list_insert_gaps_command(
		void *track_list, int64_t point_num, int64_t point_den,
		int64_t length_num, int64_t length_den)
{
	if (!track_list || point_den == 0 || length_den == 0) {
		return nullptr;
	}
	olive::TrackList *list = reinterpret_cast<olive::TrackList *>(track_list);
	return new olive::TrackListInsertGaps(
		list, olive::Rational(static_cast<int>(point_num),
							  static_cast<int>(point_den)),
		olive::Rational(static_cast<int>(length_num),
						static_cast<int>(length_den)));
}

extern "C" int oakengine_undo_command_multi_add_child(void *multi,
													void *child)
{
	if (!multi || !child) {
		return OAKENGINE_E_INVALID;
	}
	static_cast<olive::MultiUndoCommand *>(multi)->add_child(
			static_cast<olive::UndoCommand *>(child));
	return OAKENGINE_OK;
}

extern "C" int oakengine_undo_command_multi_child_count(void *multi)
{
	if (!multi) {
		return OAKENGINE_E_INVALID;
	}
	return static_cast<olive::MultiUndoCommand *>(multi)->child_count();
}

extern "C" void oakengine_undo_command_free(void *command)
{
	delete static_cast<olive::UndoCommand *>(command);
}

extern "C" int64_t oakengine_undo_count(void)
{
	olive::UndoStack *s = stack();
	return s ? s->command_count() : OAKENGINE_E_INVALID;
}

extern "C" int64_t oakengine_undo_index(void)
{
	olive::UndoStack *s = stack();
	return s ? s->done_count() : OAKENGINE_E_INVALID;
}

extern "C" int oakengine_undo_command_text(int64_t row, char *buf,
										   int buf_size)
{
	olive::UndoStack *s = stack();
	if (!s) {
		return OAKENGINE_E_INVALID;
	}
	if (row < 0 || row >= s->command_count()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	return write_string(s->command_name(row), buf, buf_size);
}

extern "C" int oakengine_undo_command_is_done(int64_t row)
{
	olive::UndoStack *s = stack();
	if (!s) {
		return OAKENGINE_E_INVALID;
	}
	if (row < 0 || row >= s->command_count()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	return s->command_is_done(row) ? 1 : 0;
}

extern "C" int oakengine_undo_jump(int64_t index)
{
	olive::UndoStack *s = stack();
	if (!s) {
		return OAKENGINE_E_STATE;
	}
	if (index < 0 || index > s->command_count()) {
		return OAKENGINE_E_INVALID;
	}
	s->jump(size_t(index));
	return OAKENGINE_OK;
}

extern "C" int oakengine_undo_clear(void)
{
	olive::UndoStack *s = stack();
	if (!s) {
		return OAKENGINE_E_STATE;
	}
	s->clear();
	return OAKENGINE_OK;
}

extern "C" int oakengine_undo_update_actions(void)
{
	olive::UndoStack *s = stack();
	if (!s) {
		return OAKENGINE_E_STATE;
	}
	s->update_actions();
	return OAKENGINE_OK;
}

extern "C" int oakengine_undo_can_undo(void)
{
	olive::UndoStack *s = stack();
	return s ? (s->can_undo() ? 1 : 0) : OAKENGINE_E_INVALID;
}

extern "C" int oakengine_undo_can_redo(void)
{
	olive::UndoStack *s = stack();
	return s ? (s->can_redo() ? 1 : 0) : OAKENGINE_E_INVALID;
}

extern "C" void *oakengine_undo_undo_action(void)
{
	olive::UndoStack *s = stack();
	return s ? static_cast<void *>(s->GetUndoAction()) : nullptr;
}

extern "C" void *oakengine_undo_redo_action(void)
{
	olive::UndoStack *s = stack();
	return s ? static_cast<void *>(s->GetRedoAction()) : nullptr;
}
