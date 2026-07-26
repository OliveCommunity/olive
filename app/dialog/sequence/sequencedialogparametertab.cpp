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

#include "sequencedialogparametertab.h"

#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "oakengine/timeline.h"
#include "oakengine/videoparams.h"

namespace olive
{

SequenceDialogParameterTab::SequenceDialogParameterTab(Sequence *sequence,
													   QWidget *parent)
	: QWidget(parent)
{
	QVBoxLayout *layout = new QVBoxLayout(this);

	int row = 0;

	// Set up video section
	QGroupBox *video_group = new QGroupBox();
	video_group->setTitle(tr("Video"));
	QGridLayout *video_layout = new QGridLayout(video_group);
	video_layout->addWidget(new QLabel(tr("Width:")), row, 0);
	width_slider_ = new IntegerSlider();
	width_slider_->set_minimum(0);
	video_layout->addWidget(width_slider_, row, 1);
	row++;
	video_layout->addWidget(new QLabel(tr("Height:")), row, 0);
	height_slider_ = new IntegerSlider();
	height_slider_->set_minimum(0);
	video_layout->addWidget(height_slider_, row, 1);
	row++;
	video_layout->addWidget(new QLabel(tr("Frame Rate:")), row, 0);
	framerate_combo_ = new FrameRateComboBox();
	video_layout->addWidget(framerate_combo_, row, 1);
	row++;
	video_layout->addWidget(new QLabel(tr("Pixel Aspect Ratio:")), row, 0);
	pixelaspect_combo_ = new PixelAspectRatioComboBox();
	video_layout->addWidget(pixelaspect_combo_, row, 1);
	row++;
	video_layout->addWidget(new QLabel(tr("Interlacing:")), row, 0);
	interlacing_combo_ = new InterlacedComboBox();
	video_layout->addWidget(interlacing_combo_, row, 1);
	layout->addWidget(video_group);

	row = 0;

	// Set up audio section
	QGroupBox *audio_group = new QGroupBox();
	audio_group->setTitle(tr("Audio"));
	QGridLayout *audio_layout = new QGridLayout(audio_group);
	audio_layout->addWidget(new QLabel(tr("Sample Rate:")), row, 0);
	audio_sample_rate_field_ = new SampleRateComboBox();
	audio_layout->addWidget(audio_sample_rate_field_, row, 1);
	row++;
	audio_layout->addWidget(new QLabel(tr("Channels:")), row, 0);
	audio_channels_field_ = new ChannelLayoutComboBox();
	audio_layout->addWidget(audio_channels_field_, row, 1);
	layout->addWidget(audio_group);

	row = 0;

	// Set up preview section
	QGroupBox *preview_group = new QGroupBox();
	preview_group->setTitle(tr("Preview"));
	QGridLayout *preview_layout = new QGridLayout(preview_group);
	preview_layout->addWidget(new QLabel(tr("Resolution:")), row, 0);
	preview_resolution_field_ = new VideoDividerComboBox();
	preview_layout->addWidget(preview_resolution_field_, row, 1);
	preview_resolution_label_ = new QLabel();
	preview_layout->addWidget(preview_resolution_label_, row, 2);
	row++;
	preview_layout->addWidget(new QLabel(tr("Quality:")), row, 0);
	preview_format_field_ = new PixelFormatComboBox(false);
	preview_layout->addWidget(preview_format_field_, row, 1, 1, 2);

	/* TEMP: Disable sequence auto-cache, wanna see if clip cache supersedes it.
  row++;
  preview_layout->addWidget(new QLabel(tr("Auto-Cache:")), row, 0);
  preview_layout->addWidget(preview_autocache_field_, row, 1);*/
	preview_autocache_field_ = new QCheckBox();

	layout->addWidget(preview_group);

	// Set values based on input sequence; the reads go through the
	// liboakengine C ABI facade (the sequence handle is the engine node
	// pointer in this family).
	OakEngineSequence *facade_handle =
		reinterpret_cast<OakEngineSequence *>(sequence);
	int width = 0, height = 0, fps_num = 0, fps_den = 1, par_num = 1,
		par_den = 1, interlacing = 0, format = 0, divider = 1;
	oakengine_sequence_get_video_params_ex(facade_handle, &width, &height,
										   &fps_num, &fps_den, &par_num,
										   &par_den, &interlacing, &format,
										   &divider);
	int sample_rate = 0;
	uint64_t channel_layout = 0;
	oakengine_sequence_get_audio_params(facade_handle, &sample_rate,
										&channel_layout);

	width_slider_->set_value(width);
	height_slider_->set_value(height);
	framerate_combo_->set_frame_rate(Rational(fps_num, fps_den));
	pixelaspect_combo_->set_pixel_aspect_ratio(Rational(par_num, par_den));
	interlacing_combo_->set_interlace_mode(interlacing);
	preview_resolution_field_->set_divider(divider);
	preview_format_field_->set_pixel_format(
		static_cast<PixelFormat::Format>(format));
	preview_autocache_field_->setChecked(
		oakengine_sequence_get_video_auto_cache(facade_handle) != 0);
	audio_sample_rate_field_->set_sample_rate(sample_rate);
	audio_channels_field_->set_channel_layout(channel_layout);

	connect(
		preview_resolution_field_,
		static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		this, &SequenceDialogParameterTab::update_preview_resolution_label);

	layout->addStretch();

	QPushButton *save_preset_btn = new QPushButton(tr("Save Preset"));
	connect(save_preset_btn, &QPushButton::clicked, this,
			&SequenceDialogParameterTab::save_preset_clicked);
	layout->addWidget(save_preset_btn);

	update_preview_resolution_label();
}

void SequenceDialogParameterTab::preset_changed(const SequencePreset &preset)
{
	width_slider_->set_value(preset.width());
	height_slider_->set_value(preset.height());
	framerate_combo_->set_frame_rate(preset.frame_rate());
	pixelaspect_combo_->set_pixel_aspect_ratio(preset.pixel_aspect());
	interlacing_combo_->set_interlace_mode(preset.interlacing());
	audio_sample_rate_field_->set_sample_rate(preset.sample_rate());
	audio_channels_field_->set_channel_layout(preset.channel_layout());
	preview_resolution_field_->set_divider(preset.preview_divider());
	preview_format_field_->set_pixel_format(preset.preview_format());
	preview_autocache_field_->setChecked(preset.preview_autocache());
}

void SequenceDialogParameterTab::save_preset_clicked()
{
	emit save_parameters_as_preset(SequencePreset(
		QString(), get_selected_video_width(), get_selected_video_height(),
		get_selected_video_frame_rate(), get_selected_video_pixel_aspect(),
		get_selected_video_interlacing_mode(), get_selected_audio_sample_rate(),
		get_selected_audio_channel_layout(), get_selected_preview_resolution(),
		get_selected_preview_format(), get_selected_preview_auto_cache()));
}

void SequenceDialogParameterTab::update_preview_resolution_label()
{
	int ew, eh;
	oakengine_video_params_effective_size(
		get_selected_video_width(), get_selected_video_height(),
		preview_resolution_field_->currentData().toInt(), &ew, &eh);

	preview_resolution_label_->setText(
		tr("(%1x%2)").arg(QString::number(ew),
						  QString::number(eh)));
}

}
