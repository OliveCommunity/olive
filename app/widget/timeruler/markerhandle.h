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

#ifndef OAK_MARKERHANDLE_H
#define OAK_MARKERHANDLE_H

#include <olive/core/core.h>

#include <QByteArray>
#include <QString>

#include "oakengine/timeline.h"

namespace olive
{

using olive::core::Rational;
using olive::core::TimeRange;

/**
 * @brief Facade accessors for marker handles held by the ruler/scrollbar
 * widgets.
 *
 * The widgets keep OakEngineMarker* / OakEngineMarkerList* as opaque
 * identity handles (selection, drawing, hit-testing). All engine data and
 * mutations go through the liboakengine C ABI (oakengine/timeline.h); the
 * handle itself is only an identity. Marker times are rational seconds
 * (num/den pairs), not frame timestamps.
 */

inline TimeRange marker_time(const OakEngineMarker *marker)
{
	int64_t in_num = 0, in_den = 1, out_num = 0, out_den = 1;
	oakengine_marker_get_time(marker, &in_num, &in_den,
							  &out_num, &out_den);
	return TimeRange(Rational(int(in_num), int(in_den)),
					 Rational(int(out_num), int(out_den)));
}

inline QString marker_name(const OakEngineMarker *marker)
{
	const int size =
		oakengine_marker_get_name(marker, nullptr, 0);
	QByteArray buf(size + 1, '\0');
	oakengine_marker_get_name(marker, buf.data(),
							  int(buf.size()));
	return QString::fromUtf8(buf.constData());
}

inline int marker_color(const OakEngineMarker *marker)
{
	return oakengine_marker_get_color(marker);
}

inline bool marker_has_sibling_at_time(const OakEngineMarker *marker,
									   const Rational &time)
{
	return oakengine_marker_has_sibling_at_time(
			   marker, time.numerator(), time.denominator()) != 0;
}

inline void marker_set_time_live(OakEngineMarker *marker,
								 const TimeRange &range)
{
	oakengine_marker_set_time_live(
		marker, range.in().numerator(), range.in().denominator(),
		range.out().numerator(), range.out().denominator());
}

/**
 * @brief ADL customization points for
 * TimeBasedViewSelectionManager<OakEngineMarker>.
 *
 * The selection manager template calls these unqualified; these overloads
 * route marker access through the facade (see
 * widget/keyframeview/keyframehandle.h for the keyframe equivalent).
 * Markers drag both their in and out points, hence selection_time_end().
 * selection_time_target_parent() returns nullptr: marker drags never pass
 * a time target.
 */
inline Rational selection_time(OakEngineMarker *marker)
{
	return marker_time(marker).in();
}

inline Rational selection_time_end(OakEngineMarker *marker)
{
	return marker_time(marker).out();
}

inline void selection_set_time(OakEngineMarker *marker, const Rational &time)
{
	// Move the in-point keeping the range length (was
	// TimelineMarker::set_time(const Rational &))
	const TimeRange range = marker_time(marker);
	const Rational length = range.out() - range.in();
	marker_set_time_live(marker, TimeRange(time, time + length));
}

inline bool selection_has_sibling_at_time(OakEngineMarker *marker,
										  const Rational &time)
{
	return marker_has_sibling_at_time(marker, time);
}

inline OakEngineNode *selection_time_target_parent(OakEngineMarker *marker)
{
	Q_UNUSED(marker)
	return nullptr;
}

} // namespace olive

#endif // OAK_MARKERHANDLE_H
