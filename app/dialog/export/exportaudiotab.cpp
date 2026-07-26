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

#include "exportaudiotab.h"

#include <QGridLayout>
#include <QLabel>

#include <olive/core/core.h>
#include "oakengine/encoding.h"

namespace olive
{

const int ExportAudioTab::k_default_bit_rate = 320;

ExportAudioTab::ExportAudioTab(QWidget *parent)
	: QWidget(parent)
{
	QVBoxLayout *outer_layout = new QVBoxLayout(this);

	QGridLayout *layout = new QGridLayout();
	outer_layout->addLayout(layout);

	int row = 0;

	layout->addWidget(new QLabel(tr("Codec:")), row, 0);

	codec_combobox_ = new QComboBox();
	connect(
		codec_combobox_,
		static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		this, &ExportAudioTab::update_sample_formats);
	connect(
		codec_combobox_,
		static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		this, &ExportAudioTab::update_bit_rate_enabled);
	layout->addWidget(codec_combobox_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Sample Rate:")), row, 0);

	sample_rate_combobox_ = new SampleRateComboBox();
	layout->addWidget(sample_rate_combobox_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Channel Layout:")), row, 0);

	channel_layout_combobox_ = new ChannelLayoutComboBox();
	layout->addWidget(channel_layout_combobox_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Format:")), row, 0);

	sample_format_combobox_ = new SampleFormatComboBox();
	layout->addWidget(sample_format_combobox_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Bit Rate:")), row, 0);

	bit_rate_slider_ = new IntegerSlider();
	bit_rate_slider_->set_minimum(32);
	bit_rate_slider_->set_maximum(320);
	bit_rate_slider_->set_value(k_default_bit_rate);
	bit_rate_slider_->set_format(tr("%1 kbps"));
	layout->addWidget(bit_rate_slider_, row, 1);

	outer_layout->addStretch();
}

int ExportAudioTab::set_format(int format)
{
	const int acodec_count = oakengine_encoding_format_audio_codec_count(format);
	setEnabled(acodec_count > 0);
	codec_combobox_->blockSignals(true);
	codec_combobox_->clear();
	for (int i = 0; i < acodec_count; i++) {
		int codec = oakengine_encoding_format_audio_codec_at(format, i);
		char buf[256];
		oakengine_encoding_codec_name(codec, buf, sizeof(buf));
		codec_combobox_->addItem(QString::fromUtf8(buf), codec);
	}
	codec_combobox_->blockSignals(false);
	fmt_ = format;

	update_sample_formats();
	update_bit_rate_enabled();

	return acodec_count;
}

void ExportAudioTab::update_sample_formats()
{
	// Use oakengine to get sample format values and build the vector
	const int count = oakengine_encoding_sample_format_count(fmt_, get_codec());
	std::vector<olive::core::SampleFormat> fmts;
	fmts.reserve(count);
	for (int i = 0; i < count; i++) {
		int val = oakengine_encoding_sample_format_at(fmt_, get_codec(), i);
		fmts.push_back(olive::core::SampleFormat(static_cast<olive::core::SampleFormat::Format>(val)));
	}
	sample_format_combobox_->set_available_formats(fmts);
}

void ExportAudioTab::update_bit_rate_enabled()
{
	bool uses_bitrate = !oakengine_encoding_codec_is_lossless(get_codec());
	bit_rate_slider_->setEnabled(uses_bitrate);

	if (!uses_bitrate) {
		bit_rate_slider_->set_tristate();
	} else {
		bit_rate_slider_->set_value(k_default_bit_rate);
	}
}

}
