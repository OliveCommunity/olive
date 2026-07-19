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

#ifndef OAK_SAMPLEFORMATCOMBOBOX_H
#define OAK_SAMPLEFORMATCOMBOBOX_H

#include <olive/core/core.h>
#include <QComboBox>

#include "ui/humanstrings.h"

namespace olive
{

using namespace core;

class SampleFormatComboBox : public QComboBox {
	Q_OBJECT
public:
	SampleFormatComboBox(QWidget *parent = nullptr)
		: QComboBox(parent)
		, attempt_to_restore_format_(true)
	{
	}

	void set_attempt_to_restore_format(bool e)
	{
		attempt_to_restore_format_ = e;
	}

	void set_available_formats(const std::vector<SampleFormat> &formats)
	{
		SampleFormat tmp = SampleFormat::invalid;

		if (attempt_to_restore_format_) {
			tmp = get_sample_format();
		}

		clear();
		foreach (const SampleFormat &of, formats) {
			add_format_item(of);
		}

		if (attempt_to_restore_format_) {
			set_sample_format(tmp);
		}
	}

	void set_packed_formats()
	{
		SampleFormat tmp = SampleFormat::invalid;

		if (attempt_to_restore_format_) {
			tmp = get_sample_format();
		}

		clear();
		for (int i = SampleFormat::packed_start; i < SampleFormat::packed_end;
			 i++) {
			add_format_item(static_cast<SampleFormat::Format>(i));
		}

		if (attempt_to_restore_format_) {
			set_sample_format(tmp);
		}
	}

	SampleFormat get_sample_format() const
	{
		return static_cast<SampleFormat::Format>(this->currentData().toInt());
	}

	void set_sample_format(SampleFormat fmt)
	{
		for (int i = 0; i < this->count(); i++) {
			if (this->itemData(i).toInt() == fmt) {
				this->setCurrentIndex(i);
				break;
			}
		}
	}

private:
	void add_format_item(SampleFormat f)
	{
		this->addItem(HumanStrings::format_to_string(f),
					  static_cast<SampleFormat::Format>(f));
	}

	bool attempt_to_restore_format_;
};

}

#endif // OAK_SAMPLEFORMATCOMBOBOX_H
