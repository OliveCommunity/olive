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
#include "oakengine/events.h"
#include "oakengine/undo.h"
#include "oakengine/viewer.h"
#include "oakengine/node.h"
#include "ui/icons/icons.h"

namespace olive
{

static int64_t rational_to_node_ts(OakEngineNode *node, const Rational &time)
{
	int num = 0, den = 1;
	oakengine_node_frame_time_base(node, &num, &den);
	return core::Timecode::time_to_timestamp(time, Rational(num, den));
}

// Exact-rational, all-tracks equivalent of Node::has_keyframe_at_time().
// The facade's oakengine_node_has_keyframe_at_time() uses a lossy
// whole-second/single-track contract, so the tracks are walked through
// the handle API instead.
static bool input_has_keyframe_at_time(OakEngineNode *node,
									   const char *input_id, int element,
									   const Rational &time)
{
	const int tracks =
		oakengine_node_keyframe_track_count(node, input_id, element);
	for (int t = 0; t < tracks; t++) {
		if (oakengine_node_keyframe_handle_at_time(
				node, input_id, element, t, time.numerator(),
				time.denominator())) {
			return true;
		}
	}
	return false;
}

// All-tracks equivalent of Node::get_closest_keyframe_before/after_time()
// (same lossy-contract caveat as above). Returns false when none.
static bool closest_keyframe_time(OakEngineNode *node, const char *input_id,
								  int element, const Rational &time, bool after,
								  Rational *out)
{
	const int tracks =
		oakengine_node_keyframe_track_count(node, input_id, element);
	bool found = false;
	Rational best;
	for (int t = 0; t < tracks; t++) {
		const int count =
			oakengine_node_keyframe_count_on_track(node, input_id, element, t);
		for (int i = 0; i < count; i++) {
			OakEngineKeyframe *key = oakengine_node_keyframe_handle_on_track(
				node, input_id, element, t, i);
			int64_t num = 0, den = 1;
			oakengine_keyframe_get_time(key, &num, &den);
			const Rational kt{int(num), int(den)};
			if ((after && kt > time) || (!after && kt < time)) {
				if (!found || (after && kt < best) ||
					(!after && kt > best)) {
					best = kt;
					found = true;
				}
			}
		}
	}
	if (found) {
		*out = best;
	}
	return found;
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
					oak::Input(source, input, element),
					enabled);
			});
	connect(bridge_, &EngineEventBridge::node_keyframe_added, this,
			&NodeParamViewKeyframeControl::update_state);
	connect(bridge_, &EngineEventBridge::node_keyframe_removed, this,
			&NodeParamViewKeyframeControl::update_state);
	connect(bridge_, &EngineEventBridge::node_keyframe_time_changed, this,
			&NodeParamViewKeyframeControl::update_state);

	// Set defaults
	set_input(oak::Input());
	show_buttons_from_keyframe_enable(false);
}

NodeParamViewKeyframeControl::~NodeParamViewKeyframeControl()
{
	// Raw C-API subscription carries `this` as userdata; it is not covered
	// by Qt's auto-disconnect. Without this, a playhead event delivered
	// after destruction calls update_state() on a dead object.
	if (viewer_sub_ > 0) {
		oakengine_event_unsubscribe(viewer_sub_);
		viewer_sub_ = 0;
	}
	// Drop the keyframe_* bridge subscriptions too (same raw-userdata
	// mechanism underneath).
	set_input(oak::Input());
}

void NodeParamViewKeyframeControl::set_input(const oak::Input &input)
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
			reinterpret_cast<void *>(input_.node_handle()),
			OAKENGINE_EVENT_NODE_KEYFRAME_ENABLE_CHANGED);
		keyframe_added_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(input_.node_handle()),
			OAKENGINE_EVENT_NODE_KEYFRAME_ADDED);
		keyframe_removed_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(input_.node_handle()),
			OAKENGINE_EVENT_NODE_KEYFRAME_REMOVED);
		keyframe_time_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(input_.node_handle()),
			OAKENGINE_EVENT_NODE_KEYFRAME_TIME_CHANGED);
	}
}

void NodeParamViewKeyframeControl::TimeTargetDisconnectEvent(OakEngineNode *v)
{
	if (viewer_sub_ > 0) {
		oakengine_event_unsubscribe(viewer_sub_);
		viewer_sub_ = 0;
	}
}

