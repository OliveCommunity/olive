/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#ifndef OAK_COLORTRANSFORM_H
#define OAK_COLORTRANSFORM_H

#include <string>

namespace olive
{

/**
 * @brief Describes a color transform: either a plain output colorspace or a
 *        display/view/look triplet (sunk from engine/render/colortransform.h,
 *        QString replaced with std::string).
 */
class ColorTransform {
public:
	ColorTransform()
	{
		is_display_ = false;
	}

	ColorTransform(const std::string &output)
	{
		is_display_ = false;
		output_ = output;
	}

	ColorTransform(const std::string &display, const std::string &view,
				   const std::string &look)
	{
		is_display_ = true;
		output_ = display;
		view_ = view;
		look_ = look;
	}

	bool is_display() const
	{
		return is_display_;
	}

	const std::string &display() const
	{
		return output_;
	}

	const std::string &output() const
	{
		return output_;
	}

	const std::string &view() const
	{
		return view_;
	}

	const std::string &look() const
	{
		return look_;
	}

private:
	std::string output_;

	bool is_display_;
	std::string view_;
	std::string look_;
};

}

#endif // OAK_COLORTRANSFORM_H
