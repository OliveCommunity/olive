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

#ifndef OAK_KEYFRAMETYPES_H
#define OAK_KEYFRAMETYPES_H

namespace olive
{

/**
 * @brief App-side keyframe enum mirrors.
 *
 * The engine olive::NodeKeyframe::Type mirror lives in
 * app/common/nodevaluehandle.h as NodeKeyframeType (kept with the
 * NodeValueType mirror); the enums below cover the remaining keyframe
 * domains. Enumerator ordinals must stay in sync with the engine / facade:
 * the C ABI transports these as ints.
 */
class KeyframeTypes {
public:
	/// Mirror of engine's olive::NodeKeyframe::BezierType
	/// (engine/node/keyframe.h; oakengine_keyframe_opposing_bezier_type()
	/// transports these ordinals).
	enum BezierType { k_in_handle, k_out_handle };

	/**
	 * @brief Facade easing order used by the C ABI
	 * (oakengine_keyframe_get_type(), oak::Keyframe::type()): NOT the same
	 * order as the engine Type enum (see NodeKeyframeType in
	 * common/nodevaluehandle.h).
	 */
	enum FacadeType {
		k_facade_invalid = -1,
		k_facade_linear = 0,
		k_facade_bezier = 1,
		k_facade_hold = 2
	};
};

}

#endif // OAK_KEYFRAMETYPES_H
