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

#ifndef OAK_EXPORTVIDEOTAB_H
#define OAK_EXPORTVIDEOTAB_H

#include <QCheckBox>
#include <QComboBox>
#include <QWidget>

#include "oakutil/qtutils.h"
#include "dialog/export/codec/av1section.h"
#include "dialog/export/codec/cineformsection.h"
#include "dialog/export/codec/codecstack.h"
#include "dialog/export/codec/h264section.h"
#include "dialog/export/codec/imagesection.h"
#include "oakengine/color.h"
#include "widget/colorwheel/colorspacechooser.h"
#include "widget/manageddisplay/colorprocessorhandle.h"
#include "widget/slider/integerslider.h"
#include "widget/standardcombos/standardcombos.h"

namespace olive
{

class ExportVideoTab : public QWidget {
	Q_OBJECT
public:
	ExportVideoTab(OakEngineColorManager *color_manager, QWidget *parent = nullptr);

	int set_format(int format);

	bool is_image_sequence_set() const;
	void set_image_sequence(bool e) const;

	Rational get_still_image_time() const
	{
		return image_section_->get_time();
	}

	int get_selected_codec() const
	{
		return codec_combobox()->currentData().toInt();
	}

	void set_selected_codec(int c)
	{
		QtUtils::set_combo_box_data(codec_combobox(), c);
	}

	QComboBox *codec_combobox() const
	{
		return codec_combobox_;
	}

	IntegerSlider *width_slider() const
	{
		return width_slider_;
	}

	IntegerSlider *height_slider() const
	{
		return height_slider_;
	}

	QCheckBox *maintain_aspect_checkbox() const
	{
		return maintain_aspect_checkbox_;
	}

	QComboBox *scaling_method_combobox() const
	{
		return scaling_method_combobox_;
	}

	Rational get_selected_frame_rate() const
	{
		return frame_rate_combobox_->get_frame_rate();
	}

	void set_selected_frame_rate(const Rational &fr)
	{
		frame_rate_combobox_->set_frame_rate(fr);
		update_frame_rate(fr);
	}

	QString current_ocio_color_space()
	{
		return color_space_chooser_->input();
	}

	void set_ocio_color_space(const QString &s)
	{
		color_space_chooser_->set_input(s);
	}

	CodecSection *get_codec_section() const
	{
		return static_cast<CodecSection *>(codec_stack_->currentWidget());
	}

	void set_codec_section(CodecSection *section)
	{
		if (section) {
			codec_stack_->setVisible(true);
			codec_stack_->setCurrentWidget(section);
		} else {
			codec_stack_->setVisible(false);
		}
	}

	InterlacedComboBox *interlaced_combobox() const
	{
		return interlaced_combobox_;
	}

	PixelAspectRatioComboBox *pixel_aspect_combobox() const
	{
		return pixel_aspect_combobox_;
	}

	PixelFormatComboBox *pixel_format_field() const
	{
		return pixel_format_field_;
	}

	const int &threads() const
	{
		return threads_;
	}

	void set_threads(int t)
	{
		threads_ = t;
	}

	const QString &pix_fmt() const
	{
		return pix_fmt_;
	}
	void set_pix_fmt(const QString &s)
	{
		pix_fmt_ = s;
	}

	int color_range() const
	{
		return color_range_;
	}
	void set_color_range(int c)
	{
		color_range_ = c;
	}

public slots:
	void video_codec_changed();

	void set_time(const Rational &time);

signals:
	void color_space_changed(const QString &colorspace);

	void image_sequence_check_box_changed(bool e);

	void time_changed(const Rational &time);

private:
	QWidget *setup_resolution_section();
	QWidget *setup_color_section();
	QWidget *setup_codec_section();

	QComboBox *codec_combobox_;
	FrameRateComboBox *frame_rate_combobox_;
	QCheckBox *maintain_aspect_checkbox_;
	QComboBox *scaling_method_combobox_;

	CodecStack *codec_stack_;
	ImageSection *image_section_;
	H264Section *h264_section_;
	H264Section *h265_section_;
	AV1Section *av1_section_;
	CineformSection *cineform_section_;

	ColorSpaceChooser *color_space_chooser_;

	IntegerSlider *width_slider_;
	IntegerSlider *height_slider_;

	OakEngineColorManager *color_manager_;

	InterlacedComboBox *interlaced_combobox_;
	PixelAspectRatioComboBox *pixel_aspect_combobox_;
	PixelFormatComboBox *pixel_format_field_;

	int threads_;

	QString pix_fmt_;
	int color_range_;

	int format_;

private slots:
	void maintain_aspect_ratio_changed(bool val);

	void open_advanced_dialog();

	void update_frame_rate(Rational r);
};

}

#endif // OAK_EXPORTVIDEOTAB_H
