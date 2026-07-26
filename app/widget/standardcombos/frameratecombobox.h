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

#ifndef OAK_FRAMERATECOMBOBOX_H
#define OAK_FRAMERATECOMBOBOX_H

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>

#include "oakengine/videoparams.h"

namespace olive
{

class FrameRateComboBox : public QWidget {
	Q_OBJECT
public:
	FrameRateComboBox(QWidget *parent = nullptr)
		: QWidget(parent)
	{
		inner_ = new QComboBox();

		QHBoxLayout *layout = new QHBoxLayout(this);
		layout->setSpacing(0);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(inner_);

		repopulate_list();

		old_index_ = 0;

		connect(inner_,
				static_cast<void (QComboBox::*)(int)>(
					&QComboBox::currentIndexChanged),
				this, &FrameRateComboBox::index_changed);
	}

	Rational get_frame_rate() const
	{
		if (inner_->currentIndex() == inner_->count() - 1) {
			return custom_rate_;
		} else {
			return inner_->currentData().value<Rational>();
		}
	}

	void set_frame_rate(const Rational &r)
	{
		int standard_rates = inner_->count() - 1;
		for (int i = 0; i < standard_rates; i++) {
			if (inner_->itemData(i).value<Rational>() == r) {
				// Set standard frame rate
				old_index_ = i;
				set_inner_index_without_signal(i);
				return;
			}
		}

		// If we're here, set a custom rate
		custom_rate_ = r;
		old_index_ = inner_->count() - 1;
		set_inner_index_without_signal(old_index_);
		repopulate_list();
	}

signals:
	void frame_rate_changed(const Rational &frame_rate);

protected:
	virtual void changeEvent(QEvent *event) override
	{
		QWidget::changeEvent(event);

		if (event->type() == QEvent::LanguageChange) {
			repopulate_list();
		}
	}

private slots:
	void index_changed(int index)
	{
		if (index == inner_->count() - 1) {
			// Custom
			QString s;
			bool ok;

			if (!custom_rate_.isNull()) {
				s = QString::number(custom_rate_.to_double());
			}

			while (true) {
				s = QInputDialog::getText(this, tr("Custom Frame Rate"),
										  tr("Enter custom frame rate:"),
										  QLineEdit::Normal, s, &ok);

				if (ok) {
					Rational r;

					// Try converting to double, assuming most users will input frame rates this way
					double d = s.toDouble(&ok);

					if (ok) {
						// Try converting from double
						r = Rational::from_double(d, &ok);
					} else {
						// Try converting to Rational in case someone formatted that way
						r = Rational::from_string(s.toStdString(), &ok);
					}

					if (ok) {
						custom_rate_ = r;
						emit frame_rate_changed(r);
						old_index_ = index;
						repopulate_list();
						break;

					} else {
						// Show message and continue loop
						QMessageBox::critical(
							this, tr("Invalid Input"),
							tr("Failed to convert \"%1\" to a frame rate.")
								.arg(s));
					}

				} else {
					// User cancelled, revert to original value
					set_inner_index_without_signal(old_index_);
					break;
				}
			}
		} else {
			old_index_ = index;
			emit frame_rate_changed(get_frame_rate());
		}
	}

private:
	void repopulate_list()
	{
		int temp_index = inner_->currentIndex();

		inner_->blockSignals(true);

		inner_->clear();

		{
			const int n = oakengine_video_params_supported_frame_rate_count();
			for (int i = 0; i < n; i++) {
				int num, den;
				oakengine_video_params_supported_frame_rate_at(i, &num, &den);
				char buf[64];
				oakengine_video_params_frame_rate_to_string(num, den, buf,
															sizeof(buf));
				Rational fr(num, den);
				inner_->addItem(QString::fromUtf8(buf),
								QVariant::fromValue(fr));
			}
		}

		if (custom_rate_.isNull()) {
			inner_->addItem(tr("Custom..."));
		} else {
			{
				char buf[64];
				oakengine_video_params_frame_rate_to_string(
					custom_rate_.numerator(), custom_rate_.denominator(), buf,
					sizeof(buf));
				inner_->addItem(
					tr("Custom (%1)")
						.arg(QString::fromUtf8(buf)));
		}
		}

		// On the first populate there is no current index (-1); select the
		// first standard rate so GetFrameRate() matches what's displayed
		inner_->setCurrentIndex(qMax(temp_index, 0));

		inner_->blockSignals(false);
	}

	void set_inner_index_without_signal(int index)
	{
		inner_->blockSignals(true);
		inner_->setCurrentIndex(index);
		inner_->blockSignals(false);
	}

	QComboBox *inner_;

	Rational custom_rate_;

	int old_index_;
};

}

#endif // OAK_FRAMERATECOMBOBOX_H
