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

#ifndef OAK_PLAYBACKCONTROLS_H
#define OAK_PLAYBACKCONTROLS_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>

#include "dragbutton.h"
#include "widget/slider/rationalslider.h"

namespace olive
{

/**
 * @brief A playback controls widget providing buttons for navigating media
 *
 * This widget optionally features timecode displays for the current timecode and end timecode.
 */
class PlaybackControls : public QWidget {
	Q_OBJECT
public:
	PlaybackControls(QWidget *parent = nullptr);

	/**
   * @brief Set whether the timecodes should be shown or not
   */
	void set_timecode_enabled(bool enabled);

	void set_timebase(const Rational &r);

	void set_audio_video_drag_buttons_visible(bool e);

public slots:
	void set_time(const Rational &r);

	void set_end_time(const Rational &r);

	void show_pause_button();

	void show_play_button();

	void start_play_blink()
	{
		play_blink_timer_->start();
		set_button_recording_state(play_btn_, true);
	}

	void stop_play_blink()
	{
		play_blink_timer_->stop();
		set_button_recording_state(play_btn_, false);
	}

	void set_pause_button_recording_state(bool on)
	{
		set_button_recording_state(pause_btn_, on);
	}

signals:
	/**
   * @brief Signal emitted when "Go to Start" is clicked
   */
	void begin_clicked();

	/**
   * @brief Signal emitted when "Previous Frame" is clicked
   */
	void prev_frame_clicked();

	/**
   * @brief Signal emitted when "Play" is clicked
   */
	void play_clicked();

	/**
   * @brief Signal emitted when "Pause" is clicked
   */
	void pause_clicked();

	/**
   * @brief Signal emitted when "Next Frame" is clicked
   */
	void next_frame_clicked();

	/**
   * @brief Signal emitted when "Go to End" is clicked
   */
	void end_clicked();

	void audio_clicked();

	void video_clicked();

	void audio_dragged();

	void video_dragged();

	void time_changed(const Rational &t);

protected:
	virtual void changeEvent(QEvent *) override;

private:
	void update_icons();

	static void set_button_recording_state(QPushButton *btn, bool on);

	QWidget *lower_left_container_;
	QWidget *lower_right_container_;

	RationalSlider *cur_tc_lbl_;
	QLabel *end_tc_lbl_;

	Rational end_time_;

	Rational time_base_;

	QPushButton *go_to_start_btn_;
	QPushButton *prev_frame_btn_;
	QPushButton *play_btn_;
	QPushButton *pause_btn_;
	QPushButton *next_frame_btn_;
	QPushButton *go_to_end_btn_;
	DragButton *video_drag_btn_;
	DragButton *audio_drag_btn_;

	QStackedWidget *playpause_stack_;

	QTimer *play_blink_timer_;

private slots:
	void timecode_changed();

	void play_blink();
};

}

#endif // OAK_PLAYBACKCONTROLS_H
