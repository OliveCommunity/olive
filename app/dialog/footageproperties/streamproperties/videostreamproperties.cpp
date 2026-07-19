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

#include "node/project.h"

namespace olive
{

VideoStreamProperties::VideoStreamProperties(Footage *footage, int video_index)
	: footage_(footage)
	, video_index_(video_index)
	, video_premultiply_alpha_(nullptr)
{
	QGridLayout *video_layout = new QGridLayout(this);
	video_layout->setContentsMargins(0, 0, 0, 0);

	int row = 0;

	video_layout->addWidget(new QLabel(tr("Pixel Aspect:")), row, 0);

	VideoParams vp = footage_->get_video_params(video_index_);

	pixel_aspect_combo_ = new PixelAspectRatioComboBox();
	pixel_aspect_combo_->set_pixel_aspect_ratio(vp.pixel_aspect_ratio());
	video_layout->addWidget(pixel_aspect_combo_, row, 1);

	row++;

	video_layout->addWidget(new QLabel(tr("Interlacing:")), row, 0);

	video_interlace_combo_ = new InterlacedComboBox();
	video_interlace_combo_->set_interlace_mode(vp.interlacing());

	video_layout->addWidget(video_interlace_combo_, row, 1);

	row++;

	video_layout->addWidget(new QLabel(tr("Color Space:")), row, 0);

	video_color_space_ = new QComboBox();
	ocio::ConstConfigRcPtr config =
		footage_->project()->color_manager()->get_config();
	int number_of_colorspaces = config->getNumColorSpaces();

	video_color_space_->addItem(tr("Default (%1)")
									.arg(footage_->project()
											 ->color_manager()
											 ->get_default_input_color_space()));

	for (int i = 0; i < number_of_colorspaces; i++) {
		QString colorspace = config->getColorSpaceNameByIndex(i);

		video_color_space_->addItem(colorspace);
	}

	video_color_space_->setCurrentText(vp.colorspace());

	video_layout->addWidget(video_color_space_, row, 1);

	row++;

	video_layout->addWidget(new QLabel(tr("Color Range:")), row, 0);

	color_range_combo_ = new QComboBox();
	color_range_combo_->addItem(tr("Limited (16-235)"),
								VideoParams::k_color_range_limited);
	color_range_combo_->addItem(tr("Full (0-255)"),
								VideoParams::k_color_range_full);
	color_range_combo_->setCurrentIndex(vp.color_range());

	video_layout->addWidget(color_range_combo_, row, 1);

	if (vp.channel_count() == VideoParams::k_rgba_channel_count) {
		row++;

		video_premultiply_alpha_ = new QCheckBox(tr("Premultiplied Alpha"));
		video_premultiply_alpha_->setChecked(vp.premultiplied_alpha());
		video_layout->addWidget(video_premultiply_alpha_, row, 0, 1, 2);
	}

	row++;

	if (vp.video_type() == VideoParams::k_video_type_image_sequence) {
		QGroupBox *imgseq_group = new QGroupBox(tr("Image Sequence"));
		QGridLayout *imgseq_layout = new QGridLayout(imgseq_group);

		int imgseq_row = 0;

		imgseq_layout->addWidget(new QLabel(tr("Start Index:")), imgseq_row, 0);

		imgseq_start_time_ = new IntegerSlider();
		imgseq_start_time_->set_minimum(0);
		imgseq_start_time_->set_value(vp.start_time());
		imgseq_layout->addWidget(imgseq_start_time_, imgseq_row, 1);

		imgseq_row++;

		imgseq_layout->addWidget(new QLabel(tr("End Index:")), imgseq_row, 0);

		imgseq_end_time_ = new IntegerSlider();
		imgseq_end_time_->set_minimum(0);
		imgseq_end_time_->set_value(vp.start_time() + vp.duration() - 1);
		imgseq_layout->addWidget(imgseq_end_time_, imgseq_row, 1);

		imgseq_row++;

		imgseq_layout->addWidget(new QLabel(tr("Frame Rate:")), imgseq_row, 0);

		imgseq_frame_rate_ = new FrameRateComboBox();
		imgseq_frame_rate_->set_frame_rate(vp.frame_rate());
		imgseq_layout->addWidget(imgseq_frame_rate_, imgseq_row, 1);

		video_layout->addWidget(imgseq_group, row, 0, 1, 2);
	}
}

void VideoStreamProperties::accept(MultiUndoCommand *parent)
{
	QString set_colorspace;

	if (video_color_space_->currentIndex() > 0) {
		set_colorspace = video_color_space_->currentText();
	}

	VideoParams vp = footage_->get_video_params(video_index_);

	if ((video_premultiply_alpha_ &&
		 video_premultiply_alpha_->isChecked() != vp.premultiplied_alpha()) ||
		set_colorspace != vp.colorspace() ||
		static_cast<VideoParams::Interlacing>(
			video_interlace_combo_->currentIndex()) != vp.interlacing() ||
		pixel_aspect_combo_->get_pixel_aspect_ratio() != vp.pixel_aspect_ratio() ||
		color_range_combo_->currentData().toInt() != vp.color_range()) {
		parent->add_child(new VideoStreamChangeCommand(
			footage_, video_index_,
			video_premultiply_alpha_ ? video_premultiply_alpha_->isChecked() :
									   vp.premultiplied_alpha(),
			set_colorspace,
			static_cast<VideoParams::Interlacing>(
				video_interlace_combo_->currentIndex()),
			pixel_aspect_combo_->get_pixel_aspect_ratio(),
			static_cast<VideoParams::ColorRange>(
				color_range_combo_->currentData().toInt())));
	}

	if (vp.video_type() == VideoParams::k_video_type_image_sequence) {
		int64_t new_dur =
			imgseq_end_time_->get_value() - imgseq_start_time_->get_value() + 1;

		if (vp.start_time() != imgseq_start_time_->get_value() ||
			vp.duration() != new_dur ||
			vp.frame_rate() != imgseq_frame_rate_->get_frame_rate()) {
			parent->add_child(new ImageSequenceChangeCommand(
				footage_, video_index_, imgseq_start_time_->get_value(), new_dur,
				imgseq_frame_rate_->get_frame_rate()));
		}
	}
}

bool VideoStreamProperties::sanity_check()
{
	if (footage_->get_video_params(video_index_).video_type() ==
		VideoParams::k_video_type_image_sequence) {
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

VideoStreamProperties::VideoStreamChangeCommand::VideoStreamChangeCommand(
	Footage *footage, int video_index, bool premultiplied, QString colorspace,
	VideoParams::Interlacing interlacing, const Rational &pixel_ar,
	VideoParams::ColorRange range)
	: footage_(footage)
	, video_index_(video_index)
	, new_premultiplied_(premultiplied)
	, new_colorspace_(colorspace)
	, new_interlacing_(interlacing)
	, new_pixel_ar_(pixel_ar)
	, new_range_(range)
{
}

Project *
VideoStreamProperties::VideoStreamChangeCommand::get_relevant_project() const
{
	return footage_->project();
}

void VideoStreamProperties::VideoStreamChangeCommand::redo()
{
	VideoParams vp = footage_->get_video_params(video_index_);

	old_premultiplied_ = vp.premultiplied_alpha();
	old_colorspace_ = vp.colorspace();
	old_interlacing_ = vp.interlacing();
	old_pixel_ar_ = vp.pixel_aspect_ratio();
	old_range_ = vp.color_range();

	vp.set_premultiplied_alpha(new_premultiplied_);
	vp.set_colorspace(new_colorspace_);
	vp.set_interlacing(new_interlacing_);
	vp.set_pixel_aspect_ratio(new_pixel_ar_);
	vp.set_color_range(new_range_);

	footage_->set_video_params(vp, video_index_);
}

void VideoStreamProperties::VideoStreamChangeCommand::undo()
{
	VideoParams vp = footage_->get_video_params(video_index_);

	vp.set_premultiplied_alpha(old_premultiplied_);
	vp.set_colorspace(old_colorspace_);
	vp.set_interlacing(old_interlacing_);
	vp.set_pixel_aspect_ratio(old_pixel_ar_);
	vp.set_color_range(old_range_);

	footage_->set_video_params(vp, video_index_);
}

VideoStreamProperties::ImageSequenceChangeCommand::ImageSequenceChangeCommand(
	Footage *footage, int video_index, int64_t start_index, int64_t duration,
	const Rational &frame_rate)
	: footage_(footage)
	, video_index_(video_index)
	, new_start_index_(start_index)
	, new_duration_(duration)
	, new_frame_rate_(frame_rate)
{
}

Project *
VideoStreamProperties::ImageSequenceChangeCommand::get_relevant_project() const
{
	return footage_->project();
}

void VideoStreamProperties::ImageSequenceChangeCommand::redo()
{
	VideoParams vp = footage_->get_video_params(video_index_);

	old_start_index_ = vp.start_time();
	vp.set_start_time(new_start_index_);

	old_duration_ = vp.duration();
	vp.set_duration(new_duration_);

	old_frame_rate_ = vp.frame_rate();
	vp.set_frame_rate(new_frame_rate_);
	vp.set_time_base(new_frame_rate_.flipped());

	footage_->set_video_params(vp, video_index_);
}

void VideoStreamProperties::ImageSequenceChangeCommand::undo()
{
	VideoParams vp = footage_->get_video_params(video_index_);

	vp.set_start_time(old_start_index_);
	vp.set_duration(old_duration_);
	vp.set_frame_rate(old_frame_rate_);
	vp.set_time_base(old_frame_rate_.flipped());

	footage_->set_video_params(vp, video_index_);
}

}
