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

#include "node/keyframe.h"

#include "keyframe.h"
#include "node.h"
#include "nodeundo.h"

#include "nodehandle.h"
#include "valueconvert.h"

using oaknode_c_api::make_handle;
using oaknode_c_api::to_native;

namespace
{

/**
 * @brief Convert an oaknode_keyframe_type to olive::NodeKeyframe::Type.
 * The oaknode enum mirrors the olive ordinals exactly (invalid = -1,
 * linear = 0, hold = 1, bezier = 2).
 */
bool keyframe_type_from_oak(int type, olive::NodeKeyframe::Type *out)
{
	if (type < OAKNODE_KEYFRAME_LINEAR || type > OAKNODE_KEYFRAME_BEZIER) {
		return false;
	}
	*out = static_cast<olive::NodeKeyframe::Type>(type);
	return true;
}

/**
 * @brief Undoable set-type command (no olive command class exists for
 * this; defined locally, mirroring NodeOverrideColorCommand's
 * capture-on-redo pattern).
 */
class KeyframeSetTypeCommand : public olive::UndoCommand {
public:
	KeyframeSetTypeCommand(olive::NodeKeyframe *key,
						   olive::NodeKeyframe::Type type)
		: key_(key)
		, new_type_(type)
		, old_type_(olive::NodeKeyframe::k_invalid)
	{
	}

protected:
	virtual void redo() override
	{
		old_type_ = key_->type();
		key_->set_type(new_type_);
	}

	virtual void undo() override
	{
		key_->set_type(old_type_);
	}

private:
	olive::NodeKeyframe *key_;
	olive::NodeKeyframe::Type new_type_;
	olive::NodeKeyframe::Type old_type_;
};

/**
 * @brief Undoable set-bezier-control command (no olive command class
 * exists for this; defined locally).
 */
class KeyframeSetBezierControlCommand : public olive::UndoCommand {
public:
	KeyframeSetBezierControlCommand(olive::NodeKeyframe *key,
									olive::NodeKeyframe::BezierType handle,
									const olive::PointF &point)
		: key_(key)
		, handle_(handle)
		, new_point_(point)
	{
	}

protected:
	virtual void redo() override
	{
		old_point_ = key_->bezier_control(handle_);
		key_->set_bezier_control(handle_, new_point_);
	}

	virtual void undo() override
	{
		key_->set_bezier_control(handle_, old_point_);
	}

private:
	olive::NodeKeyframe *key_;
	olive::NodeKeyframe::BezierType handle_;
	olive::PointF new_point_;
	olive::PointF old_point_;
};

}

OakNodeKeyframe oaknode_keyframe_create(int64_t time_num, int64_t time_den,
										const oaknode_value *value, int type,
										int track, int element,
										const char *input_id,
										OakNodeNode parent_or_null)
{
	olive::NodeKeyframe::Type keyframe_type;
	if (!keyframe_type_from_oak(type, &keyframe_type)) {
		return OakNodeKeyframe{};
	}

	try {
		olive::Variant variant;
		if (value) {
			if (!oaknode_c_api::variant_from_value(value, &variant)) {
				return OakNodeKeyframe{};
			}
		}

		olive::core::Rational time(static_cast<int>(time_num),
								   static_cast<int>(time_den));
		return make_handle<OakNodeKeyframe>(
			new (std::nothrow) olive::NodeKeyframe(
				time, variant, keyframe_type, track, element,
				input_id ? input_id : "",
				to_native<olive::Node>(parent_or_null)),
			true, oaknode_c_api::delete_as<olive::NodeKeyframe>);
	} catch (...) {
		return OakNodeKeyframe{};
	}
}

void oaknode_keyframe_free(OakNodeKeyframe *keyframe)
{
	try {
		oaknode_c_api::free_handle(keyframe);
	} catch (...) {
	}
}

