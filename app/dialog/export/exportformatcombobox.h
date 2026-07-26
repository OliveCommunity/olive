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

#ifndef OAK_EXPORTFORMATCOMBOBOX_H
#define OAK_EXPORTFORMATCOMBOBOX_H

#include <QComboBox>
#include <QWidgetAction>

#include "node/output/track/track.h"
#include "widget/menu/menu.h"

namespace olive
{

class ExportFormatComboBox : public QComboBox {
	Q_OBJECT
public:
	enum Mode {
		k_show_all_formats,
		k_show_audio_only,
		k_show_video_only,
		k_show_subtitles_only
	};

	ExportFormatComboBox(Mode mode, QWidget *parent = nullptr);
	ExportFormatComboBox(QWidget *parent = nullptr)
		: ExportFormatComboBox(k_show_all_formats, parent)
	{
	}

	int get_format() const
	{
		return current_;
	}

	void showPopup();

signals:
	void format_changed(int fmt);

public slots:
	void set_format(int fmt);

private slots:
	void handle_index_change(QAction *a);

private:
	void populate_type(Track::Type type);

	QWidgetAction *create_header(const QIcon &icon, const QString &title);

	Menu *custom_menu_;

	int current_ = -1; // was ExportFormat::k_format_count
};

}

#endif // OAK_EXPORTFORMATCOMBOBOX_H
