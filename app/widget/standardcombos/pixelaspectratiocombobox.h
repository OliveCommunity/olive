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

#ifndef OAK_PIXELASPECTRATIOCOMBOBOX_H
#define OAK_PIXELASPECTRATIOCOMBOBOX_H

#include <QComboBox>

#include "dialog/ratiodialog.h"
#include "render/videoparams.h"

namespace olive
{

class PixelAspectRatioComboBox : public QComboBox {
	Q_OBJECT
public:
	PixelAspectRatioComboBox(QWidget *parent = nullptr)
		: QComboBox(parent)
		, dont_prompt_custom_par_(false)
	{
		QStringList par_names = VideoParams::get_standard_pixel_aspect_ratio_names();
		for (int i = 0; i < VideoParams::k_standard_pixel_aspects.size(); i++) {
			const Rational &ratio = VideoParams::k_standard_pixel_aspects.at(i);

			this->addItem(par_names.at(i), QVariant::fromValue(ratio));
		}

		// Always add custom item last, much of the logic relies on this. Set this to the current AR so
		// that if none of the above are ==, it will eventually select this item
		this->addItem(QString());
		update_custom_item(Rational());

		// Pick up index signal to query for custom aspect ratio if requested
		connect(this,
				static_cast<void (QComboBox::*)(int)>(
					&QComboBox::currentIndexChanged),
				this, &PixelAspectRatioComboBox::index_changed);
	}

	Rational get_pixel_aspect_ratio() const
	{
		return this->currentData().value<Rational>();
	}

	void set_pixel_aspect_ratio(const Rational &r)
	{
		// Determine which index to select on startup
		for (int i = 0; i < this->count(); i++) {
			if (this->itemData(i).value<Rational>() == r) {
				this->setCurrentIndex(i);
				return;
			}
		}

		// Must not have found the ratio, so it must be custom
		update_custom_item(r);
		dont_prompt_custom_par_ = true;
		this->setCurrentIndex(this->count() - 1);
		dont_prompt_custom_par_ = false;
	}

private slots:
	void index_changed(int index)
	{
		if (dont_prompt_custom_par_) {
			return;
		}

		// Detect if custom was selected, in which case query what the new AR should be
		if (index == this->count() - 1) {
			// Query for custom pixel aspect ratio
			bool ok;

			double custom_ratio = get_float_ratio_from_user(
				this, tr("Set Custom Pixel Aspect Ratio"), &ok);

			if (ok) {
				update_custom_item(Rational::from_double(custom_ratio));
			}
		}
	}

private:
	void update_custom_item(const Rational &ratio)
	{
		const int custom_index = this->count() - 1;

		if (ratio.isNull()) {
			this->setItemText(custom_index, tr("Custom..."));

			// Use 1:1 to prevent any real chance of the PAR being set to 0
			this->setItemData(custom_index, QVariant::fromValue(Rational(1)));
		} else {
			this->setItemText(custom_index,
							  VideoParams::format_pixel_aspect_ratio_string(
								  tr("Custom (%1)"), ratio));
			this->setItemData(custom_index, QVariant::fromValue(ratio));
		}
	}

	bool dont_prompt_custom_par_;
};

}

#endif // OAK_PIXELASPECTRATIOCOMBOBOX_H
