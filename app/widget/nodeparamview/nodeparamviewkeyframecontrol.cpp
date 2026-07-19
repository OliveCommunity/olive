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

#include "nodeparamviewkeyframecontrol.h"

#include <QHBoxLayout>
#include <QMessageBox>

#include "core.h"
#include "node/nodeundo.h"
#include "ui/icons/icons.h"

namespace olive
{

NodeParamViewKeyframeControl::NodeParamViewKeyframeControl(bool right_align,
														   QWidget *parent)
	: QWidget(parent)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	if (right_align) {
		// Automatically right aligns all buttons
		layout->addStretch();
	}

	prev_key_btn_ = create_new_tool_button(icon::tri_left);
	prev_key_btn_->setIconSize(prev_key_btn_->iconSize() / 2);
	layout->addWidget(prev_key_btn_);

	toggle_key_btn_ = create_new_tool_button(icon::diamond);
	toggle_key_btn_->setCheckable(true);
	toggle_key_btn_->setIconSize(toggle_key_btn_->iconSize() / 2);
	layout->addWidget(toggle_key_btn_);

	next_key_btn_ = create_new_tool_button(icon::tri_right);
	next_key_btn_->setIconSize(next_key_btn_->iconSize() / 2);
	layout->addWidget(next_key_btn_);

	enable_key_btn_ = create_new_tool_button(icon::clock);
	enable_key_btn_->setCheckable(true);
	enable_key_btn_->setIconSize(enable_key_btn_->iconSize() / 4 * 3);
	layout->addWidget(enable_key_btn_);

	connect(prev_key_btn_, &QPushButton::clicked, this,
			&NodeParamViewKeyframeControl::go_to_previous_key);
	connect(next_key_btn_, &QPushButton::clicked, this,
			&NodeParamViewKeyframeControl::go_to_next_key);
	connect(toggle_key_btn_, &QPushButton::clicked, this,
			&NodeParamViewKeyframeControl::toggle_keyframe);
	connect(enable_key_btn_, &QPushButton::toggled, this,
			&NodeParamViewKeyframeControl::show_buttons_from_keyframe_enable);
	connect(enable_key_btn_, &QPushButton::clicked, this,
			&NodeParamViewKeyframeControl::keyframe_enable_btn_clicked);

	// Set defaults
	set_input(NodeInput());
	show_buttons_from_keyframe_enable(false);
}

void NodeParamViewKeyframeControl::set_input(const NodeInput &input)
{
	if (input_.is_valid()) {
		disconnect(input_.node(), &Node::keyframe_enable_changed, this,
				   &NodeParamViewKeyframeControl::keyframe_enable_changed);
		disconnect(input_.node(), &Node::keyframe_added, this,
				   &NodeParamViewKeyframeControl::update_state);
		disconnect(input_.node(), &Node::keyframe_removed, this,
				   &NodeParamViewKeyframeControl::update_state);
		disconnect(input_.node(), &Node::keyframe_time_changed, this,
				   &NodeParamViewKeyframeControl::update_state);
	}

	input_ = input;
	set_buttons_enabled(input_.is_valid());

	// Pick up keyframing value
	enable_key_btn_->setChecked(input_.is_valid() && input_.is_keyframing());

	// Update buttons
	update_state();

	if (input_.is_valid()) {
		connect(input_.node(), &Node::keyframe_enable_changed, this,
				&NodeParamViewKeyframeControl::keyframe_enable_changed);
		connect(input_.node(), &Node::keyframe_added, this,
				&NodeParamViewKeyframeControl::update_state);
		connect(input_.node(), &Node::keyframe_removed, this,
				&NodeParamViewKeyframeControl::update_state);
		connect(input_.node(), &Node::keyframe_time_changed, this,
				&NodeParamViewKeyframeControl::update_state);
	}
}

void NodeParamViewKeyframeControl::TimeTargetDisconnectEvent(ViewerOutput *v)
{
	disconnect(v, &ViewerOutput::playhead_changed, this,
			   &NodeParamViewKeyframeControl::update_state);
}

void NodeParamViewKeyframeControl::TimeTargetConnectEvent(ViewerOutput *v)
{
	connect(v, &ViewerOutput::playhead_changed, this,
			&NodeParamViewKeyframeControl::update_state);
	update_state();
}

QPushButton *
NodeParamViewKeyframeControl::create_new_tool_button(const QIcon &icon) const
{
	QPushButton *btn = new QPushButton();
	btn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	btn->setIcon(icon);

	return btn;
}

void NodeParamViewKeyframeControl::set_buttons_enabled(bool e)
{
	prev_key_btn_->setEnabled(e);
	toggle_key_btn_->setEnabled(e);
	next_key_btn_->setEnabled(e);
	enable_key_btn_->setEnabled(e);
}

Rational NodeParamViewKeyframeControl::get_current_time_as_node_time() const
{
	return get_adjusted_time(get_time_target(), input_.node(),
						   get_time_target()->get_playhead(),
						   Node::k_transform_towards_input);
}

Rational
NodeParamViewKeyframeControl::convert_to_viewer_time(const Rational &r) const
{
	return get_adjusted_time(input_.node(), get_time_target(), r,
						   Node::k_transform_towards_output);
}

void NodeParamViewKeyframeControl::show_buttons_from_keyframe_enable(bool e)
{
	prev_key_btn_->setVisible(e);
	toggle_key_btn_->setVisible(e);
	next_key_btn_->setVisible(e);
}

