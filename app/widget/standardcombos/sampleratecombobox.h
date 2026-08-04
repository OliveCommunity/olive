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

#ifndef OAK_SAMPLERATECOMBOBOX_H
#define OAK_SAMPLERATECOMBOBOX_H

#include <olive/core/core.h>
#include <QComboBox>

#include "ui/humanstrings.h"

namespace olive
{

using namespace core;

class SampleRateComboBox : public QComboBox {
	Q_OBJECT
public:
	SampleRateComboBox(QWidget *parent = nullptr)
		: QComboBox(parent)
	{
		for (int sr : AudioParams::k_supported_sample_rates) {
			this->addItem(HumanStrings::sample_rate_to_string(sr), sr);
		}
	}

	int get_sample_rate() const
	{
		return this->currentData().toInt();
	}

	void set_sample_rate(int rate)
	{
		for (int i = 0; i < this->count(); i++) {
			if (this->itemData(i).toInt() == rate) {
				this->setCurrentIndex(i);
				break;
			}
		}
	}
};

}

#endif // OAK_SAMPLERATECOMBOBOX_H
