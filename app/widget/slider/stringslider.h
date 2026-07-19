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

#ifndef OAK_STRINGSLIDER_H
#define OAK_STRINGSLIDER_H

#include "base/sliderbase.h"

namespace olive
{

class StringSlider : public SliderBase {
	Q_OBJECT
public:
	StringSlider(QWidget *parent = nullptr);

	void SetDragMultiplier(const double &d) = delete;

	QString get_value() const;

	void set_value(const QString &v);

	void SetDefaultValue(const QString &v);

signals:
	void value_changed(const QString &str);

protected:
	virtual QString value_to_string(const QVariant &value) const override;

	virtual QVariant string_to_value(const QString &s, bool *ok) const override;

	virtual void value_signal_event(const QVariant &value) override;
};

}

#endif // OAK_STRINGSLIDER_H