int oaknode_keyframe_get_time(OakNodeKeyframe keyframe,
							  int64_t *out_num, int64_t *out_den)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !out_num || !out_den) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::core::Rational &time = key->time();
		*out_num = time.numerator();
		*out_den = time.denominator();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_time(OakNodeKeyframe keyframe, int64_t time_num,
							  int64_t time_den)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key) {
		return OAKNODE_E_INVALID;
	}

	try {
		key->set_time(olive::core::Rational(static_cast<int>(time_num),
											static_cast<int>(time_den)));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_time_undoable(OakNodeKeyframe keyframe,
									   int64_t time_num, int64_t time_den,
									   OakUndoCommand *out_command)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeParamSetKeyframeTimeCommand(
				key, olive::core::Rational(static_cast<int>(time_num),
										   static_cast<int>(time_den))));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_value(OakNodeKeyframe keyframe,
							   oaknode_value *out)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !out) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Variant &variant = key->value();

		// Preferred path: the parent node's declared input type pins the
		// mapping.
		olive::Node *parent = key->parent();
		if (parent && !key->input().empty() &&
			parent->has_input_with_id(key->input())) {
			return oaknode_c_api::value_from_variant(
				parent->get_input_data_type(key->input()), variant, out);
		}

		// Orphan fallback: infer the POD type from the stored variant
		// content. Numeric kinds are reported as FLOAT (the Variant kind is
		// not recoverable across the POD).
		if (variant.can_convert<olive::core::Rational>()) {
			olive::core::Rational r = variant.value<olive::core::Rational>();
			*out = oaknode_value();
			out->type = OAKNODE_VALUE_RATIONAL;
			out->num = r.numerator();
			out->den = r.denominator();
			return OAKNODE_OK;
		}
		if (variant.can_convert<olive::core::Color>()) {
			return oaknode_c_api::value_from_variant(olive::NodeValue::k_color,
													 variant, out);
		}
		if (variant.can_convert<olive::Vector2D>()) {
			return oaknode_c_api::value_from_variant(olive::NodeValue::k_vec2,
													 variant, out);
		}
		if (variant.can_convert<olive::Vector3D>()) {
			return oaknode_c_api::value_from_variant(olive::NodeValue::k_vec3,
													 variant, out);
		}
		if (variant.can_convert<olive::Vector4D>()) {
			return oaknode_c_api::value_from_variant(olive::NodeValue::k_vec4,
													 variant, out);
		}
		if (variant.can_convert<double>()) {
			*out = oaknode_value();
			out->type = OAKNODE_VALUE_FLOAT;
			out->f[0] = variant.to_double();
			return OAKNODE_OK;
		}

		return OAKNODE_E_FAILED;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_value(OakNodeKeyframe keyframe,
							   const oaknode_value *v)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !v) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Variant variant;
		if (!oaknode_c_api::variant_from_value(v, &variant)) {
			return OAKNODE_E_INVALID;
		}
		key->set_value(variant);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_value_undoable(OakNodeKeyframe keyframe,
										const oaknode_value *v,
										OakUndoCommand *out_command)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !v || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Variant variant;
		if (!oaknode_c_api::variant_from_value(v, &variant)) {
			return OAKNODE_E_INVALID;
		}

		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeParamSetKeyframeValueCommand(key, variant));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_value_string(OakNodeKeyframe keyframe,
									  char *buf, int buf_size)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(key->value().to_string(), buf,
										  buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_value_string(OakNodeKeyframe keyframe,
									  const char *value)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !value) {
		return OAKNODE_E_INVALID;
	}

	try {
		key->set_value(olive::Variant(value));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_value_string_undoable(OakNodeKeyframe keyframe,
											   const char *value,
											   OakUndoCommand *out_command)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !value || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeParamSetKeyframeValueCommand(key,
														olive::Variant(value)));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_type(OakNodeKeyframe keyframe, int *out_type)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !out_type) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_type = static_cast<int>(key->type());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_type(OakNodeKeyframe keyframe, int type)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeKeyframe::Type keyframe_type;
		if (!keyframe_type_from_oak(type, &keyframe_type)) {
			return OAKNODE_E_INVALID;
		}
		key->set_type(keyframe_type);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_type_undoable(OakNodeKeyframe keyframe, int type,
									   OakUndoCommand *out_command)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeKeyframe::Type keyframe_type;
		if (!keyframe_type_from_oak(type, &keyframe_type)) {
			return OAKNODE_E_INVALID;
		}

		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new KeyframeSetTypeCommand(key, keyframe_type));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_bezier_control(OakNodeKeyframe keyframe,
										int handle, double *out_x,
										double *out_y)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !out_x || !out_y) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::PointF point;
		if (handle == OAKNODE_KEYFRAME_IN_HANDLE) {
			point = key->bezier_control_in();
		} else if (handle == OAKNODE_KEYFRAME_OUT_HANDLE) {
			point = key->bezier_control_out();
		} else {
			return OAKNODE_E_INVALID;
		}
		*out_x = point.x();
		*out_y = point.y();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_bezier_control(OakNodeKeyframe keyframe, int handle,
										double x, double y)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key) {
		return OAKNODE_E_INVALID;
	}

	try {
		if (handle == OAKNODE_KEYFRAME_IN_HANDLE) {
			key->set_bezier_control_in(olive::PointF(x, y));
		} else if (handle == OAKNODE_KEYFRAME_OUT_HANDLE) {
			key->set_bezier_control_out(olive::PointF(x, y));
		} else {
			return OAKNODE_E_INVALID;
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_bezier_control_undoable(OakNodeKeyframe keyframe,
												 int handle, double x, double y,
												 OakUndoCommand *out_command)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeKeyframe::BezierType bezier_handle;
		if (handle == OAKNODE_KEYFRAME_IN_HANDLE) {
			bezier_handle = olive::NodeKeyframe::k_in_handle;
		} else if (handle == OAKNODE_KEYFRAME_OUT_HANDLE) {
			bezier_handle = olive::NodeKeyframe::k_out_handle;
		} else {
			return OAKNODE_E_INVALID;
		}

		OakUndoCommand command = oaknode_c_api::wrap_command(
			new KeyframeSetBezierControlCommand(key, bezier_handle,
												olive::PointF(x, y)));
		if (!command.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = command;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_track(OakNodeKeyframe keyframe,
							   int *out_track)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !out_track) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_track = key->track();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_element(OakNodeKeyframe keyframe,
								 int *out_element)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !out_element) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_element = key->element();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_input(OakNodeKeyframe keyframe, char *buf,
							   int buf_size)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(key->input(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_parent(OakNodeKeyframe keyframe,
								OakNodeNode *out_node)
{
	olive::NodeKeyframe *key = to_native<olive::NodeKeyframe>(keyframe);
	if (!key || !out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_node = make_handle<OakNodeNode>(key->parent(), false, nullptr);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
