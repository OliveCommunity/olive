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

#ifndef OAK_PROGRESSDIALOG_H
#define OAK_PROGRESSDIALOG_H

#include <QDialog>
#include <QProgressBar>

#include "common/debugapp.h"
#include "widget/taskview/elapsedcounterwidget.h"

namespace olive
{

class ProgressDialog : public QDialog {
	Q_OBJECT
public:
	ProgressDialog(const QString &message, const QString &title,
				   QWidget *parent = nullptr);

protected:
	virtual void showEvent(QShowEvent *e) override;

	virtual void closeEvent(QCloseEvent *) override;

public slots:
	void set_progress(double value);

signals:
	void cancelled();

protected:
	void show_error_message(const QString &title, const QString &message);

private:
	QProgressBar *bar_;

	ElapsedCounterWidget *elapsed_timer_lbl_;

	bool show_progress_;

	bool first_show_;

private slots:
	void disable_sender_widget();

	void disable_progress_widgets();
};

}

#endif // OAK_PROGRESSDIALOG_H
