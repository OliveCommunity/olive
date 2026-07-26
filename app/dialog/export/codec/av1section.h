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

#ifndef OAK_AV1SECTION_H
#define OAK_AV1SECTION_H

#include <QSlider>
#include <QStackedWidget>
#include <QComboBox>

#include "codecsection.h"
#include "widget/slider/floatslider.h"

namespace olive
{

class AV1CRFSection : public QWidget {
	Q_OBJECT
public:
	AV1CRFSection(int default_crf, QWidget *parent = nullptr);

	int get_value() const;

	static const int k_default_a_v1_crf = 30;

private:
	static const int k_minimum_crf = 0;
	static const int k_maximum_crf = 63;

	QSlider *crf_slider_;
};

class AV1Section : public CodecSection {
	Q_OBJECT
public:
	enum CompressionMethod {
		k_constant_rate_factor,
	};

	AV1Section(QWidget *parent = nullptr);
	AV1Section(int default_crf, QWidget *parent);

	virtual void add_opts(OakEngineEncodingParams *params) override;

private:
	QStackedWidget *compression_method_stack_;

	AV1CRFSection *crf_section_;

	QComboBox *preset_combobox_;
};

}

#endif // OAK_AV1SECTION_H
