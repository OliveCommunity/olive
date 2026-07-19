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

#ifndef OAK_SLIDERBASE_H
#define OAK_SLIDERBASE_H

#include <QStackedWidget>

#include "sliderlabel.h"
#include "sliderladder.h"
#include "widget/focusablelineedit/focusablelineedit.h"

namespace olive
{

class SliderBase : public QStackedWidget {
	Q_OBJECT
public:
	SliderBase(QWidget *parent = nullptr);

	void set_alignment(Qt::Alignment alignment);

	bool is_tristate() const;
	void set_tristate();

	void set_format(const QString &s, const bool plural = false);
	void clear_format();

	bool is_format_plural() const;

	void set_default_value(const QVariant &v);

	QString get_formatted_value_to_string(const QVariant &v) const;

	void insert_label_substitution(const QVariant &value, const QString &label)
	{
		label_substitutions_.append({ value, label });
		update_label();
	}

	void set_color(const QColor &c)
	{
		label_->set_color(c);
	}

public slots:
	void show_editor();

protected slots:
	void update_label();

protected:
	const QVariant &get_value_internal() const;

	void set_value_internal(const QVariant &v);

	QString get_format() const;

	QString get_formatted_value_to_string() const;

	SliderLabel *label()
	{
		return label_;
	}

	virtual QString value_to_string(const QVariant &v) const = 0;

	virtual QVariant string_to_value(const QString &s, bool *ok) const = 0;

	virtual QVariant adjust_value(const QVariant &value) const;

	virtual bool can_set_value() const;

	virtual void value_signal_event(const QVariant &value) = 0;

	virtual void changeEvent(QEvent *e) override;

private:
	bool get_label_substitution(const QVariant &v, QString *out) const;

	SliderLabel *label_;

	FocusableLineEdit *editor_;

	QVariant value_;
	QVariant default_value_;

	bool tristate_;

	QString custom_format_;

	bool format_plural_;

	QVector<QPair<QVariant, QString>> label_substitutions_;

private slots:
	void line_edit_confirmed();

	void line_edit_cancelled();

	void reset_value();
};

}

#endif // OAK_SLIDERBASE_H