void NodeParamViewKeyframeControl::toggle_keyframe(bool e)
{
	Rational node_time = get_current_time_as_node_time();

	QVector<NodeKeyframe *> keys =
		input_.node()->get_keyframes_at_time(input_, node_time);

	MultiUndoCommand *command = new MultiUndoCommand();

	int nb_tracks = input_.node()->get_number_of_keyframe_tracks(input_);

	if (e && keys.isEmpty()) {
		// Add a keyframe here (one for each track)
		for (int i = 0; i < nb_tracks; i++) {
			NodeKeyframe *key = new NodeKeyframe(
				node_time,
				input_.node()->get_split_value_at_time_on_track(input_, node_time, i),
				input_.node()->get_best_keyframe_type_for_time_on_track(input_,
																 node_time, i),
				i, input_.element(), input_.input());

			command->add_child(
				new NodeParamInsertKeyframeCommand(input_.node(), key));
		}
	} else if (!e && !keys.isEmpty()) {
		// Remove all keyframes at this time
		foreach (NodeKeyframe *key, keys) {
			command->add_child(new NodeParamRemoveKeyframeCommand(key));

			if (input_.node()->get_keyframe_tracks(input_).size() == 1) {
				// If this was the last keyframe on this track, set the standard value to the value at this time too
				command->add_child(new NodeParamSetStandardValueCommand(
					NodeKeyframeTrackReference(input_, key->track()),
					input_.node()->get_split_value_at_time_on_track(input_, node_time,
															  key->track())));
			}
		}
	}

	Core::instance()->undo_stack()->push(command, tr("Toggled Keyframe"));
}

void NodeParamViewKeyframeControl::update_state()
{
	if (!input_.is_valid() || !input_.is_keyframing() || !get_time_target()) {
		return;
	}

	NodeKeyframe *earliest_key = input_.node()->get_earliest_keyframe(input_);
	NodeKeyframe *latest_key = input_.node()->get_latest_keyframe(input_);

	Rational node_time = get_current_time_as_node_time();

	prev_key_btn_->setEnabled(earliest_key && node_time > earliest_key->time());
	next_key_btn_->setEnabled(latest_key && node_time < latest_key->time());
	toggle_key_btn_->setChecked(
		input_.node()->has_keyframe_at_time(input_, node_time));
}

void NodeParamViewKeyframeControl::go_to_previous_key()
{
	Rational node_time = get_current_time_as_node_time();

	NodeKeyframe *previous_key =
		input_.node()->get_closest_keyframe_before_time(input_, node_time);

	if (previous_key && get_time_target()) {
		Rational key_time = convert_to_viewer_time(previous_key->time());
		get_time_target()->set_playhead(key_time);
	}
}

void NodeParamViewKeyframeControl::go_to_next_key()
{
	Rational node_time = get_current_time_as_node_time();

	NodeKeyframe *next_key =
		input_.node()->get_closest_keyframe_after_time(input_, node_time);

	if (next_key && get_time_target()) {
		Rational key_time = convert_to_viewer_time(next_key->time());
		get_time_target()->set_playhead(key_time);
	}
}

void NodeParamViewKeyframeControl::keyframe_enable_btn_clicked(bool e)
{
	if (e == input_.is_keyframing()) {
		// No-op
		return;
	}

	MultiUndoCommand *command = new MultiUndoCommand();

	QString command_name;

	if (e) {
		// Enable keyframing
		command->add_child(new NodeParamSetKeyframingCommand(input_, true));

		// Create one keyframe across all tracks here
		const QVector<QVariant> &key_vals =
			input_.node()->get_split_standard_value(input_);

		for (int i = 0; i < key_vals.size(); i++) {
			NodeKeyframe *key =
				new NodeKeyframe(get_current_time_as_node_time(), key_vals.at(i),
								 NodeKeyframe::k_default_type, i,
								 input_.element(), input_.input());

			command->add_child(
				new NodeParamInsertKeyframeCommand(input_.node(), key));
		}

		command_name =
			tr("Enabled Keyframing On %1 - %2")
				.arg(input_.node()->get_label_and_name(), input_.get_input_name());
	} else {
		// Confirm the user wants to clear all keyframes
		if (QMessageBox::warning(
				this, tr("Warning"),
				tr("Are you sure you want to disable keyframing on this value? This will clear all existing keyframes."),
				QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
			// Store value at this time, we'll set this as the persistent value later
			const QVector<QVariant> &stored_vals =
				input_.node()->get_split_value_at_time(input_,
												   get_current_time_as_node_time());

			// Delete all keyframes
			foreach (const NodeKeyframeTrack &track,
					 input_.node()->get_keyframe_tracks(input_)) {
				for (int i = track.size() - 1; i >= 0; i--) {
					command->add_child(
						new NodeParamRemoveKeyframeCommand(track.at(i)));
				}
			}

			// Update standard value
			for (int i = 0; i < stored_vals.size(); i++) {
				command->add_child(new NodeParamSetStandardValueCommand(
					NodeKeyframeTrackReference(input_, i), stored_vals.at(i)));
			}

			// Disable keyframing
			command->add_child(
				new NodeParamSetKeyframingCommand(input_, false));

			command_name = tr("Disabled Keyframing On %1 - %2")
							   .arg(input_.node()->get_label_and_name(),
									input_.get_input_name());
		} else {
			// Disable action has effectively been ignored
			enable_key_btn_->setChecked(true);
		}
	}

	Core::instance()->undo_stack()->push(command, command_name);
}

void NodeParamViewKeyframeControl::keyframe_enable_changed(const NodeInput &input,
														 bool e)
{
	if (input_ == input) {
		enable_key_btn_->setChecked(e);
	}
}

}
