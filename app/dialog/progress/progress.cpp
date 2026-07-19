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

#include "progress.h"

#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "window/mainwindow/mainwindow.h"

namespace olive
{

#define super QDialog

ProgressDialog::ProgressDialog(const QString &message, const QString &title,
							   QWidget *parent)
	: super(parent)
	, show_progress_(true)
	, first_show_(true)
{
	if (!title.isEmpty()) {
		setWindowTitle(title);
	}

	QVBoxLayout *layout = new QVBoxLayout(this);

	layout->addWidget(new QLabel(message));

	bar_ = new QProgressBar();
	bar_->setMinimum(0);
	bar_->setValue(0);
	bar_->setMaximum(100);
	layout->addWidget(bar_);

	elapsed_timer_lbl_ = new ElapsedCounterWidget();
	layout->addWidget(elapsed_timer_lbl_);

	QHBoxLayout *cancel_layout = new QHBoxLayout();
	layout->addLayout(cancel_layout);
	cancel_layout->setContentsMargins(0, 0, 0, 0);
	cancel_layout->setSpacing(0);
	cancel_layout->addStretch();

	QPushButton *cancel_btn = new QPushButton(tr("Cancel"));

	// Signal that derivatives can connect to
	connect(cancel_btn, &QPushButton::clicked, this, &ProgressDialog::cancelled,
			Qt::DirectConnection);

	// Stop updating the elapsed/remaining timers
	connect(cancel_btn, &QPushButton::clicked, elapsed_timer_lbl_,
			&ElapsedCounterWidget::stop);

	// Disable the button so that users know they don't need to keep clicking it
	connect(cancel_btn, &QPushButton::clicked, this,
			&ProgressDialog::disable_sender_widget);

	// Prevent the progress bar from continuing to move
	connect(cancel_btn, &QPushButton::clicked, this,
			&ProgressDialog::disable_progress_widgets);

	cancel_layout->addWidget(cancel_btn);

	cancel_layout->addStretch();
}

void ProgressDialog::showEvent(QShowEvent *e)
{
	super::showEvent(e);

	if (first_show_) {
		elapsed_timer_lbl_->start();

		Core::instance()->main_window()->set_application_progress_status(
			MainWindow::k_progress_show);

		first_show_ = false;
	}
}

void ProgressDialog::closeEvent(QCloseEvent *e)
{
	super::closeEvent(e);

	Core::instance()->main_window()->set_application_progress_status(
		MainWindow::k_progress_none);

	elapsed_timer_lbl_->stop();

	first_show_ = true;
}

void ProgressDialog::set_progress(double value)
{
	if (!show_progress_) {
		return;
	}

	int percent = qRound(100.0 * value);

	bar_->setValue(percent);
	elapsed_timer_lbl_->set_progress(value);

	Core::instance()->main_window()->set_application_progress_value(percent);
}

void ProgressDialog::show_error_message(const QString &title,
									  const QString &message)
{
	Core::instance()->main_window()->set_application_progress_status(
		MainWindow::k_progress_error);

	QMessageBox b(this);
	b.setIcon(QMessageBox::Critical);
	b.setWindowModality(Qt::WindowModal);
	b.setWindowTitle(title);
	b.setText(message);
	b.addButton(QMessageBox::Ok);
	b.exec();
}

void ProgressDialog::disable_sender_widget()
{
	static_cast<QWidget *>(sender())->setEnabled(false);
}

void ProgressDialog::disable_progress_widgets()
{
	show_progress_ = false;
}

}
