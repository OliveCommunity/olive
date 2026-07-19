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

#ifndef OAK_QTVERSIONABSTRACTION_H
#define OAK_QTVERSIONABSTRACTION_H

#include <olive/core/core.h>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QMessageBox>

namespace olive
{

class QtUtils {
public:
	/**
   * @brief Retrieves the width of a string according to certain QFontMetrics
   *
   * QFontMetrics::width() has been deprecatd in favor of QFontMetrics::horizontalAdvance(), but the
   * latter was only introduced in 5.11+. This function wraps the latter for 5.11+ and the former for
   * earlier.
   */
	static int q_font_metrics_width(QFontMetrics fm, const QString &s);

	static QFrame *create_horizontal_line();

	static QFrame *create_vertical_line();

	static int msg_box(QWidget *parent, QMessageBox::Icon icon,
					  const QString &title, const QString &message,
					  QMessageBox::StandardButtons buttons = QMessageBox::Ok);

	static QDateTime get_creation_date(const QFileInfo &info);

	static QString get_formatted_date_time(const QDateTime &dt);

	static QStringList word_wrap_string(const QString &s, const QFontMetrics &fm,
									  int bounding_width);

	static Qt::KeyboardModifiers
	flip_control_and_shift_modifiers(Qt::KeyboardModifiers e);

	static void set_combo_box_data(QComboBox *cb, int data);
	static void set_combo_box_data(QComboBox *cb, const QString &data);

	template <typename T> static T *get_parent_of_type(const QObject *child)
	{
		QObject *t = child->parent();

		while (t) {
			if (T *p = dynamic_cast<T *>(t)) {
				return p;
			}
			t = t->parent();
		}

		return nullptr;
	}

	static QColor to_q_color(const core::Color &c);

	/**
   * @brief Convert a pointer to a value that can be sent between NodeParams
   */
	static QVariant ptr_to_value(void *ptr)
	{
		return reinterpret_cast<quintptr>(ptr);
	}

	/**
   * @brief Convert a NodeParam value to a pointer of any kind
   */
	template <class T> static T *value_to_ptr(const QVariant &ptr)
	{
		return reinterpret_cast<T *>(ptr.value<quintptr>());
	}
};

namespace core
{

uint qHash(const core::Rational &r, uint seed = 0);
uint qHash(const core::TimeRange &r, uint seed = 0);

}

}

Q_DECLARE_METATYPE(olive::core::Rational)
Q_DECLARE_METATYPE(olive::core::Color)
Q_DECLARE_METATYPE(olive::core::TimeRange)
Q_DECLARE_METATYPE(olive::core::Bezier)
Q_DECLARE_METATYPE(olive::core::AudioParams)
Q_DECLARE_METATYPE(olive::core::SampleBuffer)

#endif // OAK_QTVERSIONABSTRACTION_H
