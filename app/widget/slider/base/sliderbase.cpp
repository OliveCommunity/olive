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

#include "sliderbase.h"

#include <QDebug>
#include <QEvent>
#include <QMessageBox>

#include "common/qtutils.h"
#include "core.h"
#include "window/mainwindow/mainwindow.h"

namespace olive
{

#define super QStackedWidget

SliderBase::SliderBase(QWidget *parent)
	: super(parent)
	, tristate_(false)
	, format_plural_(false)
{
	// Standard (non-numeric) sliders are not draggable, so we indicate as such
	setCursor(Qt::PointingHandCursor);

	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

	label_ = new SliderLabel(this);
	addWidget(label_);

	editor_ = new FocusableLineEdit(this);
	addWidget(editor_);

	connect(label_, &SliderLabel::focused, this, &SliderBase::show_editor);
	connect(label_, &SliderLabel::request_reset, this, &SliderBase::reset_value);
	connect(editor_, &FocusableLineEdit::confirmed, this,
			&SliderBase::line_edit_confirmed);
	connect(editor_, &FocusableLineEdit::cancelled, this,
			&SliderBase::line_edit_cancelled);
}

void SliderBase::set_alignment(Qt::Alignment alignment)
{
	label_->setAlignment(alignment);
	editor_->setAlignment(alignment);
}

bool SliderBase::is_tristate() const
{
	return tristate_;
}

void SliderBase::set_tristate()
{
	tristate_ = true;
	update_label();
}

const QVariant &SliderBase::get_value_internal() const
{
	return value_;
}

void SliderBase::set_value_internal(const QVariant &v)
{
	if (!can_set_value()) {
		return;
	}

	value_ = adjust_value(v);

	// Disable tristate
	tristate_ = false;

	update_label();
}

void SliderBase::set_default_value(const QVariant &v)
{
	default_value_ = v;
}

void SliderBase::changeEvent(QEvent *e)
{
	if (e->type() == QEvent::LanguageChange) {
		update_label();
	}
	super::changeEvent(e);
}

bool SliderBase::get_label_substitution(const QVariant &v, QString *out) const
{
	for (auto it = label_substitutions_.constBegin();
		 it != label_substitutions_.constEnd(); it++) {
		if (it->first == v) {
			*out = it->second;
			return true;
		}
	}

	return false;
}

void SliderBase::update_label()
{
	QString s;

	if (tristate_) {
		s = tr("---");
	} else if (get_label_substitution(get_value_internal(), &s)) {
		// String will already be set, just pass through
	} else {
		s = get_formatted_value_to_string();
	}

	label_->setText(s);
}

QVariant SliderBase::adjust_value(const QVariant &value) const
{
	return value;
}

bool SliderBase::can_set_value() const
{
	return true;
}

void SliderBase::value_signal_event(const QVariant &value)
{
	Q_UNUSED(value)
}

void SliderBase::show_editor()
{
	// This was a simple click
	// Load label's text into editor
	editor_->setText(value_to_string(value_));

	// Show editor
	setCurrentWidget(editor_);

	// Select all text in the editor
	editor_->setFocus();
	editor_->selectAll();
}

void SliderBase::line_edit_confirmed()
{
	bool is_valid = true;
	QVariant test_val = string_to_value(editor_->text(), &is_valid);

	// Ensure editor doesn't signal that the focus is lost
	editor_->blockSignals(true);
	label_->blockSignals(true);

	if (is_valid) {
		set_value_internal(test_val);

		setCurrentWidget(label_);

		value_signal_event(value_);
	} else {
		QMessageBox::critical(
			this, tr("Invalid Value"),
			tr("The entered value is not valid for this field."),
			QMessageBox::Ok);

		// Refocus editor
		editor_->setFocus();
	}

	editor_->blockSignals(false);
	label_->blockSignals(false);
}

void SliderBase::line_edit_cancelled()
{
	// Ensure editor doesn't signal that the focus is lost
	editor_->blockSignals(true);
	label_->blockSignals(true);

	// Set widget back to label
	setCurrentWidget(label_);

	editor_->blockSignals(false);
	label_->blockSignals(false);
}

void SliderBase::reset_value()
{
	if (default_value_.isValid()) {
		set_value_internal(default_value_);
		value_signal_event(value_);
	}
}

void SliderBase::set_format(const QString &s, const bool plural)
{
	custom_format_ = s;
	format_plural_ = plural;
	update_label();
}

void SliderBase::clear_format()
{
	custom_format_.clear();
	update_label();
}

bool SliderBase::is_format_plural() const
{
	return format_plural_;
}

QString SliderBase::get_format() const
{
	if (custom_format_.isEmpty()) {
		return QStringLiteral("%1");
	} else {
		return custom_format_;
	}
}

QString SliderBase::get_formatted_value_to_string() const
{
	return get_formatted_value_to_string(get_value_internal());
}

QString SliderBase::get_formatted_value_to_string(const QVariant &v) const
{
	if (format_plural_) {
		return tr(get_format().toUtf8().constData(), nullptr, v.toInt());
	} else {
		return get_format().arg(value_to_string(v));
	}
}

}
