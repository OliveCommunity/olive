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

#include "exportformatcombobox.h"

#include <QHBoxLayout>
#include <QLabel>

#include "ui/icons/icons.h"

namespace olive
{

ExportFormatComboBox::ExportFormatComboBox(Mode mode, QWidget *parent)
	: QComboBox(parent)
{
	custom_menu_ = new Menu(this);

	// Populate combobox formats
	switch (mode) {
	case k_show_all_formats:
		custom_menu_->addAction(create_header(icon::video, tr("Video")));
		populate_type(Track::k_video);
		custom_menu_->addSeparator();

		custom_menu_->addAction(create_header(icon::audio, tr("Audio")));
		populate_type(Track::k_audio);
		custom_menu_->addSeparator();

		custom_menu_->addAction(create_header(icon::subtitles, tr("Subtitle")));
		populate_type(Track::k_subtitle);
		break;
	case k_show_audio_only:
		populate_type(Track::k_audio);
		break;
	case k_show_video_only:
		populate_type(Track::k_video);
		break;
	case k_show_subtitles_only:
		populate_type(Track::k_subtitle);
		break;
	}

	connect(custom_menu_, &Menu::triggered, this,
			&ExportFormatComboBox::handle_index_change);
}

void ExportFormatComboBox::showPopup()
{
	custom_menu_->setMinimumWidth(this->width());
	custom_menu_->exec(mapToGlobal(QPoint(0, 0)));
}

void ExportFormatComboBox::set_format(ExportFormat::Format fmt)
{
	current_ = fmt;
	clear();
	addItem(ExportFormat::get_name(current_));
}

void ExportFormatComboBox::handle_index_change(QAction *a)
{
	ExportFormat::Format f =
		static_cast<ExportFormat::Format>(a->data().toInt());
	set_format(f);
	emit format_changed(f);
}

void ExportFormatComboBox::populate_type(Track::Type type)
{
	for (int i = 0; i < ExportFormat::k_format_count; i++) {
		ExportFormat::Format f = static_cast<ExportFormat::Format>(i);

		if (type == Track::k_video &&
			!ExportFormat::get_video_codecs(f).isEmpty()) {
			// Do nothing
		} else if (type == Track::k_audio &&
				   ExportFormat::get_video_codecs(f).isEmpty() &&
				   !ExportFormat::get_audio_codecs(f).isEmpty()) {
			// Do nothing
		} else if (type == Track::k_subtitle &&
				   ExportFormat::get_video_codecs(f).isEmpty() &&
				   ExportFormat::get_audio_codecs(f).isEmpty() &&
				   !ExportFormat::get_subtitle_codecs(f).isEmpty()) {
			// Do nothing
		} else {
			continue;
		}

		QString format_name = ExportFormat::get_name(f);

		QAction *a = custom_menu_->addAction(format_name);
		a->setData(i);
		a->setIconVisibleInMenu(false);
	}
}

QWidgetAction *ExportFormatComboBox::create_header(const QIcon &icon,
												  const QString &title)
{
	QWidgetAction *a = new QWidgetAction(this);

	QWidget *w = new QWidget();
	QHBoxLayout *layout = new QHBoxLayout(w);

	QLabel *icon_lbl = new QLabel();

	QLabel *text_lbl = new QLabel(title);
	text_lbl->setAlignment(Qt::AlignCenter);
	QFont f = text_lbl->font();
	f.setWeight(QFont::Bold);
	text_lbl->setFont(f);

	icon_lbl->setPixmap(icon.pixmap(text_lbl->sizeHint()));

	layout->addStretch();
	layout->addWidget(icon_lbl);
	layout->addWidget(text_lbl);
	layout->addStretch();

	a->setDefaultWidget(w);
	a->setEnabled(false);
	return a;
}

}
