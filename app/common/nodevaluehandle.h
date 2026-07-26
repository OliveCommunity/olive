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

#include "node/value.h"
#include "oakengine/node.h"

namespace olive
{

/**
 * @brief Convert engine NodeValue::Type to oak_node_value_type (app-side).
 *
 * The two enums do NOT share ordinals (e.g. k_boolean=4 vs BOOL=3), so a
 * plain int cast is a bug. Mirrors from_c_type() in
 * engine/src/capi/node.cpp. Lives in an app header, NOT in the public
 * facade headers — the C ABI surface stays pure C (see
 * docs/zh/r6-cleanup-plan.md red line 3 context). Returns -1 for types the
 * facade cannot represent (caller falls back to the input's declared type).
 */
inline int node_value_type_to_c(NodeValue::Type t)
{
	switch (t) {
	case NodeValue::k_int: return OAK_NODE_VALUE_INT;
	case NodeValue::k_float: return OAK_NODE_VALUE_FLOAT;
	case NodeValue::k_boolean: return OAK_NODE_VALUE_BOOL;
	case NodeValue::k_rational: return OAK_NODE_VALUE_RATIONAL;
	case NodeValue::k_color: return OAK_NODE_VALUE_COLOR;
	case NodeValue::k_vec2: return OAK_NODE_VALUE_VEC2;
	case NodeValue::k_vec3: return OAK_NODE_VALUE_VEC3;
	case NodeValue::k_vec4: return OAK_NODE_VALUE_VEC4;
	case NodeValue::k_combo: return OAK_NODE_VALUE_COMBO;
	case NodeValue::k_file: return OAK_NODE_VALUE_STRING;
	case NodeValue::k_text: return OAK_NODE_VALUE_TEXT;
	case NodeValue::k_font: return OAK_NODE_VALUE_FONT;
	case NodeValue::k_str_combo: return OAK_NODE_VALUE_STR_COMBO;
	case NodeValue::k_binary: return OAK_NODE_VALUE_BINARY;
	case NodeValue::k_bezier: return OAK_NODE_VALUE_BEZIER;
	case NodeValue::k_texture: return OAK_NODE_VALUE_TEXTURE;
	case NodeValue::k_samples: return OAK_NODE_VALUE_SAMPLES;
	case NodeValue::k_video_params: return OAK_NODE_VALUE_VIDEO_PARAMS;
	case NodeValue::k_audio_params: return OAK_NODE_VALUE_AUDIO_PARAMS;
	default: return -1;
	}
}

} // namespace olive

#endif // OAK_NODEVALUEHANDLE_H
