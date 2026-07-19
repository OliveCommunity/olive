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

#ifndef OAK_FILEFIELD_H
#define OAK_FILEFIELD_H

#include <QLineEdit>
#include <QPushButton>

namespace olive
{

class FileField : public QWidget {
	Q_OBJECT
public:
	FileField(QWidget *parent = nullptr);

	QString get_filename() const
	{
		return line_edit_->text();
	}

	virtual void set_filename(const QString &s)
	{
		line_edit_->setText(s);
	}

	void set_placeholder(const QString &s)
	{
		line_edit_->setPlaceholderText(s);
	}

	void set_directory_mode(bool e)
	{
		directory_mode_ = e;
	}

	void set_name_filter(const QString &filter)
	{
		name_filter_ = filter;
	}

	/**
	 * @brief Sets extra sidebar shortcuts (e.g. a library directory) for the
	 * browse dialog
	 *
	 * Note: setting sidebar URLs requires Qt's non-native file dialog.
	 */
	void set_sidebar_urls(const QList<QUrl> &urls)
	{
		sidebar_urls_ = urls;
	}

signals:
	void filename_changed(const QString &filename);

private:
	QLineEdit *line_edit_;

	QPushButton *browse_btn_;

	bool directory_mode_;

	QString name_filter_;

	QList<QUrl> sidebar_urls_;

private slots:
	void browse_btn_clicked();

	void line_edit_changed(const QString &text);
};

}

#endif // OAK_FILEFIELD_H
