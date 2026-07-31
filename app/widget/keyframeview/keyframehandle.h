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

#ifndef OAK_KEYFRAMEHANDLE_H
#define OAK_KEYFRAMEHANDLE_H

#include <olive/core/core.h>

#include <QPointF>
#include <QString>

#include "oakengine/node.h"

namespace olive
{

using olive::core::Rational;

/**
 * @brief Facade accessors for keyframe handles held by the keyframe
 * views.
 *
 * The keyframe/curve views keep OakEngineKeyframe* as opaque identity
 * handles (selection, drawing, hit-testing). All engine data and
 * mutations go through the liboakengine C ABI (oakengine/node.h); the
 * handle itself is only an identity. Easing types use the facade order:
 * 0 = linear, 1 = bezier, 2 = hold.
 */

inline OakEngineNode *key_node(const OakEngineKeyframe *key)
{
	return oakengine_keyframe_get_node(key);
}

inline Rational key_time(const OakEngineKeyframe *key)
{
	int64_t num = 0, den = 1;
	oakengine_keyframe_get_time(key, &num, &den);
	return Rational(int(num), int(den));
}

inline int key_easing(const OakEngineKeyframe *key)
{
	return oakengine_keyframe_get_type(key);
}

inline oak_node_value key_value(const OakEngineKeyframe *key)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	oakengine_keyframe_get_value(key, &v);
	return v;
}

inline double key_value_as_double(const OakEngineKeyframe *key)
{
	const oak_node_value v = key_value(key);
	switch (v.type) {
	case OAK_NODE_VALUE_INT:
	case OAK_NODE_VALUE_COMBO:
	case OAK_NODE_VALUE_BOOL:
		return double(v.num);
	case OAK_NODE_VALUE_RATIONAL:
		return v.den ? double(v.num) / double(v.den) : 0.0;
	default:
		return v.f[0];
	}
}

inline void key_set_value_live(OakEngineKeyframe *key, const oak_node_value &v)
{
	oakengine_keyframe_set_value_live(key, &v);
}

inline QPointF key_bezier_point(const OakEngineKeyframe *key, int point_index)
{
	double x = 0, y = 0;
	oakengine_keyframe_get_bezier_point(key, point_index, &x, &y);
	return QPointF(x, y);
}

inline QPointF key_valid_bezier_point(const OakEngineKeyframe *key,
									  int point_index)
{
	double x = 0, y = 0;
	oakengine_keyframe_get_valid_bezier_point(key, point_index, &x, &y);
	return QPointF(x, y);
}

inline void key_set_bezier_point_live(OakEngineKeyframe *key, int point_index,
									  const QPointF &point)
{
	oakengine_keyframe_set_bezier_point_live(key, point_index,
											 point.x(), point.y());
}

inline void key_set_time_live(OakEngineKeyframe *key, const Rational &time)
{
	oakengine_keyframe_set_time_live(key, time.numerator(),
									 time.denominator());
}

/**
 * @brief NodeKeyframe::has_sibling_at_time() equivalent: true when another
 * keyframe sits at `time` on the same input/track/element.
 *
 * NOTE: oakengine_keyframe_has_sibling_at_time() is NOT used here — its
 * facade contract (whole-second time, ignored track) does not match the
 * engine semantics, so the check is done with an exact rational lookup.
 */
inline bool key_has_sibling_at_time(const OakEngineKeyframe *key,
									const Rational &time)
{
	char input_id[256];
	input_id[0] = '\0';
	oakengine_keyframe_get_input_id(key, input_id, sizeof(input_id));
	OakEngineKeyframe *sibling = oakengine_node_keyframe_handle_at_time(
		oakengine_keyframe_get_node(key), input_id,
		oakengine_keyframe_get_element(key), oakengine_keyframe_get_track(key),
		time.numerator(), time.denominator());
	return sibling && sibling != key;
}

inline QString key_input_id(const OakEngineKeyframe *key)
{
	const int size =
		oakengine_keyframe_get_input_id(key, nullptr, 0);
	QByteArray buf(size + 1, '\0');
	oakengine_keyframe_get_input_id(key, buf.data(),
									int(buf.size()));
	return QString::fromUtf8(buf.constData());
}

inline int key_track(const OakEngineKeyframe *key)
{
	return oakengine_keyframe_get_track(key);
}

inline int key_element(const OakEngineKeyframe *key)
{
	return oakengine_keyframe_get_element(key);
}

/**
 * @brief ADL customization points for
 * TimeBasedViewSelectionManager<OakEngineKeyframe>.
 *
 * The selection manager template calls these unqualified; the overloads in
 * timeruler/markerhandle.h cover TimelineMarker, while these route keyframe
 * access through the facade.
 */
inline Rational selection_time(OakEngineKeyframe *key)
{
	return key_time(key);
}

inline void selection_set_time(OakEngineKeyframe *key, const Rational &time)
{
	key_set_time_live(key, time);
}

inline bool selection_has_sibling_at_time(OakEngineKeyframe *key,
										  const Rational &time)
{
	return key_has_sibling_at_time(key, time);
}

inline OakEngineNode *selection_time_target_parent(OakEngineKeyframe *key)
{
	return key_node(key);
}

} // namespace olive

#endif // OAK_KEYFRAMEHANDLE_H
