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

#include "common/nodevaluehandle.h"
#include "common/oakvaluehelper.h"
#include "core.h"
#include "node/value.h"
#include "oakengine/events.h"
#include "oakengine/undo.h"
#include "oakengine/viewer.h"
#include "oakengine/node.h"
#include "ui/icons/icons.h"

namespace olive
{

static int64_t rational_to_node_ts(Node *node, const Rational &time)
{
	int num = 0, den = 1;
	oakengine_node_frame_time_base(reinterpret_cast<OakEngineNode *>(node),
							   &num, &den);
	return core::Timecode::time_to_timestamp(time, Rational(num, den));
}

NodeParamViewKeyframeControl::NodeParamViewKeyframeControl(bool right_align,
														   QWidget *parent)
	: QWidget(parent)
	, bridge_(new EngineEventBridge(this))
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

	connect(bridge_, &EngineEventBridge::node_keyframe_enable_changed, this,
			[this](OakEngineNode *source, const QString &input, int element,
				   bool enabled) {
				keyframe_enable_changed(
					NodeInput(reinterpret_cast<Node *>(source), input,
							  element),
					enabled);
			});
	connect(bridge_, &EngineEventBridge::node_keyframe_added, this,
			&NodeParamViewKeyframeControl::update_state);
	connect(bridge_, &EngineEventBridge::node_keyframe_removed, this,
			&NodeParamViewKeyframeControl::update_state);
	connect(bridge_, &EngineEventBridge::node_keyframe_time_changed, this,
			&NodeParamViewKeyframeControl::update_state);

	// Set defaults
	set_input(NodeInput());
	show_buttons_from_keyframe_enable(false);
}

void NodeParamViewKeyframeControl::set_input(const NodeInput &input)
{
	if (input_.is_valid()) {
		bridge_->unsubscribe(keyframe_enable_sub_);
		bridge_->unsubscribe(keyframe_added_sub_);
		bridge_->unsubscribe(keyframe_removed_sub_);
		bridge_->unsubscribe(keyframe_time_sub_);
		keyframe_enable_sub_ = 0;
		keyframe_added_sub_ = 0;
		keyframe_removed_sub_ = 0;
		keyframe_time_sub_ = 0;
	}

	input_ = input;
	set_buttons_enabled(input_.is_valid());

	// Pick up keyframing value
	enable_key_btn_->setChecked(input_.is_valid() && input_.is_keyframing());

	// Update buttons
	update_state();

	if (input_.is_valid()) {
		keyframe_enable_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(input_.node()),
			OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED);
		keyframe_added_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(input_.node()),
			OAKENGINE_EVENT_NODE_KEYFRAME_ADDED);
		keyframe_removed_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(input_.node()),
			OAKENGINE_EVENT_NODE_KEYFRAME_REMOVED);
		keyframe_time_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(input_.node()),
			OAKENGINE_EVENT_NODE_KEYFRAME_TIME_CHANGED);
	}
}

void NodeParamViewKeyframeControl::TimeTargetDisconnectEvent(ViewerOutput *v)
{
	if (viewer_sub_ > 0) {
		oakengine_event_unsubscribe(viewer_sub_);
		viewer_sub_ = 0;
	}
}

void NodeParamViewKeyframeControl::TimeTargetConnectEvent(ViewerOutput *v)
{
	viewer_sub_ = oakengine_event_subscribe(
		reinterpret_cast<OakEngineNode *>(v),
		OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED,
		[](const oakengine_event *, void *userdata) {
			static_cast<NodeParamViewKeyframeControl *>(userdata)
				->update_state();
		},
		this);
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

	void *command = oakengine_undo_command_create_multi();

	Node *node = input_.node();
	const NodeValue::Type declared = node->get_input_data_type(input_.input());

	int nb_tracks = oakengine_node_value_keyframe_track_count(
		node_value_type_to_c(declared));

	const QByteArray input_utf8 = input_.input().toUtf8();
	const char *input_id = input_utf8.constData();
	const int element = input_.element();
	const int64_t time_ts = rational_to_node_ts(node, node_time);

	if (e && keys.isEmpty()) {
		// Add a keyframe here (one for each track)
		oak_node_value v;
		if (oakengine_node_get_input_at_time(
				reinterpret_cast<OakEngineNode *>(node), input_id, element, -1,
				time_ts, 1, &v) != OAKENGINE_OK) {
			oakengine_undo_command_free(command);
			return;
		}

		for (int i = 0; i < nb_tracks; i++) {
			void *cmd = oakengine_node_insert_keyframe_command(
				reinterpret_cast<OakEngineNode *>(node), input_id, element, i,
				time_ts, &v,
				NodeKeyframeTypeToFacade(
					node->get_best_keyframe_type_for_time_on_track(input_,
													 node_time, i)),
				0, 0, 0, 0);
			oakengine_undo_command_multi_add_child(command, cmd);
		}
	} else if (!e && !keys.isEmpty()) {
		// Remove all keyframes at this time
		foreach (NodeKeyframe *key, keys) {
			void *cmd = oakengine_node_remove_keyframe_command(
				reinterpret_cast<OakEngineKeyframe *>(key));
			oakengine_undo_command_multi_add_child(command, cmd);

			if (node->get_keyframe_tracks(input_).size() == 1) {
				// If this was the last keyframe on this track, set the standard value
				// to the value at this time too.
				oak_node_value v;
				NodeTrackComponentToOakNodeValue(
					declared,
					node->get_split_value_at_time_on_track(input_, node_time,
													   key->track()),
					&v);
				void *sv = oakengine_node_set_standard_value_command(
					reinterpret_cast<OakEngineNode *>(node), input_id, element,
					key->track(), &v);
				oakengine_undo_command_multi_add_child(command, sv);
			}
		}
	}

	oakengine_undo_push(command, tr("Toggled Keyframe").toUtf8().constData());
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
		oakengine_viewer_set_playhead(
			reinterpret_cast<OakEngineNode *>(get_time_target()),
			key_time.numerator(), key_time.denominator());
	}
}

