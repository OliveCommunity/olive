/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "multicamnode.h"

#include "node/project/sequence/sequence.h"

namespace olive
{

#define super Node

const QString MultiCamNode::k_current_input = QStringLiteral("current_in");
const QString MultiCamNode::k_sources_input = QStringLiteral("sources_in");
const QString MultiCamNode::k_sequence_input = QStringLiteral("sequence_in");
const QString MultiCamNode::k_sequence_type_input =
	QStringLiteral("sequence_type_in");

MultiCamNode::MultiCamNode()
{
	add_input(k_current_input, NodeValue::k_combo, InputFlags(k_input_flag_static));

	add_input(k_sources_input, NodeValue::k_none,
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_array));
	set_input_property(k_sources_input, QStringLiteral("arraystart"), 1);

	add_input(k_sequence_input, NodeValue::k_none,
			 InputFlags(k_input_flag_not_keyframable));
	add_input(k_sequence_type_input, NodeValue::k_combo,
			 InputFlags(k_input_flag_static | k_input_flag_hidden));

	sequence_ = nullptr;
}

QString MultiCamNode::name() const
{
	return tr("Multi-Cam");
}

QString MultiCamNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.multicam");
}

QVector<Node::CategoryID> MultiCamNode::category() const
{
	return { k_category_timeline };
}

QString MultiCamNode::description() const
{
	return tr("Allows easy switching between multiple sources.");
}

Node::ActiveElements
MultiCamNode::get_active_elements_at_time(const QString &input,
									  const TimeRange &r) const
{
	if (input == k_sources_input) {
		int src = get_current_source();
		if (src >= 0 && src < get_source_count()) {
			Node::ActiveElements a;
			a.add(src);
			return a;
		} else {
			return ActiveElements::k_no_elements;
		}
	} else {
		return super::get_active_elements_at_time(input, r);
	}
}

void MultiCamNode::value(const NodeValueRow &value, const NodeGlobals &globals,
						 NodeValueTable *table) const
{
	NodeValueArray arr = value[k_sources_input].to_array();
	if (!arr.empty()) {
		table->push(arr.begin()->second);
	}
}

void MultiCamNode::index_to_row_cols(int index, int total_rows, int total_cols,
								  int *row, int *col)
{
	Q_UNUSED(total_rows)

	*col = index % total_cols;
	*row = index / total_cols;
}

Node *MultiCamNode::get_connected_render_output(const QString &input,
											 int element) const
{
	if (sequence_ && input == k_sources_input && element >= 0 &&
		element < get_source_count()) {
		return get_track_list()->get_track_at(element);
	} else {
		return Node::get_connected_render_output(input, element);
	}
}

bool MultiCamNode::is_input_connected_for_render(const QString &input,
											 int element) const
{
	if (sequence_ && input == k_sources_input && element >= 0 &&
		element < get_source_count()) {
		return true;
	} else {
		return Node::is_input_connected_for_render(input, element);
	}
}

QVector<QString> MultiCamNode::ignore_inputs_for_rendering() const
{
	return { k_sequence_input };
}

void MultiCamNode::InputConnectedEvent(const QString &input, int element,
									   Node *output)
{
	if (input == k_sequence_input) {
		if (Sequence *s = dynamic_cast<Sequence *>(output)) {
			set_input_flag(k_sequence_type_input, k_input_flag_hidden, false);
			sequence_ = s;
		}
	}
}

void MultiCamNode::InputDisconnectedEvent(const QString &input, int element,
										  Node *output)
{
	if (input == k_sequence_input) {
		set_input_flag(k_sequence_type_input, k_input_flag_hidden, true);
		sequence_ = nullptr;
	}
}

TrackList *MultiCamNode::get_track_list() const
{
	return sequence_->track_list(
		static_cast<Track::Type>(get_standard_value(k_sequence_type_input).toInt()));
}

void MultiCamNode::retranslate()
{
	super::retranslate();

	set_input_name(k_current_input, tr("Current"));
	set_input_name(k_sources_input, tr("Sources"));
	set_input_name(k_sequence_input, tr("Sequence"));
	set_input_name(k_sequence_type_input, tr("Sequence Type"));
	set_combo_box_strings(k_sequence_type_input, { tr("Video"), tr("Audio") });

	QStringList names;
	int name_count = get_source_count();
	names.reserve(name_count);
	for (int i = 0; i < name_count; i++) {
		QString src_name;
		if (Node *n = get_connected_render_output(k_sources_input, i)) {
			src_name = n->name();
		}
		names.append(tr("%1: %2").arg(QString::number(i + 1), src_name));
	}
	set_combo_box_strings(k_current_input, names);
}

int MultiCamNode::get_source_count() const
{
	if (sequence_) {
		return get_track_list()->get_track_count();
	} else {
		return input_array_size(k_sources_input);
	}
}

void MultiCamNode::get_rows_and_columns(int sources, int *rows_in, int *cols_in)
{
	int &rows = *rows_in;
	int &cols = *cols_in;

	rows = 1;
	cols = 1;
	while (rows * cols < sources) {
		if (rows < cols) {
			rows++;
		} else {
			cols++;
		}
	}
}

}
