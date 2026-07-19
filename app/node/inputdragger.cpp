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

#include "inputdragger.h"

#include "core.h"
#include "node.h"
#include "nodeundo.h"

namespace olive
{

int NodeInputDragger::input_being_dragged = 0;

NodeInputDragger::NodeInputDragger()
{
}

bool NodeInputDragger::is_started() const
{
	return input_.is_valid();
}

void NodeInputDragger::start(const NodeKeyframeTrackReference &input,
							 const Rational &time,
							 bool create_key_on_all_tracks)
{
	Q_ASSERT(!is_started());

	// Set up new drag
	input_ = input;
	time_ = time;

	Node *node = input_.input().node();

	// Cache current value
	start_value_ = node->get_split_value_at_time_on_track(input_, time);
	end_value_ = start_value_;

	// Determine whether we are creating a keyframe or not
	if (input_.input().is_keyframing()) {
		dragging_key_ = node->get_keyframe_at_time_on_track(input_, time);

		if (!dragging_key_) {
			dragging_key_ = new NodeKeyframe(
				time, start_value_,
				node->get_best_keyframe_type_for_time_on_track(input_, time),
				input_.track(), input_.input().element(),
				input_.input().input(), node);
			created_keys_.append(dragging_key_);

			if (create_key_on_all_tracks) {
				int nb_tracks = NodeValue::get_number_of_keyframe_tracks(
					input.input().node()->get_input_data_type(
						input.input().input()));
				for (int i = 0; i < nb_tracks; i++) {
					if (i != input.track()) {
						NodeKeyframeTrackReference this_ref(input.input(), i);
						created_keys_.append(new NodeKeyframe(
							time,
							node->get_split_value_at_time_on_track(this_ref, time),
							node->get_best_keyframe_type_for_time_on_track(this_ref,
																	time),
							i, input.input().element(), input.input().input(),
							node));
					}
				}
			}
		}
	}

	input_being_dragged++;
}

void NodeInputDragger::drag(QVariant value)
{
	Q_ASSERT(is_started());

	Node *node = input_.input().node();
	const QString &input = input_.input().input();

	if (node->has_input_property(input, QStringLiteral("min"))) {
		// Assumes the value is a double of some kind
		double min =
			node->get_input_property(input, QStringLiteral("min")).toDouble();
		double v = value.toDouble();
		if (v < min) {
			value = min;
		}
	}

	if (node->has_input_property(input, QStringLiteral("max"))) {
		double max =
			node->get_input_property(input, QStringLiteral("max")).toDouble();
		double v = value.toDouble();
		if (v > max) {
			value = max;
		}
	}

	end_value_ = value;

	//input_->blockSignals(true);

	if (input_.input().is_keyframing()) {
		dragging_key_->set_value(value);
	} else {
		node->set_split_standard_value_on_track(input_, value);
	}

	//input_->blockSignals(false);
}

void NodeInputDragger::end(MultiUndoCommand *command)
{
	if (!is_started()) {
		return;
	}

	input_being_dragged--;

	if (input_.input().node()->is_input_keyframing(input_.input())) {
		for (int i = 0; i < created_keys_.size(); i++) {
			// We created a keyframe in this process
			command->add_child(new NodeParamInsertKeyframeCommand(
				input_.input().node(), created_keys_.at(i)));
		}

		// We just set a keyframe's value
		// We do this even when inserting a keyframe because we don't actually perform an insert in this undo command
		// so this will ensure the ValueChanged() signal is sent correctly
		command->add_child(new NodeParamSetKeyframeValueCommand(
			dragging_key_, end_value_, start_value_));
	} else {
		// We just set the standard value
		command->add_child(new NodeParamSetStandardValueCommand(
			input_, end_value_, start_value_));
	}

	input_.reset();
	created_keys_.clear();
}

}
