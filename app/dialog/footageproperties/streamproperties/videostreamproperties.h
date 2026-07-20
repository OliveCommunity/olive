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

#ifndef OAK_VIDEOSTREAMPROPERTIES_H
#define OAK_VIDEOSTREAMPROPERTIES_H

#include <QCheckBox>
#include <QComboBox>

#include "node/project/footage/footage.h"
#include "streamproperties.h"
#include "widget/slider/integerslider.h"
#include "widget/standardcombos/standardcombos.h"

namespace olive
{

class VideoStreamProperties : public StreamProperties {
	Q_OBJECT
public:
	VideoStreamProperties(Footage *footage, int video_index);

	virtual void accept(MultiUndoCommand *parent) override;

	virtual bool sanity_check() override;

private:
	Footage *footage_;

	int video_index_;

	/**
   * @brief Setting for associated/premultiplied alpha
   */
	QCheckBox *video_premultiply_alpha_;

	/**
   * @brief Setting for this media's color space
   */
	QComboBox *video_color_space_;

	/**
   * @brief Setting for this streams's color range
   */
	QComboBox *color_range_combo_;

	/**
   * @brief Setting for video interlacing
   */
	InterlacedComboBox *video_interlace_combo_;

	/**
   * @brief Sets the start index for image sequences
   */
	IntegerSlider *imgseq_start_time_;

	/**
   * @brief Sets the end index for image sequences
   */
	IntegerSlider *imgseq_end_time_;

	/**
   * @brief Sets the frame rate for image sequences
   */
	FrameRateComboBox *imgseq_frame_rate_;

	/**
   * @brief Sets the pixel aspect ratio of the stream
   */
	PixelAspectRatioComboBox *pixel_aspect_combo_;
};

}

#endif // OAK_VIDEOSTREAMPROPERTIES_H
