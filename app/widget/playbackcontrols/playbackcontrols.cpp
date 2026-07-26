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

#include "playbackcontrols.h"

#include <QDebug>
#include <QEvent>
#include <QHBoxLayout>

#include "core.h"
#include "common/configwrapper.h"
#include "ui/icons/icons.h"

namespace olive
{

PlaybackControls::PlaybackControls(QWidget *parent)
	: QWidget(parent)
	, time_base_(0)
{
	// Create lower controls
	QHBoxLayout *lower_control_layout = new QHBoxLayout(this);
	lower_control_layout->setSpacing(0);
	lower_control_layout->setContentsMargins(0, 0, 0, 0);

	QSizePolicy lower_container_size_policy(QSizePolicy::Maximum,
											QSizePolicy::Expanding);
	lower_container_size_policy.setHorizontalStretch(1);

	// In the lower-left, we create a current timecode label wrapped in a QWidget for fixed sizing
	lower_left_container_ = new QWidget();
	lower_left_container_->setVisible(false);
	lower_left_container_->setSizePolicy(lower_container_size_policy);

	lower_control_layout->addWidget(lower_left_container_);

	QHBoxLayout *lower_left_layout = new QHBoxLayout(lower_left_container_);
	lower_left_layout->setSpacing(0);
	lower_left_layout->setContentsMargins(0, 0, 0, 0);

	cur_tc_lbl_ = new RationalSlider();
	cur_tc_lbl_->set_display_type(slider::k_time);
	cur_tc_lbl_->set_minimum(0);
	connect(cur_tc_lbl_, &RationalSlider::value_changed, this,
			&PlaybackControls::time_changed);
	lower_left_layout->addWidget(cur_tc_lbl_);
	lower_left_layout->addStretch();

	// This is only here
	QWidget *blank_widget = new QWidget();
	//new QHBoxLayout(blank_widget);
	blank_widget->setSizePolicy(lower_container_size_policy);
	lower_control_layout->addWidget(blank_widget);

	// In the lower-middle, we create playback control buttons
	QWidget *lower_middle_container = new QWidget();
	lower_middle_container->setSizePolicy(lower_container_size_policy);
	lower_control_layout->addWidget(lower_middle_container);

	QHBoxLayout *lower_middle_layout = new QHBoxLayout(lower_middle_container);
	lower_middle_layout->setSpacing(0);
	lower_middle_layout->setContentsMargins(0, 0, 0, 0);
	lower_middle_layout->addStretch();

	QSizePolicy btn_sz_policy(QSizePolicy::Maximum, QSizePolicy::Preferred);

	// Go To Start Button
	go_to_start_btn_ = new QPushButton();
	go_to_start_btn_->setSizePolicy(btn_sz_policy);
	lower_middle_layout->addWidget(go_to_start_btn_);
	connect(go_to_start_btn_, &QPushButton::clicked, this,
			&PlaybackControls::begin_clicked);

	// Prev Frame Button
	prev_frame_btn_ = new QPushButton();
	prev_frame_btn_->setSizePolicy(btn_sz_policy);
	lower_middle_layout->addWidget(prev_frame_btn_);
	connect(prev_frame_btn_, &QPushButton::clicked, this,
			&PlaybackControls::prev_frame_clicked);

	// Play/Pause Button
	playpause_stack_ = new QStackedWidget();
	playpause_stack_->setSizePolicy(btn_sz_policy);
	lower_middle_layout->addWidget(playpause_stack_);

	play_btn_ = new QPushButton();
	playpause_stack_->addWidget(play_btn_);
	connect(play_btn_, &QPushButton::clicked, this,
			&PlaybackControls::play_clicked);

	pause_btn_ = new QPushButton();
	playpause_stack_->addWidget(pause_btn_);
	connect(pause_btn_, &QPushButton::clicked, this,
			&PlaybackControls::pause_clicked);

	// Default to showing play button
	playpause_stack_->setCurrentWidget(play_btn_);

	// Next Frame Button
	next_frame_btn_ = new QPushButton();
	next_frame_btn_->setSizePolicy(btn_sz_policy);
	lower_middle_layout->addWidget(next_frame_btn_);
	connect(next_frame_btn_, &QPushButton::clicked, this,
			&PlaybackControls::next_frame_clicked);

	// Go To End Button
	go_to_end_btn_ = new QPushButton();
	go_to_end_btn_->setSizePolicy(btn_sz_policy);
	lower_middle_layout->addWidget(go_to_end_btn_);
	connect(go_to_end_btn_, &QPushButton::clicked, this,
			&PlaybackControls::end_clicked);

	lower_middle_layout->addStretch();

	QWidget *av_btn_widget = new QWidget();
	av_btn_widget->setSizePolicy(lower_container_size_policy);
	QHBoxLayout *av_btn_layout = new QHBoxLayout(av_btn_widget);
	av_btn_layout->setSpacing(0);
	av_btn_layout->setContentsMargins(0, 0, 0, 0);
	video_drag_btn_ = new DragButton();
	connect(video_drag_btn_, &QPushButton::clicked, this,
			&PlaybackControls::video_clicked);
	connect(video_drag_btn_, &DragButton::drag_started, this,
			&PlaybackControls::video_dragged);
	av_btn_layout->addWidget(video_drag_btn_);
	audio_drag_btn_ = new DragButton();
	connect(audio_drag_btn_, &QPushButton::clicked, this,
			&PlaybackControls::audio_clicked);
	connect(audio_drag_btn_, &DragButton::drag_started, this,
			&PlaybackControls::audio_dragged);
	av_btn_layout->addWidget(audio_drag_btn_);
	lower_control_layout->addWidget(av_btn_widget);

	// The lower-right, we create another timecode label, this time to show the end timecode
	lower_right_container_ = new QWidget();
	lower_right_container_->setVisible(false);
	lower_right_container_->setSizePolicy(lower_container_size_policy);
	lower_control_layout->addWidget(lower_right_container_);

	QHBoxLayout *lower_right_layout = new QHBoxLayout(lower_right_container_);
	lower_right_layout->setSpacing(0);
	lower_right_layout->setContentsMargins(0, 0, 0, 0);

	lower_right_layout->addStretch();
	end_tc_lbl_ = new QLabel();
	lower_right_layout->addWidget(end_tc_lbl_);

	update_icons();

	set_timebase(0);

	set_audio_video_drag_buttons_visible(false);

	connect(Core::instance(), &Core::timecode_display_changed, this,
			&PlaybackControls::timecode_changed);

	play_blink_timer_ = new QTimer(this);
	play_blink_timer_->setInterval(500);
	connect(play_blink_timer_, &QTimer::timeout, this,
			&PlaybackControls::play_blink);
}

void PlaybackControls::set_timecode_enabled(bool enabled)
{
	lower_left_container_->setVisible(enabled);
	lower_right_container_->setVisible(enabled);
}

void PlaybackControls::set_timebase(const Rational &r)
{
	time_base_ = r;
	cur_tc_lbl_->set_timebase(r);

	cur_tc_lbl_->setVisible(!r.isNull());
	end_tc_lbl_->setVisible(!r.isNull());

	setEnabled(!r.isNull());
}

void PlaybackControls::set_audio_video_drag_buttons_visible(bool e)
{
	video_drag_btn_->setVisible(e);
	audio_drag_btn_->setVisible(e);
}

void PlaybackControls::set_time(const Rational &r)
{
	cur_tc_lbl_->set_value(r);
}

void PlaybackControls::set_end_time(const Rational &r)
{
	if (time_base_.isNull()) {
		return;
	}

	end_time_ = r;

	end_tc_lbl_->setText(QString::fromStdString(Timecode::time_to_timecode(
		end_time_, time_base_, Core::instance()->get_timecode_display())));
}

void PlaybackControls::show_pause_button()
{
	// Play was clicked, toggle to pause
	playpause_stack_->setCurrentWidget(pause_btn_);
}

void PlaybackControls::show_play_button()
{
	playpause_stack_->setCurrentWidget(play_btn_);
}

void PlaybackControls::changeEvent(QEvent *e)
{
	QWidget::changeEvent(e);

	if (e->type() == QEvent::StyleChange) {
		update_icons();
	}
}

void PlaybackControls::update_icons()
{
	go_to_start_btn_->setIcon(icon::go_to_start);
	prev_frame_btn_->setIcon(icon::prev_frame);
	play_btn_->setIcon(icon::play);
	pause_btn_->setIcon(icon::pause);
	next_frame_btn_->setIcon(icon::next_frame);
	go_to_end_btn_->setIcon(icon::go_to_end);
	video_drag_btn_->setIcon(icon::video);
	audio_drag_btn_->setIcon(icon::audio);
}

void PlaybackControls::set_button_recording_state(QPushButton *btn, bool on)
{
	btn->setStyleSheet(on ? QStringLiteral("background: red;") : QString());
}

void PlaybackControls::timecode_changed()
{
	// Update end time
	set_end_time(end_time_);
}

void PlaybackControls::play_blink()
{
	set_button_recording_state(play_btn_, play_btn_->styleSheet().isEmpty());
}

}
