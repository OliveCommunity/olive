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

#ifndef OAK_MARKERPAINTING_H
#define OAK_MARKERPAINTING_H

#include <olive/core/core.h>

#include <QFontMetrics>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QString>

namespace olive
{

/**
 * @brief Marker painting helpers (pure UI code, moved app-side from the
 * engine's TimelineMarker::draw()/get_marker_height()).
 *
 * The caller passes the marker's data by value (name, color index and
 * in/out times) so no engine types are needed for drawing.
 */
namespace MarkerPainting
{

/// Height of a marker in pixels for the given font (was
/// TimelineMarker::get_marker_height()).
int height(const QFontMetrics &fm);

/// Draw a marker at `pt` (bottom-center anchor) and return its bounding
/// rect (was TimelineMarker::draw()). `max_right` of -1 disables the
/// label text; `scale` is pixels per second for ranged markers.
QRect draw(QPainter *p, const QPoint &pt, int max_right, double scale,
		   bool selected, const QString &name, int color,
		   const core::Rational &in, const core::Rational &out);

}

}

#endif // OAK_MARKERPAINTING_H
