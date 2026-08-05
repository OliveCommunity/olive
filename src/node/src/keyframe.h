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

#ifndef OAK_NODEKEYFRAME_H
#define OAK_NODEKEYFRAME_H

#include <memory>
#include <string>
#include <vector>

#include "param.h"

namespace olive
{

class Node;
class XmlStreamReader;
class XmlStreamWriter;

/**
 * @brief A point of data to be used at a certain time and interpolated with other data
 */
class NodeKeyframe {
public:
	/**
   * @brief Methods of interpolation to use with this keyframe
   */
	enum Type { k_invalid = -1, k_linear, k_hold, k_bezier };

	/**
   * @brief The two types of bezier handles that are available on bezier keyframes
   */
	enum BezierType { k_in_handle, k_out_handle };

	static const Type k_default_type;

	/**
   * @brief NodeKeyframe Constructor
   */
	NodeKeyframe(const Rational &time, const Variant &value, Type type,
				 int track, int element, const std::string &input,
				 Node *parent = nullptr);
	NodeKeyframe();

	virtual ~NodeKeyframe();

	NodeKeyframe *copy(int element, Node *parent = nullptr) const;
	NodeKeyframe *copy(Node *parent = nullptr) const;

	Node *parent() const;

	/**
	 * @brief Set the Node this keyframe belongs to.
	 *
	 * Called by Node::add_keyframe()/remove_keyframe(). Replaces the former
	 * QObject parent-child mechanism.
	 */
	void set_parent(Node *p)
	{
		parent_ = p;
	}

	const std::string &input() const
	{
		return input_;
	}
	void set_input(const std::string &input)
	{
		input_ = input;
	}

	NodeKeyframeTrackReference key_track_ref() const
	{
		return NodeKeyframeTrackReference(
			NodeInput(parent(), input(), element()), track());
	}

	/**
   * @brief The time this keyframe is set at
   */
	const Rational &time() const;
	void set_time(const Rational &time);

	/**
   * @brief The value of this keyframe (i.e. the value to use at this keyframe's time)
   */
	const Variant &value() const;
	void set_value(const Variant &value);

	/**
   * @brief The method of interpolation to use with this keyframe
   */
	const Type &type() const;
	void set_type(const Type &type);
	void set_type_no_bezier_adj(const Type &type);

	/**
   * @brief For bezier interpolation, the control point leading into this keyframe
   */
	const PointF &bezier_control_in() const;
	void set_bezier_control_in(const PointF &control);

	/**
   * @brief For bezier interpolation, the control point leading out of this keyframe
   */
	const PointF &bezier_control_out() const;
	void set_bezier_control_out(const PointF &control);

	/**
  * @brief Returns a known good bezier that should be used in actual animation
  *
  * While users can move the bezier controls wherever they want, we have to limit their usage
  * internally to prevent a situation where the animation overlaps (i.e. there can only be one Y
  * value for any given X in the bezier line). This returns a value that is known good.
  */
	PointF valid_bezier_control_in() const;
	PointF valid_bezier_control_out() const;

	/**
   * @brief Convenience functions for retrieving/setting bezier handle information with a BezierType
   */
	const PointF &bezier_control(BezierType type) const;
	void set_bezier_control(BezierType type, const PointF &control);

	/**
   * @brief The track that this keyframe belongs to
   *
   * For the majority of keyfreames, this will be 0, but for some types, such as kVec2, this will be 0 for X keyframes
   * and 1 for Y keyframes, etc.
   */
	int track() const
	{
		return track_;
	}
	void set_track(int t)
	{
		track_ = t;
	}

	int element() const
	{
		return element_;
	}
	void set_element(int e)
	{
		element_ = e;
	}

	/**
   * @brief Convenience function for getting the opposite handle type (e.g. kInHandle <-> kOutHandle)
   */
	static BezierType get_opposing_bezier_type(BezierType type);

	NodeKeyframe *previous() const
	{
		return previous_;
	}

	void set_previous(NodeKeyframe *keyframe)
	{
		previous_ = keyframe;
	}

	NodeKeyframe *next() const
	{
		return next_;
	}

	void set_next(NodeKeyframe *keyframe)
	{
		next_ = keyframe;
	}

	bool has_sibling_at_time(const Rational &t) const;

	bool load(XmlStreamReader *reader, NodeValue::Type data_type);
	void save(XmlStreamWriter *writer, NodeValue::Type data_type) const;

private:
	Rational time_;

	Variant value_;

	Type type_;

	PointF bezier_control_in_;

	PointF bezier_control_out_;

	std::string input_;

	int track_;

	int element_;

	Node *parent_;

	NodeKeyframe *previous_;

	NodeKeyframe *next_;
};

using NodeKeyframeTrack = std::vector<NodeKeyframe *>;

}

#endif // OAK_NODEKEYFRAME_H
