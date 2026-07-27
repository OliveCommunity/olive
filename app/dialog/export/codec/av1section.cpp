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

#include "av1section.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>

#include "oakutil/qtutils.h"
#include "widget/slider/integerslider.h"

namespace olive
{

AV1Section::AV1Section(QWidget *parent)
	: AV1Section(AV1CRFSection::k_default_a_v1_crf, parent)
{
}

AV1Section::AV1Section(int default_crf, QWidget *parent)
	: CodecSection(parent)
{
	QGridLayout *layout = new QGridLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	int row = 0;
	layout->addWidget(new QLabel(tr("Preset:")), row, 0);

	preset_combobox_ = new QComboBox();
	preset_combobox_->setToolTip(tr(
		"This parameter governs the efficiency/encode-time trade-off.\n"
		"Lower presets will result in an output with better quality for a given file size, but will take longer to encode.\n"
		"Higher presets can result in a very fast encode, but will make some compromises on visual quality for a given crf value."));

	for (int i = 0; i <= 13; i++)
		preset_combobox_->addItem(QString::number(i));

	preset_combobox_->setCurrentIndex(8);

	layout->addWidget(preset_combobox_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Compression Method:")), row, 0);

	QComboBox *compression_box = new QComboBox();
	compression_box->setToolTip(tr(
		"This parameter governs the quality/size trade-off.\n"
		"Higher CRF values will result in a final output that takes less space, but begins to lose detail.\n"
		"Lower CRF values retain more detail at the cost of larger file sizes.\n"
		"The possible range of CRF in SVT-AV1 is 1-63."));

	// These items must correspond to the CompressionMethod enum
	compression_box->addItem(tr("Constant Rate Factor"));

	layout->addWidget(compression_box, row, 1);

	row++;

	compression_method_stack_ = new QStackedWidget();
	layout->addWidget(compression_method_stack_, row, 0, 1, 2);

	crf_section_ = new AV1CRFSection(default_crf);
	compression_method_stack_->addWidget(crf_section_);

	connect(
		compression_box,
		static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		compression_method_stack_, &QStackedWidget::setCurrentIndex);
}

void AV1Section::add_opts(OakEngineEncodingParams *params)
{
	CompressionMethod method = static_cast<CompressionMethod>(
		compression_method_stack_->currentIndex());

	if (method == k_constant_rate_factor) {
		// Set Quantizer value
		oakengine_encoding_params_set_video_option(
			params, "qp",
			QByteArray::number(crf_section_->get_value()).constData());
	}

	oakengine_encoding_params_set_video_option(
		params, "preset",
		QByteArray::number(preset_combobox_->currentIndex()).constData());
}

AV1CRFSection::AV1CRFSection(int default_crf, QWidget *parent)
	: QWidget(parent)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	crf_slider_ = new QSlider(Qt::Horizontal);
	crf_slider_->setMinimum(k_minimum_crf);
	crf_slider_->setMaximum(k_maximum_crf);
	crf_slider_->setValue(default_crf);
	layout->addWidget(crf_slider_);

	IntegerSlider *crf_input = new IntegerSlider();
	crf_input->setMaximumWidth(QtUtils::q_font_metrics_width(
		crf_input->fontMetrics(), QStringLiteral("HHHH")));
	crf_input->set_minimum(k_minimum_crf);
	crf_input->set_maximum(k_maximum_crf);
	crf_input->set_value(default_crf);
	crf_input->SetDefaultValue(default_crf);
	layout->addWidget(crf_input);

	connect(crf_slider_, &QSlider::valueChanged, crf_input,
			&IntegerSlider::set_value);
	connect(crf_input, &IntegerSlider::value_changed, crf_slider_,
			&QSlider::setValue);
}

int AV1CRFSection::get_value() const
{
	return crf_slider_->value();
}

}
