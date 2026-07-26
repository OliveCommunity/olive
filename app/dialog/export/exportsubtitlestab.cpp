#include "exportsubtitlestab.h"

#include <QGridLayout>

#include "oakengine/encoding.h"

namespace olive
{

ExportSubtitlesTab::ExportSubtitlesTab(QWidget *parent)
	: QWidget(parent)
{
	QVBoxLayout *outer_layout = new QVBoxLayout(this);

	QGridLayout *layout = new QGridLayout();
	outer_layout->addLayout(layout);

	int row = 0;

	sidecar_checkbox_ = new QCheckBox(tr("Export to sidecar file"));
	layout->addWidget(sidecar_checkbox_, row, 0, 1, 2);

	row++;

	sidecar_format_label_ = new QLabel(tr("Sidecar Format:"));
	sidecar_format_label_->setVisible(false);
	layout->addWidget(sidecar_format_label_, row, 0);

	sidecar_format_combobox_ =
		new ExportFormatComboBox(ExportFormatComboBox::k_show_subtitles_only);
	sidecar_format_combobox_->setVisible(true);
	layout->addWidget(sidecar_format_combobox_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Codec:")), row, 0);

	codec_combobox_ = new QComboBox();
	layout->addWidget(codec_combobox_, row, 1);

	outer_layout->addStretch();

	connect(sidecar_checkbox_, &QCheckBox::toggled, sidecar_format_label_,
			&QWidget::setVisible);
	connect(sidecar_checkbox_, &QCheckBox::toggled, sidecar_format_combobox_,
			&QWidget::setVisible);
}

int ExportSubtitlesTab::set_format(int format)
{
	const bool has_video = oakengine_encoding_format_video_codec_count(format) > 0;
	const bool has_audio = oakengine_encoding_format_audio_codec_count(format) > 0;
	int scodec_count = oakengine_encoding_format_subtitle_codec_count(format);

	if (scodec_count > 0 && !has_video && !has_audio) {
		// If format supports ONLY scodecs, default this to off and disable it
		sidecar_checkbox_->setChecked(false);
		sidecar_checkbox_->setEnabled(false);
	} else {
		// If format does not support scodecs, default this to checked and disable it
		sidecar_checkbox_->setChecked(scodec_count == 0);
		sidecar_checkbox_->setEnabled(scodec_count > 0);
	}

	// Refresh for sidecar format
	int sidecar_fmt = sidecar_format_combobox_->get_format();
	scodec_count = oakengine_encoding_format_subtitle_codec_count(sidecar_fmt);

	codec_combobox_->clear();
	for (int i = 0; i < scodec_count; i++) {
		int scodec = oakengine_encoding_format_subtitle_codec_at(sidecar_fmt, i);
		char buf[256];
		oakengine_encoding_codec_name(scodec, buf, sizeof(buf));
		codec_combobox_->addItem(QString::fromUtf8(buf), scodec);
	}

	return scodec_count;
}

}
