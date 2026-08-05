/***

  Oak Video Editor - Non-Linear Video Editor
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

#ifndef OAK_NODE_C_API_VALUECONVERT_H
#define OAK_NODE_C_API_VALUECONVERT_H

// Internal helpers shared by the oaknode c_api translation units:
// oaknode_value <-> olive::Variant mapping, the pinned
// oaknode_value_type <-> olive::NodeValue::Type mapping, two-stage string
// copy, OakUndoCommand wrapping and the debug alive counter.

#include "node/node.h"

#include <cstring>
#include <new>
#include <string>

#include "value.h"

// Internal layout of the OakUndoCommand handle, shared with the oakundo
// module (src/undo/c_api/commandhandle.h). Included so undoable variants
// can hand out owned handles wrapping freshly created olive commands.
#include "../../undo/c_api/commandhandle.h"

namespace oaknode_c_api
{

/**
 * @brief Bump/release the debug alive counter (defined in node.cpp).
 */
void alive_inc();
void alive_dec();

/**
 * @brief Shared two-stage string getter.
 *
 * Returns the required buffer size in bytes (including the terminating
 * NUL) as a non-negative value.
 */
inline int copy_string(const std::string &value, char *buf, int buf_size)
{
	int required = static_cast<int>(value.size()) + 1;

	if (buf && buf_size > 0) {
		size_t copy_len = value.size();
		if (copy_len > static_cast<size_t>(buf_size) - 1) {
			copy_len = static_cast<size_t>(buf_size) - 1;
		}
		memcpy(buf, value.data(), copy_len);
		buf[copy_len] = '\0';
	}

	return required;
}

/**
 * @brief Pinned mapping olive::NodeValue::Type -> oaknode_value_type
 * (see the table on oaknode_value_type in node/node.h). Types without a
 * POD representation map to OAKNODE_VALUE_NONE.
 */
inline int value_type_to_oak(olive::NodeValue::Type type)
{
	switch (type) {
	case olive::NodeValue::k_int:
		return OAKNODE_VALUE_INT;
	case olive::NodeValue::k_float:
		return OAKNODE_VALUE_FLOAT;
	case olive::NodeValue::k_boolean:
		return OAKNODE_VALUE_BOOL;
	case olive::NodeValue::k_rational:
		return OAKNODE_VALUE_RATIONAL;
	case olive::NodeValue::k_color:
		return OAKNODE_VALUE_COLOR;
	case olive::NodeValue::k_vec2:
		return OAKNODE_VALUE_VEC2;
	case olive::NodeValue::k_vec3:
		return OAKNODE_VALUE_VEC3;
	case olive::NodeValue::k_vec4:
		return OAKNODE_VALUE_VEC4;
	case olive::NodeValue::k_combo:
		return OAKNODE_VALUE_COMBO;
	case olive::NodeValue::k_file:
	case olive::NodeValue::k_text:
	case olive::NodeValue::k_font:
	case olive::NodeValue::k_str_combo:
		return OAKNODE_VALUE_STRING;
	default:
		return OAKNODE_VALUE_NONE;
	}
}

/**
 * @brief 1 if the olive type is string-carried (no POD representation,
 * handled by the dedicated string functions).
 */
inline bool value_type_is_string(olive::NodeValue::Type type)
{
	return type == olive::NodeValue::k_file || type == olive::NodeValue::k_text ||
		   type == olive::NodeValue::k_font || type == olive::NodeValue::k_str_combo;
}

/**
 * @brief Build an olive::Variant from an oaknode_value POD.
 *
 * `value->type` must be one of the POD-carrying oaknode_value_type
 * values (STRING is rejected: no string data fits the POD).
 */
