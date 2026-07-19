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

#ifndef OAK_TOOL_H
#define OAK_TOOL_H

#include <QCoreApplication>
#include <QString>

#include "common/define.h"

namespace olive
{

class Tool {
public:
	/**
   * @brief A list of tools that can be used throughout the application
   */
	enum Item {
		/// No tool. This should never be set as the application tool, its only real purpose is to indicate the lack of
		/// a tool somewhere similar to nullptr.
		k_none,

		/// Pointer tool
		k_pointer,

		/// Edit tool
		k_edit,

		/// Ripple tool
		k_ripple,

		/// Rolling tool
		k_rolling,

		/// Razor tool
		k_razor,

		/// Slip tool
		k_slip,

		/// Slide tool
		k_slide,

		/// Hand tool
		k_hand,

		/// Zoom tool
		k_zoom,

		/// Transition tool
		k_transition,

		/// Record tool
		k_record,

		/// Add tool
		k_add,

		/// Track select tool
		k_track_select,

		k_count
	};

	/**
   * @brief Tools that can be added using the kAdd tool
   */
	enum AddableObject {
		/// An empty clip
		k_addable_empty,

		/// A video clip showing a generic video placeholder
		k_addable_bars,

		/// A video clip showing a primitive shape
		k_addable_shape,

		/// A video clip with a solid connected
		k_addable_solid,

		/// A video clip with a title connected
		k_addable_title,

		/// An audio clip with a sine connected to it
		k_addable_tone,

		/// A subtitle clip
		k_addable_subtitle,

		k_addable_count
	};

	static QString get_addable_object_name(const AddableObject &a)
	{
		switch (a) {
		case k_addable_empty:
			return QCoreApplication::translate("Tool", "Empty");
		case k_addable_bars:
			return QCoreApplication::translate("Tool", "Bars");
		case k_addable_shape:
			return QCoreApplication::translate("Tool", "Shape");
		case k_addable_solid:
			return QCoreApplication::translate("Tool", "Solid");
		case k_addable_title:
			return QCoreApplication::translate("Tool", "Title");
		case k_addable_tone:
			return QCoreApplication::translate("Tool", "Tone");
		case k_addable_subtitle:
			return QCoreApplication::translate("Tool", "Subtitle");
		case k_addable_count:
			break;
		}

		return QCoreApplication::translate("Tool", "Unknown");
	}

	static QString get_addable_object_id(const AddableObject &a)
	{
		switch (a) {
		case k_addable_empty:
			return QStringLiteral("empty");
		case k_addable_bars:
			return QStringLiteral("bars");
		case k_addable_shape:
			return QStringLiteral("shape");
		case k_addable_solid:
			return QStringLiteral("solid");
		case k_addable_title:
			return QStringLiteral("title");
		case k_addable_tone:
			return QStringLiteral("tone");
		case k_addable_subtitle:
			return QStringLiteral("subtitle");
		case k_addable_count:
			break;
		}

		return QString();
	}
};

}

#endif // OAK_TOOL_H
