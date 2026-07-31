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

#ifndef OAK_NODEVALUEHANDLE_H
#define OAK_NODEVALUEHANDLE_H

#include "oakengine/node.h"

namespace olive
{

/**
 * @brief App-side mirror of engine NodeValue::Type (engine/node/value.h).
 *
 * Ordinals MUST stay in sync with the engine enum: call sites that still
 * hold an engine NodeValue::Type reach the helpers below through an
 * implicit int conversion. The static_asserts pin the engine ordinals as
 * of the R8 wave 1 migration; update both sides together.
 *
 * NOTE: the C ABI oak_node_value_type (OAK_NODE_VALUE_*) does NOT share
 * these ordinals (e.g. k_boolean=4 vs OAK_NODE_VALUE_BOOL=3), so a plain
 * int cast between the two domains is a bug. Convert with
 * node_value_type_to_c().
 */
class NodeValueType
{
public:
	enum Type {
		k_none = 0,
		k_int,
		k_float,
		k_rational,
		k_boolean,
		k_color,
		k_matrix,
		k_text,
		k_font,
		k_file,
		k_texture,
		k_samples,
		k_vec2,
		k_vec3,
		k_vec4,
		k_bezier,
		k_combo,
		k_str_combo,
		k_video_params,
		k_audio_params,
		k_subtitle_params,
		k_binary,
		k_push_button,
		k_data_type_count
	};
};

// Ordinal sync guards against engine/node/value.h.
static_assert(NodeValueType::k_boolean == 4,
			  "NodeValueType out of sync with engine NodeValue::Type");
static_assert(NodeValueType::k_vec2 == 12,
			  "NodeValueType out of sync with engine NodeValue::Type");
static_assert(NodeValueType::k_data_type_count == 23,
			  "NodeValueType out of sync with engine NodeValue::Type");

/**
 * @brief App-side mirror of engine NodeKeyframe::Type
 * (engine/node/keyframe.h).
 *
 * Ordinals MUST stay in sync with the engine enum (k_invalid=-1,
 * k_linear=0, k_hold=1, k_bezier=2). The facade easing type transported
 * over the C ABI is a DIFFERENT numbering: 0=linear, 1=bezier, 2=hold
 * (see oakengine/node.h) — convert with NodeKeyframeTypeToFacade() in
 * oakvaluehelper.h, never with a plain cast.
 */
class NodeKeyframeType
{
public:
	enum Type { k_invalid = -1, k_linear = 0, k_hold = 1, k_bezier = 2 };
};

// Ordinal sync guard against engine/node/keyframe.h.
static_assert(NodeKeyframeType::k_bezier == 2,
			  "NodeKeyframeType out of sync with engine NodeKeyframe::Type");

/**
 * @brief Convert engine NodeValue::Type ordinals to oak_node_value_type (app-side).
 *
 * `t` uses engine NodeValue::Type ordinals (see the NodeValueType mirror
 * above); callers holding an engine NodeValue::Type pass it through an
 * implicit int conversion. The two enums do NOT share ordinals (e.g.
 * k_boolean=4 vs BOOL=3), so a plain int cast is a bug. Mirrors
 * from_c_type() in engine/src/capi/node.cpp. Lives in an app header, NOT
 * in the public facade headers — the C ABI surface stays pure C (see
 * docs/zh/r6-cleanup-plan.md red line 3 context). Returns -1 for types the
 * facade cannot represent (caller falls back to the input's declared type).
 */
inline int node_value_type_to_c(int t)
{
	switch (t) {
	case NodeValueType::k_int: return OAK_NODE_VALUE_INT;
	case NodeValueType::k_float: return OAK_NODE_VALUE_FLOAT;
	case NodeValueType::k_boolean: return OAK_NODE_VALUE_BOOL;
	case NodeValueType::k_rational: return OAK_NODE_VALUE_RATIONAL;
	case NodeValueType::k_color: return OAK_NODE_VALUE_COLOR;
	case NodeValueType::k_vec2: return OAK_NODE_VALUE_VEC2;
	case NodeValueType::k_vec3: return OAK_NODE_VALUE_VEC3;
	case NodeValueType::k_vec4: return OAK_NODE_VALUE_VEC4;
	case NodeValueType::k_combo: return OAK_NODE_VALUE_COMBO;
	case NodeValueType::k_file: return OAK_NODE_VALUE_STRING;
	case NodeValueType::k_text: return OAK_NODE_VALUE_TEXT;
	case NodeValueType::k_font: return OAK_NODE_VALUE_FONT;
	case NodeValueType::k_str_combo: return OAK_NODE_VALUE_STR_COMBO;
	case NodeValueType::k_binary: return OAK_NODE_VALUE_BINARY;
	case NodeValueType::k_bezier: return OAK_NODE_VALUE_BEZIER;
	case NodeValueType::k_texture: return OAK_NODE_VALUE_TEXTURE;
	case NodeValueType::k_samples: return OAK_NODE_VALUE_SAMPLES;
	case NodeValueType::k_video_params: return OAK_NODE_VALUE_VIDEO_PARAMS;
	case NodeValueType::k_audio_params: return OAK_NODE_VALUE_AUDIO_PARAMS;
	default: return -1;
	}
}

} // namespace olive

#endif // OAK_NODEVALUEHANDLE_H
