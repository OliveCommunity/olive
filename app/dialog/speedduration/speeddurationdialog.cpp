/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
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

#include "speeddurationdialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>

#include "core.h"
#include "oakengine/preview.h"
#include "oakengine/timeline.h"
#include "oakengine/node.h"
#include "oakengine/undo.h"
#include "timeline/timelinecommonapp.h"
#include "widget/timelinewidget/cliphandle.h"

namespace olive
{

#define super QDialog

namespace
{

/// Block::length() as rational seconds (C ABI facade).
inline Rational sdd_block_length(const OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_length_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

/// Block::in() as rational seconds.
inline Rational sdd_block_in(const OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_in_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

/// Block::out() as rational seconds.
inline Rational sdd_block_out(const OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_out_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

/// Block::next() as a block handle.
inline OakEngineBlock *sdd_block_next(OakEngineBlock *block)
{
	return oakengine_block_next(block);
}

/// ClipBlock::track() as an opaque track handle.
inline OakEngineTrack *sdd_clip_track(OakEngineBlock *clip)
{
	return oakengine_block_get_track(clip);
}

} // namespace

SpeedDurationDialog::SpeedDurationDialog(const QVector<OakEngineBlock *> &clips,
										 const Rational &timebase,
										 QWidget *parent)
	: super(parent)
	, clips_(clips)
	, timebase_(timebase)
{
	setWindowTitle(tr("Clip Properties"));

	QVBoxLayout *layout = new QVBoxLayout(this);

	{
		QGroupBox *speed_group = new QGroupBox(tr("Speed/Duration"));
		layout->addWidget(speed_group);

		QGridLayout *speed_layout = new QGridLayout(speed_group);

		int row = 0;

		speed_layout->addWidget(new QLabel(tr("Speed:")), row, 0);

		speed_slider_ = new FloatSlider();
		speed_slider_->set_display_type(slider::k_percentage);
		connect(speed_slider_, &FloatSlider::value_changed, this,
				&SpeedDurationDialog::speed_changed);
		speed_layout->addWidget(speed_slider_, row, 1);

		row++;

		speed_layout->addWidget(new QLabel(tr("Duration:")), row, 0);

		dur_slider_ = new RationalSlider();
		dur_slider_->set_timebase(timebase);
		dur_slider_->set_display_type(slider::k_time);
		connect(dur_slider_, &RationalSlider::value_changed, this,
				&SpeedDurationDialog::duration_changed);
		speed_layout->addWidget(dur_slider_, row, 1);

		row++;

		link_box_ = new QCheckBox(tr("Link Speed and Duration"));
		link_box_->setChecked(true);
		speed_layout->addWidget(link_box_, row, 0, 1, 2);

		row++;

		reverse_box_ = new QCheckBox(tr("Reverse"));
		speed_layout->addWidget(reverse_box_, row, 0, 1, 2);

		row++;

		maintain_audio_pitch_box_ = new QCheckBox(tr("Maintain Audio Pitch"));
		speed_layout->addWidget(maintain_audio_pitch_box_, row, 0, 1, 2);

		row++;

		ripple_box_ = new QCheckBox(tr("Ripple Trailing Clips"));
		speed_layout->addWidget(ripple_box_, row, 0, 1, 2);
	}

	{
		auto loop_box = new QGroupBox(tr("Loop"));
		layout->addWidget(loop_box);

		auto loop_layout = new QGridLayout(loop_box);

		int row = 0;

		loop_layout->addWidget(new QLabel(tr("Loop:")), row, 0);

		loop_combo_ = new QComboBox();
		loop_combo_->addItem(tr("None"), OAKENGINE_LOOP_MODE_OFF);
		loop_combo_->addItem(tr("Loop"), OAKENGINE_LOOP_MODE_LOOP);
		loop_combo_->addItem(tr("Clamp"), OAKENGINE_LOOP_MODE_CLAMP);
		loop_layout->addWidget(loop_combo_, row, 1);
	}

	QDialogButtonBox *btns =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	btns->setCenterButtons(true);
	connect(btns, &QDialogButtonBox::accepted, this,
			&SpeedDurationDialog::accept);
	connect(btns, &QDialogButtonBox::rejected, this,
			&SpeedDurationDialog::reject);
	layout->addWidget(btns);

	// Determine which speed value to use
	start_speed_ = clip_speed(clips.first());
	start_duration_ = sdd_block_length(clips.first());
	start_reverse_ = clip_is_reversed(clips.first());
	start_maintain_audio_pitch_ = clip_maintain_audio_pitch(clips.first());
	start_loop_ = clip_loop_mode(clips.first());
	for (int i = 1; i < clips.size(); i++) {
		OakEngineBlock *c = clips.at(i);

		if (!qIsNaN(start_speed_) && !qFuzzyCompare(start_speed_, clip_speed(c))) {
			// Speed differs per clip
			start_speed_ = qSNaN();
		}

		if (start_duration_ != -1 && sdd_block_length(c) != start_duration_) {
			start_duration_ = -1;
		}

		// Yes, in theory a bool should only ever be 0 or 1 anyway, but MSVC complained and it is
		// *possible* that a bool could be something else, so this code is safer
		int clip_reverse = clip_is_reversed(c) ? 1 : 0;
		int clip_maintain_pitch = clip_maintain_audio_pitch(c) ? 1 : 0;
		if (start_reverse_ != -1 && clip_reverse != start_reverse_) {
			start_reverse_ = -1;
		}
		if (start_maintain_audio_pitch_ != -1 &&
			clip_maintain_pitch != start_maintain_audio_pitch_) {
			start_maintain_audio_pitch_ = -1;
		}

		if (start_loop_ != -1 && clip_loop_mode(c) != start_loop_) {
			start_loop_ = -1;
		}
	}

	if (qIsNaN(start_speed_)) {
		speed_slider_->set_tristate();
	} else {
		speed_slider_->set_value(start_speed_);
	}

	if (start_duration_ == -1) {
		dur_slider_->set_tristate();
	} else {
		dur_slider_->set_value(start_duration_);
	}

	if (start_reverse_ == -1) {
		reverse_box_->setTristate();
	} else {
		reverse_box_->setChecked(start_reverse_);
	}

	if (start_maintain_audio_pitch_ == -1) {
		maintain_audio_pitch_box_->setTristate();
	} else {
		maintain_audio_pitch_box_->setChecked(start_maintain_audio_pitch_);
	}

	if (start_loop_ == -1) {
		loop_combo_->setCurrentIndex(-1);
	} else {
		loop_combo_->setCurrentIndex(start_loop_);
	}
}

void SpeedDurationDialog::accept()
{
	// Collect all duration/speed changes into a single undo entry.
	const QByteArray undo_name = tr("Speed/Duration").toUtf8();
	oakengine_undo_group_begin(undo_name.constData());

	// Set speed values
	if (speed_slider_->is_tristate()) {
		if (link_box_->isChecked() && !dur_slider_->is_tristate()) {
			// Automatically determine speed from duration
			foreach (OakEngineBlock *c, clips_) {
				double speed = get_speed_adjustment(clip_speed(c),
													sdd_block_length(c),
													dur_slider_->get_value());
				oak_node_value val;
				memset(&val, 0, sizeof(val));
				val.type = OAK_NODE_VALUE_FLOAT;
				val.f[0] = speed;
				oakengine_node_set_input(
					reinterpret_cast<OakEngineNode *>(c),
					oakengine_clip_speed_input_id(), &val);
			}
		}
	} else {
		// Set speeds to value of slider
		foreach (OakEngineBlock *c, clips_) {
			oak_node_value val;
			memset(&val, 0, sizeof(val));
			val.type = OAK_NODE_VALUE_FLOAT;
			val.f[0] = speed_slider_->get_value();
			oakengine_node_set_input(
				reinterpret_cast<OakEngineNode *>(c),
				oakengine_clip_speed_input_id(), &val);
		}
	}

	// Set duration values (undoable via facade)
	// (track handle, range) pairs fed to
	// oakengine_timeline_ripple_delete_gaps_command below; was
	// TimelineRippleDeleteGapsAtRegionsCommand::RangeList.
	QVector<QPair<void *, TimeRange>> ripple_ranges;

	foreach (OakEngineBlock *c, clips_) {
		Rational proposed_length = sdd_block_length(c);

		if (dur_slider_->is_tristate()) {
			if (link_box_->isChecked() && !speed_slider_->is_tristate()) {
				proposed_length = get_length_adjustment(sdd_block_length(c),
													  clip_speed(c),
													  speed_slider_->get_value(),
													  timebase_);
			}
		} else {
			proposed_length = dur_slider_->get_value();
		}

		if (proposed_length != sdd_block_length(c)) {
			// Clip length should ideally change, but check if there's "room" to do so
			if (proposed_length > sdd_block_length(c) && sdd_block_next(c)) {
				// Gap check via the C ABI (GapBlock's type used to ride in
				// with the removed timelineundo headers)
				if (oakengine_block_is_gap(sdd_block_next(c))) {
					proposed_length =
						qMin(proposed_length,
							 sdd_block_out(sdd_block_next(c)) -
								 sdd_block_in(c));
				} else {
					proposed_length = sdd_block_length(c);
				}
			}

			if (proposed_length != sdd_block_length(c)) {
				// Trim the clip's out-point to the new length (one undoable child
				// inside the group, kept as a direct C++ command because the dialog
				// already works in Rational time and has the track available).
				oakengine_undo_push(
				oakengine_block_trim_command(
					reinterpret_cast<void *>(sdd_clip_track(c)),
					reinterpret_cast<void *>(c),
					proposed_length.numerator(),
					proposed_length.denominator(),
					TimelineApp::k_trim_out, 0),
				tr("Trim Clip").toUtf8().constData());
				ripple_ranges.append(
					{ reinterpret_cast<void *>(sdd_clip_track(c)),
					  TimeRange(sdd_block_in(c) + proposed_length,
								sdd_block_out(c)) });
			}
		}
	}

	if (ripple_box_->isChecked() && !ripple_ranges.isEmpty()) {
		OakEngineSequence *seq = oakengine_clip_get_sequence(
			reinterpret_cast<OakEngineClip *>(clips_.first()));
		if (seq) {
			QVector<int64_t> range_in_ts;
			QVector<int64_t> range_out_ts;
			QVector<int> range_track_types;
			QVector<int> range_track_indexes;
			range_in_ts.reserve(ripple_ranges.size());
			range_out_ts.reserve(ripple_ranges.size());
			range_track_types.reserve(ripple_ranges.size());
			range_track_indexes.reserve(ripple_ranges.size());
			int tbn = 0, tbd = 0;
			oakengine_node_frame_time_base(
				reinterpret_cast<OakEngineNode *>(seq), &tbn, &tbd);
			for (const auto &range : ripple_ranges) {
				range_track_types.append(oakengine_track_get_type(
					reinterpret_cast<const OakEngineNode *>(range.first)));
				range_track_indexes.append(oakengine_track_get_index(
					reinterpret_cast<const OakEngineNode *>(range.first)));
				range_in_ts.append(olive::core::Timecode::time_to_timestamp(
					range.second.in(), olive::Rational(tbn, tbd),
					olive::core::Timecode::k_round));
				range_out_ts.append(olive::core::Timecode::time_to_timestamp(
					range.second.out(), olive::Rational(tbn, tbd),
					olive::core::Timecode::k_round));
			}
			oakengine_undo_push(
				oakengine_timeline_ripple_delete_gaps_command(
					reinterpret_cast<void *>(seq),
					range_in_ts.constData(), range_out_ts.constData(),
					range_track_types.constData(),
					range_track_indexes.constData(),
					ripple_ranges.size()),
				tr("Ripple Delete Gaps").toUtf8().constData());
		}
	}

	// Set reverse values
	if (!reverse_box_->isTristate()) {
		foreach (OakEngineBlock *c, clips_) {
			oak_node_value val;
			memset(&val, 0, sizeof(val));
			val.type = OAK_NODE_VALUE_BOOL;
			val.num = reverse_box_->isChecked() ? 1 : 0;
			oakengine_node_set_input(
				reinterpret_cast<OakEngineNode *>(c),
				oakengine_clip_reverse_input_id(), &val);
		}
	}

	// Set maintain audio pitch values
	if (!maintain_audio_pitch_box_->isTristate()) {
		foreach (OakEngineBlock *c, clips_) {
			oak_node_value val;
			memset(&val, 0, sizeof(val));
			val.type = OAK_NODE_VALUE_BOOL;
			val.num = maintain_audio_pitch_box_->isChecked() ? 1 : 0;
			oakengine_node_set_input(
				reinterpret_cast<OakEngineNode *>(c),
				oakengine_clip_maintain_audio_pitch_input_id(), &val);
		}
	}

	if (loop_combo_->currentIndex() != -1) {
		foreach (OakEngineBlock *c, clips_) {
			oak_node_value val;
			memset(&val, 0, sizeof(val));
			val.type = OAK_NODE_VALUE_INT;
			val.num = loop_combo_->currentData().toInt();
			oakengine_node_set_input(
				reinterpret_cast<OakEngineNode *>(c),
				oakengine_clip_loop_mode_input_id(), &val);
		}
	}

	oakengine_undo_group_end();

	super::accept();
}
Rational SpeedDurationDialog::get_length_adjustment(
	const Rational &original_length, double original_speed, double new_speed,
	const Rational &timebase)
{
	return Timecode::snap_time_to_timebase(
		Rational::from_double(original_length.to_double() / new_speed *
							 original_speed),
		timebase);
}

double SpeedDurationDialog::get_speed_adjustment(double original_speed,
											   const Rational &original_length,
											   const Rational &new_length)
{
	return original_speed / new_length.to_double() * original_length.to_double();
}

void SpeedDurationDialog::speed_changed(double s)
{
	if (!link_box_->isChecked()) {
		return;
	}

	if (start_duration_ == -1) {
		dur_slider_->set_tristate();
	} else {
		dur_slider_->set_value(
			get_length_adjustment(start_duration_, start_speed_, s, timebase_));
	}
}

void SpeedDurationDialog::duration_changed(const Rational &r)
{
	if (!link_box_->isChecked()) {
		return;
	}

	if (qIsNaN(start_speed_)) {
		speed_slider_->set_tristate();
	} else {
		speed_slider_->set_value(
			get_speed_adjustment(start_speed_, start_duration_, r));
	}
}

}
