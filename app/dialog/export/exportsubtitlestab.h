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

#ifndef OAK_EXPORTSUBTITLESTAB_H
#define OAK_EXPORTSUBTITLESTAB_H

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>

#include "codec/exportformat.h"
#include "common/qtutils.h"
#include "dialog/export/exportformatcombobox.h"

namespace olive
{

class ExportSubtitlesTab : public QWidget {
	Q_OBJECT
public:
	ExportSubtitlesTab(QWidget *parent = nullptr);

	bool get_sidecar_enabled() const
	{
		return sidecar_checkbox_->isChecked();
	}
	void set_sidecar_enabled(bool e)
	{
		sidecar_checkbox_->setChecked(e);
	}

	ExportFormat::Format get_sidecar_format() const
	{
		return sidecar_format_combobox_->get_format();
	}
	void set_sidecar_format(ExportFormat::Format f)
	{
		sidecar_format_combobox_->set_format(f);
	}

	int set_format(ExportFormat::Format format);

	ExportCodec::Codec get_subtitle_codec()
	{
		return static_cast<ExportCodec::Codec>(
			codec_combobox_->currentData().toInt());
	}

	void set_subtitle_codec(ExportCodec::Codec c)
	{
		QtUtils::set_combo_box_data(codec_combobox_, c);
	}

private:
	QCheckBox *sidecar_checkbox_;

	QLabel *sidecar_format_label_;
	ExportFormatComboBox *sidecar_format_combobox_;

	QComboBox *codec_combobox_;
};

}

#endif // OAK_EXPORTSUBTITLESTAB_H