void NodeParamViewKeyframeControl::TimeTargetConnectEvent(OakEngineNode *v)
{
	viewer_sub_ = oakengine_event_subscribe(
		v,
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
	int64_t pn = 0, pd = 1;
	oakengine_viewer_get_playhead(get_time_target(), &pn, &pd);
	return get_adjusted_time(get_time_target(),
						   input_.node_handle(),
						   Rational(pn, pd),
						   k_transform_towards_input);
}

Rational
NodeParamViewKeyframeControl::convert_to_viewer_time(const Rational &r) const
{
	return get_adjusted_time(input_.node_handle(),
						   get_time_target(), r,
						   k_transform_towards_output);
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

	OakEngineNode *node = input_.node_handle();

	const QByteArray input_utf8 = input_.input_id().toUtf8();
	const char *input_id = input_utf8.constData();
	const int element = input_.element();
	const int64_t time_ts = rational_to_node_ts(node, node_time);

	// Node::get_keyframes_at_time() across all tracks
	QVector<OakEngineKeyframe *> keys(
		qMax(1, oakengine_node_keyframe_track_count(node, input_id, element)));
	keys.resize(oakengine_node_keyframes_at_time(
		node, input_id, element, node_time.numerator(),
		node_time.denominator(), keys.data(), keys.size()));

	// WRAPPER-GAP: oakengine_undo_* / oakengine_node_*_keyframe_command
	// (undo command assembly has no oak:: wrapper)
	void *command = oakengine_undo_command_create_multi();

	const int c_type = input_.c_type();

	int nb_tracks = oakengine_node_value_keyframe_track_count(c_type);

	if (e && keys.isEmpty()) {
		// Add a keyframe here (one for each track)
		oak_node_value v;
		if (oakengine_node_get_input_at_time(
				node, input_id, element, -1,
				time_ts, 1, &v) != OAKENGINE_OK) {
			oakengine_undo_command_free(command);
			return;
		}

		for (int i = 0; i < nb_tracks; i++) {
			void *cmd = oakengine_node_insert_keyframe_command(
				node, input_id, element, i,
				time_ts, &v,
				oakengine_node_keyframe_best_type_at_time(
					node, input_id, element, time_ts, i,
					oakengine_keyframe_default_type()),
				0, 0, 0, 0);
			oakengine_undo_command_multi_add_child(command, cmd);
		}
	} else if (!e && !keys.isEmpty()) {
		// Remove all keyframes at this time
		foreach (OakEngineKeyframe *key, keys) {
			void *cmd = oakengine_node_remove_keyframe_command(key);
			oakengine_undo_command_multi_add_child(command, cmd);

			if (oakengine_node_keyframe_track_count(node, input_id, element) == 1) {
				// If this was the last keyframe on this track, set the standard value
				// to the value at this time too.
				oak_node_value normal;
				const int track = oakengine_keyframe_get_track(key);
				QVector<oak_node_value> track_vals(nb_tracks);
				if (oakengine_node_get_input_at_time(
						node, input_id, element, -1, time_ts, 1,
						&normal) == OAKENGINE_OK &&
					oakengine_node_value_split_to_tracks(
						c_type, &normal, track_vals.data(),
						nb_tracks) == OAKENGINE_OK &&
					track >= 0 && track < nb_tracks) {
					void *sv = oakengine_node_set_standard_value_command(
						node, input_id, element,
						track, &track_vals[track]);
					oakengine_undo_command_multi_add_child(command, sv);
				}
			}
		}
	}

	oakengine_undo_push(command, tr("Toggled Keyframe").toUtf8().constData());
}

void NodeParamViewKeyframeControl::update_state()
{
	if (!input_.is_valid() || !input_.is_keyframing() ||
		!get_time_target()) {
		return;
	}

	OakEngineNode *node = input_.node_handle();
	const QByteArray input_utf8 = input_.input_id().toUtf8();
	const char *input_id = input_utf8.constData();
	const int element = input_.element();

	int64_t earliest_num = 0, earliest_den = 1;
	int64_t latest_num = 0, latest_den = 1;
	const bool has_earliest = oakengine_node_keyframe_earliest_time(
		node, input_id, element, &earliest_num, &earliest_den);
	const bool has_latest = oakengine_node_keyframe_latest_time(
		node, input_id, element, &latest_num, &latest_den);

	Rational node_time = get_current_time_as_node_time();

	prev_key_btn_->setEnabled(
		has_earliest &&
		node_time > Rational(int(earliest_num), int(earliest_den)));
	next_key_btn_->setEnabled(
		has_latest && node_time < Rational(int(latest_num), int(latest_den)));
	toggle_key_btn_->setChecked(
		input_has_keyframe_at_time(node, input_id, element, node_time));
}

void NodeParamViewKeyframeControl::go_to_previous_key()
{
	Rational node_time = get_current_time_as_node_time();

	Rational previous_time;
	if (closest_keyframe_time(input_.node_handle(),
							  input_.input_id().toUtf8().constData(),
							  input_.element(), node_time, false,
							  &previous_time) &&
		get_time_target()) {
		Rational key_time = convert_to_viewer_time(previous_time);
		oakengine_viewer_set_playhead(
			reinterpret_cast<OakEngineNode *>(get_time_target()),
			key_time.numerator(), key_time.denominator());
	}
}

void NodeParamViewKeyframeControl::go_to_next_key()
{
	Rational node_time = get_current_time_as_node_time();

	Rational next_time;
	if (closest_keyframe_time(input_.node_handle(),
							  input_.input_id().toUtf8().constData(),
							  input_.element(), node_time, true,
							  &next_time) &&
		get_time_target()) {
		Rational key_time = convert_to_viewer_time(next_time);
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

	OakEngineNode *node = input_.node_handle();
	const QByteArray input_utf8 = input_.input_id().toUtf8();
	const char *input_id = input_utf8.constData();
	const int element = input_.element();

	QString command_name;

	if (e) {
		// Enable keyframing
		// WRAPPER-GAP: oakengine_undo_* / oakengine_node_*_command (undo
		// command assembly has no oak:: wrapper)
		void *command = oakengine_undo_command_create_multi();

		void *kf = oakengine_node_set_input_keyframing_command(
			node, input_id, element, 1);
		oakengine_undo_command_multi_add_child(command, kf);

		// Create one keyframe across all tracks here. Keyframing is still
		// off at this point, so the value at the current time is the
		// input's standard value.
		const int64_t time_ts =
			rational_to_node_ts(node, get_current_time_as_node_time());
		oak_node_value v;
		memset(&v, 0, sizeof(v));
		if (oakengine_node_get_input_at_time(node, input_id, element, -1,
											 time_ts, 1, &v) == OAKENGINE_OK) {
			const int nb_tracks = input_.keyframe_track_count();
			const int type = oakengine_keyframe_default_type();
			for (int i = 0; i < nb_tracks; i++) {
				void *cmd = oakengine_node_insert_keyframe_command(
					node, input_id, element, i,
					time_ts, &v, type, 0, 0, 0, 0);
				oakengine_undo_command_multi_add_child(command, cmd);
			}
		}

		command_name =
			tr("Enabled Keyframing On %1 - %2")
				.arg(oak::Node(node).label_and_name(),
					 oak::Input(node, input_id).name());

		oakengine_undo_push(command, command_name.toUtf8().constData());
	} else {
		// Confirm the user wants to clear all keyframes
		if (QMessageBox::warning(
				this, tr("Warning"),
				tr("Are you sure you want to disable keyframing on this value? This will clear all existing keyframes."),
				QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
			// WRAPPER-GAP: oakengine_undo_* / oakengine_node_*_command (undo
			// command assembly has no oak:: wrapper)
			void *command = oakengine_undo_command_create_multi();

			// Store value at this time, we'll set this as the persistent value later
			const int64_t time_ts =
				rational_to_node_ts(node, get_current_time_as_node_time());
			const int nb_tracks = input_.keyframe_track_count();
			QVector<oak_node_value> track_vals(nb_tracks);
			bool have_vals = false;
			oak_node_value normal;
			if (oakengine_node_get_input_at_time(
					node, input_id, element, -1, time_ts, 1,
					&normal) == OAKENGINE_OK &&
				oakengine_node_value_split_to_tracks(
					input_.c_type(), &normal, track_vals.data(),
					nb_tracks) == OAKENGINE_OK) {
				have_vals = true;
			}

			// Delete all keyframes
			for (int t = 0; t < nb_tracks; t++) {
				const int count = oakengine_node_keyframe_count_on_track(
					node, input_id, element, t);
				for (int i = count - 1; i >= 0; i--) {
					void *cmd = oakengine_node_remove_keyframe_command(
						oakengine_node_keyframe_handle_on_track(
							node, input_id, element, t, i));
					oakengine_undo_command_multi_add_child(command, cmd);
				}
			}

			// Update standard value
			if (have_vals) {
				for (int i = 0; i < nb_tracks; i++) {
					void *cmd = oakengine_node_set_standard_value_command(
						node, input_id, element, i,
						&track_vals[i]);
					oakengine_undo_command_multi_add_child(command, cmd);
				}
			}

			// Disable keyframing
			void *kf = oakengine_node_set_input_keyframing_command(
				node, input_id, element, 0);
			oakengine_undo_command_multi_add_child(command, kf);

			command_name = tr("Disabled Keyframing On %1 - %2")
							   .arg(oak::Node(node).label_and_name(),
									oak::Input(node, input_id).name());

			oakengine_undo_push(command, command_name.toUtf8().constData());
		} else {
			// Disable action has effectively been ignored
			enable_key_btn_->setChecked(true);
		}
	}
}

void NodeParamViewKeyframeControl::keyframe_enable_changed(const oak::Input &input,
														 bool e)
{
	if (input_ == input) {
		enable_key_btn_->setChecked(e);
	}
}

}
