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

#ifndef OAK_H264SECTION_H
#define OAK_H264SECTION_H

#include <QSlider>
#include <QStackedWidget>
#include <QComboBox>

#include "codecsection.h"
#include "widget/slider/floatslider.h"

namespace olive
{

class H264CRFSection : public QWidget {
	Q_OBJECT
public:
	H264CRFSection(int default_crf, QWidget *parent = nullptr);

	int get_value() const;
	void set_value(int c);

	static constexpr int k_default_h264_crf = 18;
	static constexpr int k_default_h265_crf = 23;

private:
	static constexpr int k_minimum_crf = 0;
	static constexpr int k_maximum_crf = 51;

	QSlider *crf_slider_;
};

class H264BitRateSection : public QWidget {
	Q_OBJECT
public:
	H264BitRateSection(QWidget *parent = nullptr);

	/**
   * @brief Get user-selected target bit rate (returns in BITS)
   */
	int64_t get_target_bit_rate() const;
	void set_target_bit_rate(int64_t b);

	/**
   * @brief Get user-selected maximum bit rate (returns in BITS)
   */
	int64_t get_maximum_bit_rate() const;
	void set_maximum_bit_rate(int64_t b);

private:
	FloatSlider *target_rate_;

	FloatSlider *max_rate_;
};

class H264FileSizeSection : public QWidget {
	Q_OBJECT
public:
	H264FileSizeSection(QWidget *parent = nullptr);

	/**
   * @brief Returns file size in BITS
   */
	int64_t get_file_size() const;
	void set_file_size(int64_t f);

private:
	FloatSlider *file_size_;
};

class H264Section : public CodecSection {
	Q_OBJECT
public:
	enum CompressionMethod {
		k_constant_rate_factor,
		k_target_bit_rate,
		k_target_file_size
	};

	H264Section(QWidget *parent = nullptr);
	H264Section(int default_crf, QWidget *parent);

	virtual void add_opts(OakEngineEncodingParams *params) override;

	virtual void set_opts(const OakEngineEncodingParams *p) override;

private:
	QStackedWidget *compression_method_stack_;

	H264CRFSection *crf_section_;

	H264BitRateSection *bitrate_section_;

	H264FileSizeSection *filesize_section_;

	QComboBox *preset_combobox_;
};

class H265Section : public H264Section {
	Q_OBJECT
public:
	H265Section(QWidget *parent = nullptr);
};

}

#endif // OAK_H264SECTION_H
