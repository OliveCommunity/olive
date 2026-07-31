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

#include "exportvideotab.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>

#include "exportadvancedvideodialog.h"
#include "oakengine/encoding.h"

namespace olive
{

ExportVideoTab::ExportVideoTab(OakEngineColorManager *color_manager, QWidget *parent)
	: QWidget(parent)
	, color_manager_(color_manager)
	, threads_(0)
	, color_range_(0) // k_color_range_default
{
	QVBoxLayout *outer_layout = new QVBoxLayout(this);

	outer_layout->addWidget(setup_resolution_section());

	outer_layout->addWidget(setup_codec_section());

	outer_layout->addWidget(setup_color_section());

	outer_layout->addStretch();
}

int ExportVideoTab::set_format(int format)
{
	format_ = format;

	const int vcodec_count = oakengine_encoding_format_video_codec_count(format);
	setEnabled(vcodec_count > 0);
	codec_combobox()->clear();
	for (int i = 0; i < vcodec_count; i++) {
		int vcodec = oakengine_encoding_format_video_codec_at(format, i);
		char buf[256];
		oakengine_encoding_codec_name(vcodec, buf, sizeof(buf));
		codec_combobox()->addItem(QString::fromUtf8(buf), vcodec);
	}
	return vcodec_count;
}

bool ExportVideoTab::is_image_sequence_set() const
{
	ImageSection *img_section =
		dynamic_cast<ImageSection *>(codec_stack_->currentWidget());

	return (img_section && img_section->is_image_sequence_checked());
}

void ExportVideoTab::set_image_sequence(bool e) const
{
	if (ImageSection *img_section =
			dynamic_cast<ImageSection *>(codec_stack_->currentWidget())) {
		img_section->set_image_sequence_checked(e);
	}
}

QWidget *ExportVideoTab::setup_resolution_section()
{
	int row = 0;

	QGroupBox *resolution_group = new QGroupBox();
	resolution_group->setTitle(tr("General"));

	QGridLayout *layout = new QGridLayout(resolution_group);

	layout->addWidget(new QLabel(tr("Width:")), row, 0);

	width_slider_ = new IntegerSlider();
	width_slider_->set_minimum(1);
	layout->addWidget(width_slider_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Height:")), row, 0);

	height_slider_ = new IntegerSlider();
	height_slider_->set_minimum(1);
	layout->addWidget(height_slider_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Maintain Aspect Ratio:")), row, 0);

	maintain_aspect_checkbox_ = new QCheckBox();
	maintain_aspect_checkbox_->setChecked(true);
	layout->addWidget(maintain_aspect_checkbox_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Scaling Method:")), row, 0);

	scaling_method_combobox_ = new QComboBox();
	scaling_method_combobox_->setEnabled(false);
	scaling_method_combobox_->addItem(tr("Fit"), OAKENGINE_ENCODING_SCALING_FIT);
	scaling_method_combobox_->addItem(tr("Stretch"), OAKENGINE_ENCODING_SCALING_STRETCH);
	scaling_method_combobox_->addItem(tr("Crop"), OAKENGINE_ENCODING_SCALING_CROP);
	layout->addWidget(scaling_method_combobox_, row, 1);

	// Automatically enable/disable the scaling method depending on maintain aspect ratio
	connect(maintain_aspect_checkbox_, &QCheckBox::toggled, this,
			&ExportVideoTab::maintain_aspect_ratio_changed);

	row++;

	layout->addWidget(new QLabel(tr("Frame Rate:")), row, 0);

	frame_rate_combobox_ = new FrameRateComboBox();
	connect(frame_rate_combobox_, &FrameRateComboBox::frame_rate_changed, this,
			&ExportVideoTab::update_frame_rate);
	layout->addWidget(frame_rate_combobox_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Pixel Aspect Ratio:")), row, 0);

	pixel_aspect_combobox_ = new PixelAspectRatioComboBox();
	layout->addWidget(pixel_aspect_combobox_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Interlacing:")), row, 0);

	interlaced_combobox_ = new InterlacedComboBox();
	layout->addWidget(interlaced_combobox_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Quality:")), row, 0);

	pixel_format_field_ = new PixelFormatComboBox(false);
	layout->addWidget(pixel_format_field_, row, 1);

	return resolution_group;
}

QWidget *ExportVideoTab::setup_color_section()
{
	color_space_chooser_ = new ColorSpaceChooser(color_manager_, true, false);
	connect(color_space_chooser_, &ColorSpaceChooser::input_color_space_changed,
			this, &ExportVideoTab::color_space_changed);
	return color_space_chooser_;
}

QWidget *ExportVideoTab::setup_codec_section()
{
	int row = 0;

	QGroupBox *codec_group = new QGroupBox();
	codec_group->setTitle(tr("Codec"));

	QGridLayout *codec_layout = new QGridLayout(codec_group);

	codec_layout->addWidget(new QLabel(tr("Codec:")), row, 0);

	codec_combobox_ = new QComboBox();
	codec_layout->addWidget(codec_combobox_, row, 1);
	connect(
		codec_combobox_,
		static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		this, &ExportVideoTab::video_codec_changed);

	row++;

	codec_stack_ = new CodecStack();
	codec_layout->addWidget(codec_stack_, row, 0, 1, 2);

	image_section_ = new ImageSection();
	connect(image_section_, &ImageSection::time_changed, this,
			&ExportVideoTab::time_changed);
	codec_stack_->addWidget(image_section_);

	h264_section_ = new H264Section();
	codec_stack_->addWidget(h264_section_);

	h265_section_ = new H265Section();
	codec_stack_->addWidget(h265_section_);

	av1_section_ = new AV1Section();
	codec_stack_->addWidget(av1_section_);

	cineform_section_ = new CineformSection();
	codec_stack_->addWidget(cineform_section_);

	row++;

	QPushButton *advanced_btn = new QPushButton(tr("Advanced"));
	connect(advanced_btn, &QPushButton::clicked, this,
			&ExportVideoTab::open_advanced_dialog);
	codec_layout->addWidget(advanced_btn, row, 1);

	return codec_group;
}

void ExportVideoTab::maintain_aspect_ratio_changed(bool val)
{
	scaling_method_combobox_->setEnabled(!val);
}

void ExportVideoTab::open_advanced_dialog()
{
	// Find pixel formats compatible with this encoder
	QStringList pixel_formats;
	const int pix_count = oakengine_encoding_pix_fmt_count(format_, get_selected_codec());
	for (int i = 0; i < pix_count; i++) {
		char buf[64];
		oakengine_encoding_pix_fmt_at(format_, get_selected_codec(), i, buf, sizeof(buf));
		pixel_formats.append(QString::fromUtf8(buf));
	}

	ExportAdvancedVideoDialog d(pixel_formats, this);

	d.set_threads(threads_);
	d.set_pix_fmt(pix_fmt_);
	d.set_yuv_range(color_range_);

	if (d.exec() == QDialog::Accepted) {
		threads_ = d.threads();
		pix_fmt_ = d.pix_fmt();
		color_range_ = d.yuv_range();
	}
}

void ExportVideoTab::update_frame_rate(Rational r)
{
	// Convert frame rate to timebase
	r.flip();

	for (int i = 0; i < codec_stack_->count(); i++) {
		ImageSection *img =
			dynamic_cast<ImageSection *>(codec_stack_->widget(i));
		if (img) {
			img->set_timebase(r);
		}
	}
}

void ExportVideoTab::video_codec_changed()
{
	int codec = get_selected_codec();

	switch (codec) {
	case OAKENGINE_ENCODING_CODEC_H264:
	case OAKENGINE_ENCODING_CODEC_H264RGB:
		set_codec_section(h264_section_);
		break;
	case OAKENGINE_ENCODING_CODEC_H265:
		set_codec_section(h265_section_);
		break;
	case OAKENGINE_ENCODING_CODEC_AV1:
		set_codec_section(av1_section_);
		break;
	case OAKENGINE_ENCODING_CODEC_CINEFORM:
		set_codec_section(cineform_section_);
		break;
	default:
		set_codec_section(
			oakengine_encoding_codec_is_still_image(codec) ? image_section_ : nullptr);
	}

	// Set default pixel format
	QStringList pix_fmts;
	const int pix_count = oakengine_encoding_pix_fmt_count(format_, codec);
	for (int i = 0; i < pix_count; i++) {
		char buf[64];
		oakengine_encoding_pix_fmt_at(format_, codec, i, buf, sizeof(buf));
		pix_fmts.append(QString::fromUtf8(buf));
	}
	if (!pix_fmts.isEmpty()) {
		pix_fmt_ = pix_fmts.first();
	} else {
		pix_fmt_.clear();
	}
}

void ExportVideoTab::set_time(const Rational &time)
{
	for (int i = 0; i < codec_stack_->count(); i++) {
		ImageSection *img =
			dynamic_cast<ImageSection *>(codec_stack_->widget(i));
		if (img) {
			img->set_time(time);
		}
	}
}

}