void NodeParamViewKeyframeControl::go_to_next_key()
{
	Rational node_time = get_current_time_as_node_time();

	NodeKeyframe *next_key =
		input_.node()->get_closest_keyframe_after_time(input_, node_time);

	if (next_key && get_time_target()) {
		Rational key_time = convert_to_viewer_time(next_key->time());
		oakengine_viewer_set_playhead(
			reinterpret_cast<OakEngineNode *>(get_time_target()),
			key_time.numerator(), key_time.denominator());
	}
}

void NodeParamViewKeyframeControl::keyframe_enable_btn_clicked(bool e)
{
	if (e == input_.is_keyframing()) {
		// No-op
		return;
	}

	Node *node = input_.node();
	const NodeValue::Type declared = node->get_input_data_type(input_.input());
	const QByteArray input_utf8 = input_.input().toUtf8();
	const char *input_id = input_utf8.constData();
	const int element = input_.element();

	QString command_name;

	if (e) {
		// Enable keyframing
		void *command = oakengine_undo_command_create_multi();

		void *kf = oakengine_node_set_input_keyframing_command(
			reinterpret_cast<OakEngineNode *>(node), input_id, element, 1);
		oakengine_undo_command_multi_add_child(command, kf);

		// Create one keyframe across all tracks here
		const QVector<QVariant> &key_vals = node->get_split_standard_value(input_);

		if (!key_vals.isEmpty()) {
			QVector<oak_node_value> tracks(key_vals.size());
			bool converted = true;
			for (int i = 0; i < key_vals.size(); i++) {
				if (!NodeTrackComponentToOakNodeValue(declared, key_vals.at(i),
													  &tracks[i])) {
					converted = false;
					break;
				}
			}
			oak_node_value v;
			memset(&v, 0, sizeof(v));
			if (converted) {
				oakengine_node_value_combine_tracks(
					node_value_type_to_c(declared), tracks.constData(),
					tracks.size(), &v);
			}
			const int64_t time_ts =
				rational_to_node_ts(node, get_current_time_as_node_time());
			const int type = NodeKeyframeTypeToFacade(static_cast<NodeKeyframe::Type>(oakengine_keyframe_default_type()));
			for (int i = 0; i < key_vals.size(); i++) {
				void *cmd = oakengine_node_insert_keyframe_command(
					reinterpret_cast<OakEngineNode *>(node), input_id, element, i,
					time_ts, &v, type, 0, 0, 0, 0);
				oakengine_undo_command_multi_add_child(command, cmd);
			}
		}

		command_name =
			tr("Enabled Keyframing On %1 - %2")
				.arg(node->get_label_and_name(), input_.get_input_name());

		oakengine_undo_push(command, command_name.toUtf8().constData());
	} else {
		// Confirm the user wants to clear all keyframes
		if (QMessageBox::warning(
				this, tr("Warning"),
				tr("Are you sure you want to disable keyframing on this value? This will clear all existing keyframes."),
				QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
			void *command = oakengine_undo_command_create_multi();

			// Store value at this time, we'll set this as the persistent value later
			const QVector<QVariant> &stored_vals =
				node->get_split_value_at_time(input_,
											  get_current_time_as_node_time());

			// Delete all keyframes
			foreach (const NodeKeyframeTrack &track,
					 node->get_keyframe_tracks(input_)) {
				for (int i = track.size() - 1; i >= 0; i--) {
					void *cmd = oakengine_node_remove_keyframe_command(
						reinterpret_cast<OakEngineKeyframe *>(track.at(i)));
					oakengine_undo_command_multi_add_child(command, cmd);
				}
			}

			// Update standard value
			for (int i = 0; i < stored_vals.size(); i++) {
				oak_node_value v;
				NodeTrackComponentToOakNodeValue(declared, stored_vals.at(i), &v);
				void *cmd = oakengine_node_set_standard_value_command(
					reinterpret_cast<OakEngineNode *>(node), input_id, element, i,
					&v);
				oakengine_undo_command_multi_add_child(command, cmd);
			}

			// Disable keyframing
			void *kf = oakengine_node_set_input_keyframing_command(
				reinterpret_cast<OakEngineNode *>(node), input_id, element, 0);
			oakengine_undo_command_multi_add_child(command, kf);

			command_name = tr("Disabled Keyframing On %1 - %2")
							   .arg(node->get_label_and_name(),
								input_.get_input_name());

			oakengine_undo_push(command, command_name.toUtf8().constData());
		} else {
			// Disable action has effectively been ignored
			enable_key_btn_->setChecked(true);
		}
	}
}

void NodeParamViewKeyframeControl::keyframe_enable_changed(const NodeInput &input,
														 bool e)
{
	if (input_ == input) {
		enable_key_btn_->setChecked(e);
	}
}

}
