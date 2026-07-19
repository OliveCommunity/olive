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

#ifndef OAK_RATIONALSLIDER_H
#define OAK_RATIONALSLIDER_H

#include <olive/core/core.h>
#include <QMouseEvent>

#include "base/decimalsliderbase.h"
#include "node/sliderdisplaytype.h"

namespace olive
{

using namespace core;

/**
 * @brief A olive::Rational based slider
 *
 * A slider that can display rationals as either timecode (drop or non-drop), a timestamp (frames),
 * or a float (seconds).
 */
class RationalSlider : public DecimalSliderBase {
	Q_OBJECT
public:
	/**
   * @brief enum containing the possibly display types
   *
   * The canonical definition lives in the engine layer
   * (node/sliderdisplaytype.h); this alias keeps existing call sites
   * source-compatible. Use slider::k_time etc. for the enumerators.
   */
	using DisplayType = slider::RationalDisplayType;

	RationalSlider(QWidget *parent = nullptr);

	/**
   * @brief Returns the sliders value as a Rational
   */
	Rational get_value();

	/**
   * @brief Sets the sliders default value
   */
	void SetDefaultValue(const Rational &r);

	/**
   * @brief Sets the sliders minimum value
   */
	void set_minimum(const Rational &d);

	/**
   * @brief Sets the sliders maximum value
   */
	void set_maximum(const Rational &d);

	/**
   * @brief Sets the display type of the slider
   */
	void set_display_type(const DisplayType &type);

	/**
   * @brief Set whether the user can change the display type or not
   */
	void set_lock_display_type(bool e);

	/**
   * @brief Get whether the user can change the display type or not
   */
	bool get_lock_display_type();

	/**
   * @brief Hide display type in menu
   */
	void disable_display_type(DisplayType type);

public slots:
	/**
   * @brief Sets the sliders timebase which is also the minimum increment of the slider
   */
	void set_timebase(const Rational &timebase);

	/**
   * @brief Sets the sliders value
   */
	void set_value(const Rational &d);

protected:
	virtual QString value_to_string(const QVariant &v) const override;

	virtual QVariant string_to_value(const QString &s, bool *ok) const override;

	virtual QVariant
	adjust_drag_distance_internal(const QVariant &start,
							   const double &drag) const override;

	virtual void value_signal_event(const QVariant &v) override;

	virtual bool value_greater_than(const QVariant &lhs,
								  const QVariant &rhs) const override;

	virtual bool value_less_than(const QVariant &lhs,
							   const QVariant &rhs) const override;

signals:
	void value_changed(Rational);

private slots:
	void show_display_type_menu();

	void set_display_type_from_menu();

private:
	DisplayType display_type_;

	Rational timebase_;

	bool lock_display_type_;

	QVector<DisplayType> disabled_;
};

}

#endif // OAK_RATIONALSLIDER_H