inline bool variant_from_value(const oaknode_value *value, olive::Variant *out)
{
	using olive::core::Color;
	using olive::core::Rational;
	using olive::Vector2D;
	using olive::Vector3D;
	using olive::Vector4D;

	switch (value->type) {
	case OAKNODE_VALUE_INT:
	case OAKNODE_VALUE_COMBO:
		*out = olive::Variant(value->num);
		return true;
	case OAKNODE_VALUE_FLOAT:
		*out = olive::Variant(value->f[0]);
		return true;
	case OAKNODE_VALUE_BOOL:
		*out = olive::Variant(value->num != 0);
		return true;
	case OAKNODE_VALUE_RATIONAL:
		*out = olive::Variant::from_value(
			Rational(static_cast<int>(value->num), static_cast<int>(value->den)));
		return true;
	case OAKNODE_VALUE_COLOR:
		*out = olive::Variant::from_value(
			Color(static_cast<float>(value->f[0]), static_cast<float>(value->f[1]),
				  static_cast<float>(value->f[2]), static_cast<float>(value->f[3])));
		return true;
	case OAKNODE_VALUE_VEC2:
		*out = olive::Variant::from_value(Vector2D(static_cast<float>(value->f[0]),
												   static_cast<float>(value->f[1])));
		return true;
	case OAKNODE_VALUE_VEC3:
		*out = olive::Variant::from_value(Vector3D(static_cast<float>(value->f[0]),
												   static_cast<float>(value->f[1]),
												   static_cast<float>(value->f[2])));
		return true;
	case OAKNODE_VALUE_VEC4:
		*out = olive::Variant::from_value(Vector4D(static_cast<float>(value->f[0]),
												   static_cast<float>(value->f[1]),
												   static_cast<float>(value->f[2]),
												   static_cast<float>(value->f[3])));
		return true;
	default:
		return false;
	}
}

/**
 * @brief Map an olive::Variant of declared type `type` into an
 * oaknode_value POD.
 *
 * Returns OAKNODE_OK, OAKNODE_E_INVALID for string-family types (use the
 * string getters), or OAKNODE_E_FAILED for types without a POD
 * representation.
 */
inline int value_from_variant(olive::NodeValue::Type type, const olive::Variant &v,
							  oaknode_value *out)
{
	using olive::core::Color;
	using olive::core::Rational;
	using olive::Vector2D;
	using olive::Vector3D;
	using olive::Vector4D;

	if (value_type_is_string(type)) {
		return OAKNODE_E_INVALID;
	}

	*out = oaknode_value();
	out->type = value_type_to_oak(type);

	switch (type) {
	case olive::NodeValue::k_none:
		return OAKNODE_OK;
	case olive::NodeValue::k_int:
	case olive::NodeValue::k_combo:
		out->num = v.to_long_long();
		return OAKNODE_OK;
	case olive::NodeValue::k_float:
		out->f[0] = v.to_double();
		return OAKNODE_OK;
	case olive::NodeValue::k_boolean:
		out->num = v.to_bool() ? 1 : 0;
		return OAKNODE_OK;
	case olive::NodeValue::k_rational: {
		Rational r = v.value<Rational>();
		out->num = r.numerator();
		out->den = r.denominator();
		return OAKNODE_OK;
	}
	case olive::NodeValue::k_color: {
		Color c = v.value<Color>();
		out->f[0] = c.red();
		out->f[1] = c.green();
		out->f[2] = c.blue();
		out->f[3] = c.alpha();
		return OAKNODE_OK;
	}
	case olive::NodeValue::k_vec2: {
		Vector2D vec = v.value<Vector2D>();
		out->f[0] = vec.x();
		out->f[1] = vec.y();
		return OAKNODE_OK;
	}
	case olive::NodeValue::k_vec3: {
		Vector3D vec = v.value<Vector3D>();
		out->f[0] = vec.x();
		out->f[1] = vec.y();
		out->f[2] = vec.z();
		return OAKNODE_OK;
	}
	case olive::NodeValue::k_vec4: {
		Vector4D vec = v.value<Vector4D>();
		out->f[0] = vec.x();
		out->f[1] = vec.y();
		out->f[2] = vec.z();
		out->f[3] = vec.w();
		return OAKNODE_OK;
	}
	default:
		out->type = OAKNODE_VALUE_NONE;
		return OAKNODE_E_FAILED;
	}
}

/**
 * @brief Wrap a freshly created olive::UndoCommand in an owned
 * OakUndoCommand handle. Returns NULL on allocation failure.
 */
inline OakUndoCommand *wrap_command(olive::UndoCommand *command)
{
	if (!command) {
		return NULL;
	}

	OakUndoCommand *handle = new (std::nothrow) OakUndoCommand();
	if (!handle) {
		delete command;
		return NULL;
	}
	handle->command = command;
	handle->owned = true;
	return handle;
}

}

#endif // OAK_NODE_C_API_VALUECONVERT_H
