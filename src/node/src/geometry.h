/***

  Oak - Non-Linear Video Editor
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

#ifndef OAK_NODEGEOMETRY_H
#define OAK_NODEGEOMETRY_H

#include <vector>

#include "mathtypes.h"

// NOTE: the de-Qt replacements for QRectF and QLineF live with their gizmo
// owners: olive::RectF in "node/gizmo/text.h", olive::LineF in
// "node/gizmo/line.h" (gizmo wave). QPolygonF is std::vector<olive::PointF>.

namespace olive
{

/**
 * @brief De-Qt replacement for QPainterPath (minimal recording POD).
 *
 * Records path elements only; rasterization is performed by the facade/app
 * layer (see PathFillBackend below), which owns an actual rasterizer
 * (formerly QPainter::drawPath with a white brush and no pen).
 */
class PainterPath {
public:
	struct Element {
		enum Type {
			k_move_to,
			k_line_to,
			k_cubic_to // p1/p2 = control points, p3 = end point
		};

		Type type;
		PointF p1;
		PointF p2;
		PointF p3;
	};

	void move_to(const PointF &p)
	{
		elements_.push_back({ Element::k_move_to, p, PointF(), PointF() });
	}

	void line_to(const PointF &p)
	{
		elements_.push_back({ Element::k_line_to, p, PointF(), PointF() });
	}

	void cubic_to(const PointF &c1, const PointF &c2, const PointF &end)
	{
		elements_.push_back({ Element::k_cubic_to, c1, c2, end });
	}

	bool empty() const
	{
		return elements_.empty();
	}

	const std::vector<Element> &elements() const
	{
		return elements_;
	}

	/**
	 * @brief QPainterPath::translated() equivalent (returns a copy).
	 */
	PainterPath translated(const PointF &offset) const
	{
		PainterPath copy = *this;
		for (Element &e : copy.elements_) {
			e.p1 += offset;
			e.p2 += offset;
			e.p3 += offset;
		}
		return copy;
	}

private:
	std::vector<Element> elements_;
};

/**
 * @brief Backend hook for filling a PainterPath into an 8-bit RGBA
 *        premultiplied buffer (replaces the QPainter rasterization in
 *        generator nodes' generate_frame()).
 *
 * The transform mirrors the original QPainter calls `scale(sx, sy)` followed
 * by `translate(tx, ty)`, i.e. a path point p lands at
 * ((p.x + tx) * sx, (p.y + ty) * sy). The path is filled solid white
 * (formerly QPainter with Qt::white brush and Qt::NoPen).
 *
 * Installed by the facade/app layer via set_path_fill_backend(). When no
 * backend is installed, callers leave the (already cleared) buffer untouched.
 */
using PathFillBackend = void (*)(const PainterPath &path, double scale_x,
								double scale_y, double translate_x,
								double translate_y, unsigned char *rgba,
								int width, int height, int linesize_bytes);

inline PathFillBackend g_path_fill_backend = nullptr;

inline void set_path_fill_backend(PathFillBackend backend)
{
	g_path_fill_backend = backend;
}

inline PathFillBackend path_fill_backend()
{
	return g_path_fill_backend;
}

}

#endif // OAK_NODEGEOMETRY_H
