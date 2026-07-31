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

#include "keyframeproperties.h"

#include <QDialogButtonBox>
#include <QGridLayout>

#include "common/keyframetypes.h"
#include "oakengine/node.h"
#include "olive/core/util/timecodefunctions.h"

namespace olive
{

namespace
{

Rational keyframe_rational_time(const oak::Keyframe &key)
{
	int64_t num = 0, den = 1;
	key.time(&num, &den);
	return Rational(int(num), int(den));
}

// Selected keyframes grouped by owning input (node/id/element), with
// times converted to the facade's frame timestamps. The dialog's
// per-property writes go through the facade as ONE undoable command per
// group (usually just one).
struct KeyGroup {
	OakEngineNode *node;
	QString input;
	int element;
	QVector<int64_t> times;
	QVector<int> tracks;
};

QVector<KeyGroup> group_keys(const QVector<oak::Keyframe> &keys)
{
	QVector<KeyGroup> groups;
	for (const oak::Keyframe &item : keys) {
		OakEngineNode *node = item.node().handle();
		int g = 0;
		for (; g < groups.size(); g++) {
			if (groups.at(g).node == node &&
				groups.at(g).input == item.input_id() &&
				groups.at(g).element == item.element()) {
				break;
			}
		}
		if (g == groups.size()) {
			groups.append({ node, item.input_id(), item.element(),
							{}, {} });
		}
		int tbn = 0, tbd = 0;
		oakengine_node_frame_time_base(node, &tbn, &tbd);
		groups[g].times.append(Timecode::time_to_timestamp(
			keyframe_rational_time(item), Rational(tbn, tbd), Timecode::k_round));
		groups[g].tracks.append(item.track());
	}
	return groups;
}

} // namespace

KeyframePropertiesDialog::KeyframePropertiesDialog(
	const QVector<oak::Keyframe> &keys, const Rational &timebase,
	QWidget *parent)
	: QDialog(parent)
	, keys_(keys)
	, timebase_(timebase)
{
	setWindowTitle(tr("Keyframe Properties"));

	QGridLayout *layout = new QGridLayout(this);

	int row = 0;

	layout->addWidget(new QLabel("Time:"), row, 0);

	time_slider_ = new RationalSlider();
	time_slider_->set_display_type(slider::k_time);
	time_slider_->set_timebase(timebase_);
	layout->addWidget(time_slider_, row, 1);

	row++;

	layout->addWidget(new QLabel("Type:"), row, 0);

	type_select_ = new QComboBox();
	connect(type_select_, SIGNAL(currentIndexChanged(int)), this,
			SLOT(key_type_changed(int)));
	layout->addWidget(type_select_, row, 1);

	row++;

	// Bezier handles
	bezier_group_ = new QGroupBox();

	QGridLayout *bezier_group_layout = new QGridLayout(bezier_group_);

	bezier_group_layout->addWidget(new QLabel(tr("In:")), 0, 0);

	bezier_in_x_slider_ = new FloatSlider();
	bezier_group_layout->addWidget(bezier_in_x_slider_, 0, 1);

	bezier_in_y_slider_ = new FloatSlider();
	bezier_group_layout->addWidget(bezier_in_y_slider_, 0, 2);

	bezier_group_layout->addWidget(new QLabel(tr("Out:")), 1, 0);

	bezier_out_x_slider_ = new FloatSlider();
	bezier_group_layout->addWidget(bezier_out_x_slider_, 1, 1);

	bezier_out_y_slider_ = new FloatSlider();
	bezier_group_layout->addWidget(bezier_out_y_slider_, 1, 2);

	layout->addWidget(bezier_group_, row, 0, 1, 2);

	bool all_same_time = true;
	bool can_set_time = true;

	bool all_same_type = true;

	bool all_same_bezier_in_x = true;
	bool all_same_bezier_in_y = true;
	bool all_same_bezier_out_x = true;
	bool all_same_bezier_out_y = true;

	for (int i = 0; i < keys_.size(); i++) {
		if (i > 0) {
			const oak::Keyframe &prev_key = keys_.at(i - 1);
			const oak::Keyframe &this_key = keys_.at(i);

			// Determine if the keyframes are all the same time or not
			if (all_same_time) {
				all_same_time = (keyframe_rational_time(prev_key) ==
								 keyframe_rational_time(this_key));
			}

			// Determine if the keyframes are all the same type
			if (all_same_type) {
				all_same_type = (prev_key.type() == this_key.type());
			}

			// Check all four bezier control points
			if (all_same_bezier_in_x) {
				all_same_bezier_in_x = (prev_key.bezier_point(0).x() ==
										this_key.bezier_point(0).x());
			}

			if (all_same_bezier_in_y) {
				all_same_bezier_in_y = (prev_key.bezier_point(0).y() ==
										this_key.bezier_point(0).y());
			}

			if (all_same_bezier_out_x) {
				all_same_bezier_out_x = (prev_key.bezier_point(1).x() ==
										 this_key.bezier_point(1).x());
			}

			if (all_same_bezier_out_y) {
				all_same_bezier_out_y = (prev_key.bezier_point(1).y() ==
										 this_key.bezier_point(1).y());
			}
		}

		// Determine if any keyframes are on the same track (in which case we can't set the time)
		if (can_set_time) {
			for (int j = 0; j < keys_.size(); j++) {
				if (i != j && keys_.at(j).track() == keys_.at(i).track()) {
					can_set_time = false;
					break;
				}
			}
		}

		if (!all_same_time && !all_same_type && !can_set_time &&
			!all_same_bezier_in_x && !all_same_bezier_in_y &&
			!all_same_bezier_out_x && !all_same_bezier_out_y) {
			break;
		}
	}

	if (all_same_time) {
		time_slider_->set_value(keyframe_rational_time(keys_.front()));
	} else {
		time_slider_->set_tristate();
	}

	time_slider_->setEnabled(can_set_time);

	if (!all_same_type) {
		// If all keyframes aren't the same type, add an empty item
		type_select_->addItem(QStringLiteral("--"), -1);

		// Ensure UI updates for the index being 0
		key_type_changed(0);
	}

	// Item data uses the facade easing order (oak::Keyframe::type()):
	// 0 = linear, 1 = bezier, 2 = hold.
	type_select_->addItem(tr("Linear"), KeyframeTypes::k_facade_linear);
	type_select_->addItem(tr("Hold"), KeyframeTypes::k_facade_hold);
	type_select_->addItem(tr("Bezier"), KeyframeTypes::k_facade_bezier);

	if (all_same_type) {
		// If all keyframes are the same type, set it here
		for (int i = 0; i < type_select_->count(); i++) {
			if (type_select_->itemData(i).toInt() == keys_.front().type()) {
				type_select_->setCurrentIndex(i);

				// Ensure UI updates for this index
				key_type_changed(i);
				break;
			}
		}
	}

	set_up_bezier_slider(bezier_in_x_slider_, all_same_bezier_in_x,
					  keys_.front().bezier_point(0).x());
	set_up_bezier_slider(bezier_in_y_slider_, all_same_bezier_in_y,
					  keys_.front().bezier_point(0).y());
	set_up_bezier_slider(bezier_out_x_slider_, all_same_bezier_out_x,
					  keys_.front().bezier_point(1).x());
	set_up_bezier_slider(bezier_out_y_slider_, all_same_bezier_out_y,
					  keys_.front().bezier_point(1).y());

	row++;

	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->setCenterButtons(true);
	layout->addWidget(buttons, row, 0, 1, 2);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void KeyframePropertiesDialog::accept()
{
	const Rational new_time = time_slider_->get_value();
	const int new_type = type_select_->currentData().toInt();

	const QVector<KeyGroup> groups = group_keys(keys_);

	if (new_type > -1) {
		// new_type is already in the facade's easing order (linear 0 /
		// bezier 1 / hold 2), so it can be passed straight through.
		foreach (const KeyGroup &g, groups) {
			oakengine_node_keyframes_set_type_many(
				g.node,
				g.input.toUtf8().constData(), g.element, g.times.constData(),
				g.tracks.data(), g.times.size(), new_type);
		}
	}

	if (bezier_group_->isEnabled()) {
		foreach (const KeyGroup &g, groups) {
			oakengine_node_keyframes_set_bezier_many(
				g.node,
				g.input.toUtf8().constData(), g.element, g.times.constData(),
				g.tracks.data(), g.times.size(),
				bezier_in_x_slider_->get_value(),
				bezier_in_y_slider_->get_value(),
				bezier_out_x_slider_->get_value(),
				bezier_out_y_slider_->get_value());
		}
	}

	// Time moves go LAST: the facade addresses keyframes by time, so the
	// type/bezier writes above must happen while the keys still sit at
	// the times the groups were built from.
	if (time_slider_->isEnabled() && !time_slider_->is_tristate()) {
		foreach (const KeyGroup &g, groups) {
			OakEngineNode *handle = g.node;
			int tbn = 0, tbd = 0;
			oakengine_node_frame_time_base(handle, &tbn, &tbd);
			oakengine_node_keyframes_set_time_many(
				handle, g.input.toUtf8().constData(), g.element,
				g.times.constData(), g.tracks.data(), g.times.size(),
				Timecode::time_to_timestamp(new_time, Rational(tbn, tbd),
											Timecode::k_round));
		}
	}

	QDialog::accept();
}

void KeyframePropertiesDialog::set_up_bezier_slider(FloatSlider *slider,
												 bool all_same, double value)
{
	if (all_same) {
		slider->set_value(value);
	} else {
		slider->set_tristate();
	}
}

void KeyframePropertiesDialog::key_type_changed(int index)
{
	bezier_group_->setEnabled(type_select_->itemData(index) ==
							  KeyframeTypes::k_facade_bezier);
}

}
