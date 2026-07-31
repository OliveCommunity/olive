/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2020 Olive Team
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

#include "videostreamproperties.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>

#include "oakengine/color.h"
#include "widget/manageddisplay/colorprocessorhandle.h"
#include "oakengine/footage.h"
#include "oakengine/node.h"
#include "oakengine/viewer.h"
#include "oakengine/videoparams.h"

namespace olive
{

VideoStreamProperties::VideoStreamProperties(OakEngineNode *footage,
											 int video_index)
	: footage_(footage)
	, video_index_(video_index)
	, video_premultiply_alpha_(nullptr)
{
	QGridLayout *video_layout = new QGridLayout(this);
	video_layout->setContentsMargins(0, 0, 0, 0);

	int row = 0;

	video_layout->addWidget(new QLabel(tr("Pixel Aspect:")), row, 0);

	oak_video_params vpod;
	oakengine_viewer_get_video_params(footage_, video_index_, &vpod);

	// Stream override values come through the liboakengine C ABI facade;
	// layout-only conditions (channel count, video type) stay direct reads.
	OakEngineFootage *facade_handle = oakengine_footage_borrow(footage_);
	char colorspace_buf[256];
	int color_range = 0;
	int interlacing = 0;
	int premultiplied = 0;
	oakengine_footage_get_video_stream_overrides(
		facade_handle, video_index_, colorspace_buf, sizeof(colorspace_buf),
		&color_range, &interlacing, &premultiplied);
	int par_num = 1, par_den = 1;
	oakengine_footage_get_pixel_aspect(facade_handle, video_index_, &par_num,
									   &par_den);

	pixel_aspect_combo_ = new PixelAspectRatioComboBox();
	pixel_aspect_combo_->set_pixel_aspect_ratio(Rational(par_num, par_den));
	video_layout->addWidget(pixel_aspect_combo_, row, 1);

	row++;

	video_layout->addWidget(new QLabel(tr("Interlacing:")), row, 0);

	video_interlace_combo_ = new InterlacedComboBox();
	video_interlace_combo_->set_interlace_mode(interlacing);

	video_layout->addWidget(video_interlace_combo_, row, 1);

	row++;

	video_layout->addWidget(new QLabel(tr("Color Space:")), row, 0);

	video_color_space_ = new QComboBox();

	// The dropdown's color space list comes through the facade (same list
	// the engine's color config reports).
	OakEngineColorManager *cm = oakengine_color_manager_from_project(
		oakengine_node_get_project(footage_));
	video_color_space_->addItem(tr("Default (%1)")
									.arg(oak_query_string([cm](char *buf, int size) {
										return oakengine_color_manager_default_input_color_space(
											cm, buf, size);
									})));

	const int colorspace_count =
		oakengine_footage_colorspace_count(facade_handle);
	for (int i = 0; i < colorspace_count; i++) {
		char name_buf[256];
		if (oakengine_footage_colorspace_at(facade_handle, i, name_buf,
											sizeof(name_buf)) > 0) {
			video_color_space_->addItem(QString::fromUtf8(name_buf));
		}
	}

	video_color_space_->setCurrentText(QString::fromUtf8(colorspace_buf));

	video_layout->addWidget(video_color_space_, row, 1);

	row++;

	video_layout->addWidget(new QLabel(tr("Color Range:")), row, 0);

	color_range_combo_ = new QComboBox();
	color_range_combo_->addItem(tr("Limited (16-235)"),
								0);
	color_range_combo_->addItem(tr("Full (0-255)"),
								1);
	color_range_combo_->setCurrentIndex(color_range);

	video_layout->addWidget(color_range_combo_, row, 1);

	if (oakengine_video_params_internal_channel_count() == 4) {
		row++;

		video_premultiply_alpha_ = new QCheckBox(tr("Premultiplied Alpha"));
		video_premultiply_alpha_->setChecked(premultiplied != 0);
		video_layout->addWidget(video_premultiply_alpha_, row, 0, 1, 2);
	}

	row++;

	if (vpod.video_type == 2) {
		QGroupBox *imgseq_group = new QGroupBox(tr("Image Sequence"));
		QGridLayout *imgseq_layout = new QGridLayout(imgseq_group);

		int imgseq_row = 0;

		imgseq_layout->addWidget(new QLabel(tr("Start Index:")), imgseq_row, 0);

		int64_t seq_start = 0, seq_duration = 0;
		int fr_num = 0, fr_den = 1;
		oakengine_footage_get_image_sequence_params(
			facade_handle, video_index_, &seq_start, &seq_duration, &fr_num,
			&fr_den);

		imgseq_start_time_ = new IntegerSlider();
		imgseq_start_time_->set_minimum(0);
		imgseq_start_time_->set_value(seq_start);
		imgseq_layout->addWidget(imgseq_start_time_, imgseq_row, 1);

		imgseq_row++;

		imgseq_layout->addWidget(new QLabel(tr("End Index:")), imgseq_row, 0);

		imgseq_end_time_ = new IntegerSlider();
		imgseq_end_time_->set_minimum(0);
		imgseq_end_time_->set_value(seq_start + seq_duration - 1);
		imgseq_layout->addWidget(imgseq_end_time_, imgseq_row, 1);

		imgseq_row++;

		imgseq_layout->addWidget(new QLabel(tr("Frame Rate:")), imgseq_row, 0);

		imgseq_frame_rate_ = new FrameRateComboBox();
		imgseq_frame_rate_->set_frame_rate(Rational(fr_num, fr_den));
		imgseq_layout->addWidget(imgseq_frame_rate_, imgseq_row, 1);

		video_layout->addWidget(imgseq_group, row, 0, 1, 2);
	}

	oakengine_footage_free(facade_handle);
}

void VideoStreamProperties::accept(void *parent)
{
	Q_UNUSED(parent)

	OakEngineFootage *facade_handle = oakengine_footage_borrow(footage_);

	QString set_colorspace;

	if (video_color_space_->currentIndex() > 0) {
		set_colorspace = video_color_space_->currentText();
	}

	// Fetch current values through the facade (avoids the inline
	// ViewerOutput::get_video_params() which references k_video_params_input).
	char vp_colorspace[256];
	vp_colorspace[0] = '\0';
	int vp_color_range = 0, vp_interlacing = 0, vp_premultiplied = 0;
	oakengine_footage_get_video_stream_overrides(
		facade_handle, video_index_, vp_colorspace, sizeof(vp_colorspace),
		&vp_color_range, &vp_interlacing, &vp_premultiplied);

	int vp_par_num = 1, vp_par_den = 1;
	oakengine_footage_get_pixel_aspect(facade_handle, video_index_,
									   &vp_par_num, &vp_par_den);

	oak_video_params vpod;
	oakengine_viewer_get_video_params(footage_, video_index_, &vpod);

	int64_t vp_start_time = 0, vp_duration = 0;
	int vp_fr_num = 0, vp_fr_den = 1;
	oakengine_footage_get_image_sequence_params(
		facade_handle, video_index_, &vp_start_time, &vp_duration,
		&vp_fr_num, &vp_fr_den);

	// Write every override through the facade (each call is one undoable
	// command on the shared undo stack, replacing this dialog's own undo
	// command classes with identical semantics).
	if ((video_premultiply_alpha_ &&
		 video_premultiply_alpha_->isChecked() != (vp_premultiplied != 0)) ||
		set_colorspace != QString::fromUtf8(vp_colorspace) ||
		video_interlace_combo_->currentIndex() != vp_interlacing ||
		color_range_combo_->currentData().toInt() != vp_color_range) {
		oakengine_footage_set_video_stream_overrides(
			facade_handle, video_index_,
			set_colorspace.toUtf8().constData(),
			color_range_combo_->currentData().toInt(),
			video_interlace_combo_->currentIndex(),
			video_premultiply_alpha_ ?
				(video_premultiply_alpha_->isChecked() ? 1 : 0) :
				-1);
	}

	const Rational new_par = pixel_aspect_combo_->get_pixel_aspect_ratio();
	if (new_par != Rational(vp_par_num, vp_par_den)) {
		oakengine_footage_set_pixel_aspect(facade_handle, video_index_,
										   new_par.numerator(),
										   new_par.denominator());
	}

	if (vpod.video_type == 2) {
		int64_t new_dur =
			imgseq_end_time_->get_value() - imgseq_start_time_->get_value() + 1;

		if (vp_start_time != imgseq_start_time_->get_value() ||
			vp_duration != new_dur ||
			Rational(vp_fr_num, vp_fr_den) != imgseq_frame_rate_->get_frame_rate()) {
			const Rational fr = imgseq_frame_rate_->get_frame_rate();
			oakengine_footage_set_image_sequence_params(
				facade_handle, video_index_,
				imgseq_start_time_->get_value(), new_dur, fr.numerator(),
				fr.denominator());
		}
	}

	oakengine_footage_free(facade_handle);
}

bool VideoStreamProperties::sanity_check()
{
	oak_video_params vpod;
	oakengine_viewer_get_video_params(footage_, video_index_, &vpod);
	if (vpod.video_type == 2) {
		if (imgseq_start_time_->get_value() >= imgseq_end_time_->get_value()) {
			QMessageBox::critical(
				this, tr("Invalid Configuration"),
				tr("Image sequence end index must be a value higher than the start index."),
				QMessageBox::Ok);
			return false;
		}
	}

	return true;
}

}
