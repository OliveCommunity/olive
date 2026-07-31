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

#ifndef OAK_SEQUENCEDIALOGPARAMETERTAB_H
#define OAK_SEQUENCEDIALOGPARAMETERTAB_H

#include <QCheckBox>
#include <QComboBox>
#include <QList>
#include <QSpinBox>

#include "sequencepreset.h"
#include "widget/slider/integerslider.h"
#include "widget/standardcombos/standardcombos.h"

struct OakEngineNode;

namespace olive
{

class SequenceDialogParameterTab : public QWidget {
	Q_OBJECT
public:
	SequenceDialogParameterTab(OakEngineNode *sequence,
							   QWidget *parent = nullptr);

	int get_selected_video_width() const
	{
		return width_slider_->get_value();
	}

	int get_selected_video_height() const
	{
		return height_slider_->get_value();
	}

	Rational get_selected_video_frame_rate() const
	{
		return framerate_combo_->get_frame_rate();
	}

	Rational get_selected_video_pixel_aspect() const
	{
		return pixelaspect_combo_->get_pixel_aspect_ratio();
	}

	int get_selected_video_interlacing_mode() const
	{
		return interlacing_combo_->get_interlace_mode();
	}

	int get_selected_audio_sample_rate() const
	{
		return audio_sample_rate_field_->get_sample_rate();
	}

	[[nodiscard]] uint64_t get_selected_audio_channel_layout() const
	{
		return audio_channels_field_->get_channel_layout();
	}

	int get_selected_preview_resolution() const
	{
		return preview_resolution_field_->get_divider();
	}

	PixelFormat get_selected_preview_format() const
	{
		return preview_format_field_->get_pixel_format();
	}

	bool get_selected_preview_auto_cache() const
	{
		//return preview_autocache_field_->isChecked();
		// TEMP: Disable sequence auto-cache, wanna see if clip cache supersedes it.
		return false;
	}

public slots:
	void preset_changed(const SequencePreset &preset);

signals:
	void save_parameters_as_preset(const SequencePreset &preset);

private:
	IntegerSlider *width_slider_;

	IntegerSlider *height_slider_;

	FrameRateComboBox *framerate_combo_;

	PixelAspectRatioComboBox *pixelaspect_combo_;

	InterlacedComboBox *interlacing_combo_;

	SampleRateComboBox *audio_sample_rate_field_;

	ChannelLayoutComboBox *audio_channels_field_;

	VideoDividerComboBox *preview_resolution_field_;

	QLabel *preview_resolution_label_;

	PixelFormatComboBox *preview_format_field_;

	QCheckBox *preview_autocache_field_;

private slots:
	void save_preset_clicked();

	void update_preview_resolution_label();
};

}

#endif // OAK_SEQUENCEDIALOGPARAMETERTAB_H
