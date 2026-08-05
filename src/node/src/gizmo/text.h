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

#ifndef OAK_TEXTGIZMO_H
#define OAK_TEXTGIZMO_H

#include <string>

#include "gizmo.h"
#include "param.h"

namespace olive
{

/**
 * @brief De-Qt replacement for QRectF (data carrier only, text gizmo rect).
 */
class RectF {
public:
	RectF()
		: x_(0.0)
		, y_(0.0)
		, width_(0.0)
		, height_(0.0)
	{
	}

	RectF(double x, double y, double w, double h)
		: x_(x)
		, y_(y)
		, width_(w)
		, height_(h)
	{
	}

	double x() const
	{
		return x_;
	}
	double y() const
	{
		return y_;
	}
	double width() const
	{
		return width_;
	}
	double height() const
	{
		return height_;
	}

private:
	double x_;
	double y_;
	double width_;
	double height_;
};

class TextGizmo : public NodeGizmo {
public:
	/// Vertical alignment values (formerly Qt::Alignment), matching the
	/// oakengine facade: 0 = AlignTop, 1 = AlignBottom, 2 = AlignVCenter
	enum VerticalAlignment { k_align_top = 0, k_align_bottom = 1, k_align_vcenter = 2 };

	explicit TextGizmo(Node *parent = nullptr);

	const RectF &get_rect() const
	{
		return rect_;
	}
	void set_rect(const RectF &r);

	const std::string &get_html() const
	{
		return text_;
	}
	void set_html(const std::string &t)
	{
		text_ = t;
	}

	void set_input(const NodeKeyframeTrackReference &input)
	{
		input_ = input;
	}

	void update_input_html(const std::string &s, const Rational &time);

	int get_vertical_alignment() const
	{
		return valign_;
	}
	void set_vertical_alignment(int va);

private:
	RectF rect_;

	std::string text_;

	NodeKeyframeTrackReference input_;

	int valign_;
};

}

#endif // OAK_TEXTGIZMO_H
