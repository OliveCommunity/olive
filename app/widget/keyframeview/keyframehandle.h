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

class Node;
class NodeKeyframe;

using olive::core::Rational;

/**
 * @brief Facade accessors for keyframe pointers held by the keyframe
 * views.
 *
 * The keyframe/curve views keep olive::NodeKeyframe* as opaque identity
 * pointers (selection, drawing, hit-testing). All engine data and
 * mutations go through the liboakengine C ABI (oakengine/node.h); the
 * pointer itself is only a handle. Easing types use the facade order:
 * 0 = linear, 1 = bezier, 2 = hold.
 */

inline OakEngineKeyframe *keyhandle(NodeKeyframe *key)
{
	return reinterpret_cast<OakEngineKeyframe *>(key);
}

inline const OakEngineKeyframe *keyhandle(const NodeKeyframe *key)
{
	return reinterpret_cast<const OakEngineKeyframe *>(key);
}

inline NodeKeyframe *keyhandle(OakEngineKeyframe *key)
{
	return reinterpret_cast<NodeKeyframe *>(key);
}

inline OakEngineNode *nodehandle(Node *node)
{
	return reinterpret_cast<OakEngineNode *>(node);
}

inline const OakEngineNode *nodehandle(const Node *node)
{
	return reinterpret_cast<const OakEngineNode *>(node);
}

inline Node *key_node(const NodeKeyframe *key)
{
	return reinterpret_cast<Node *>(
		oakengine_keyframe_get_node(keyhandle(key)));
}

inline Rational key_time(const NodeKeyframe *key)
{
	int64_t num = 0, den = 1;
	oakengine_keyframe_get_time(keyhandle(key), &num, &den);
	return Rational(int(num), int(den));
}

inline int key_easing(const NodeKeyframe *key)
{
	return oakengine_keyframe_get_type(keyhandle(key));
}

inline oak_node_value key_value(const NodeKeyframe *key)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	oakengine_keyframe_get_value(keyhandle(key), &v);
	return v;
}

inline double key_value_as_double(const NodeKeyframe *key)
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

inline void key_set_value_live(NodeKeyframe *key, const oak_node_value &v)
{
	oakengine_keyframe_set_value_live(keyhandle(key), &v);
}

inline QPointF key_bezier_point(const NodeKeyframe *key, int point_index)
{
	double x = 0, y = 0;
	oakengine_keyframe_get_bezier_point(keyhandle(key), point_index, &x, &y);
	return QPointF(x, y);
}

inline QPointF key_valid_bezier_point(const NodeKeyframe *key,
									  int point_index)
{
	double x = 0, y = 0;
	oakengine_keyframe_get_valid_bezier_point(keyhandle(key), point_index,
											  &x, &y);
	return QPointF(x, y);
}

inline void key_set_bezier_point_live(NodeKeyframe *key, int point_index,
									  const QPointF &point)
{
	oakengine_keyframe_set_bezier_point_live(keyhandle(key), point_index,
											 point.x(), point.y());
}

inline void key_set_time_live(NodeKeyframe *key, const Rational &time)
{
	oakengine_keyframe_set_time_live(keyhandle(key), time.numerator(),
									 time.denominator());
}

inline bool key_has_sibling_at_time(const NodeKeyframe *key,
									const Rational &time)
{
	return oakengine_keyframe_has_sibling_at_time(
			   keyhandle(key), time.numerator(), time.denominator()) != 0;
}

inline QString key_input_id(const NodeKeyframe *key)
{
	const int size =
		oakengine_keyframe_get_input_id(keyhandle(key), nullptr, 0);
	QByteArray buf(size + 1, '\0');
	oakengine_keyframe_get_input_id(keyhandle(key), buf.data(),
									int(buf.size()));
	return QString::fromUtf8(buf.constData());
}

inline int key_track(const NodeKeyframe *key)
{
	return oakengine_keyframe_get_track(keyhandle(key));
}

inline int key_element(const NodeKeyframe *key)
{
	return oakengine_keyframe_get_element(keyhandle(key));
}

/**
 * @brief ADL customization points for
 * TimeBasedViewSelectionManager<NodeKeyframe>.
 *
 * The selection manager template calls these unqualified; the generic
 * member-forwarding templates in timebasedviewselectionmanager.h cover
 * other object types (e.g. TimelineMarker), while these overloads route
 * keyframe access through the facade.
 */
inline Rational selection_time(NodeKeyframe *key)
{
	return key_time(key);
}

inline void selection_set_time(NodeKeyframe *key, const Rational &time)
{
	key_set_time_live(key, time);
}

inline bool selection_has_sibling_at_time(NodeKeyframe *key,
										  const Rational &time)
{
	return key_has_sibling_at_time(key, time);
}

inline Node *selection_time_target_parent(NodeKeyframe *key)
{
	return key_node(key);
}

} // namespace olive

#endif // OAK_KEYFRAMEHANDLE_H
