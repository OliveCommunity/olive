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

#include "valueconvert.h"

namespace
{

inline olive::NodeKeyframe *to_key(OakNodeKeyframe *keyframe)
{
	return reinterpret_cast<olive::NodeKeyframe *>(keyframe);
}

inline const olive::NodeKeyframe *to_key(const OakNodeKeyframe *keyframe)
{
	return reinterpret_cast<const olive::NodeKeyframe *>(keyframe);
}

inline olive::Node *to_node(OakNodeNode *node)
{
	return reinterpret_cast<olive::Node *>(node);
}

inline OakNodeNode *from_node(olive::Node *node)
{
	return reinterpret_cast<OakNodeNode *>(node);
}

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

OakNodeKeyframe *oaknode_keyframe_create(int64_t time_num, int64_t time_den,
										 const oaknode_value *value, int type,
										 int track, int element,
										 const char *input_id,
										 OakNodeNode *parent_or_null)
{
	olive::NodeKeyframe::Type keyframe_type;
	if (!keyframe_type_from_oak(type, &keyframe_type)) {
		return NULL;
	}

	try {
		olive::Variant variant;
		if (value) {
			if (!oaknode_c_api::variant_from_value(value, &variant)) {
				return NULL;
			}
		}

		olive::core::Rational time(static_cast<int>(time_num),
								   static_cast<int>(time_den));
		olive::NodeKeyframe *key = new (std::nothrow) olive::NodeKeyframe(
			time, variant, keyframe_type, track, element,
			input_id ? input_id : "", to_node(parent_or_null));
		if (key) {
			oaknode_c_api::alive_inc();
		}
		return reinterpret_cast<OakNodeKeyframe *>(key);
	} catch (...) {
		return NULL;
	}
}

void oaknode_keyframe_free(OakNodeKeyframe *keyframe)
{
	if (!keyframe) {
		return;
	}

	try {
		delete to_key(keyframe);
		oaknode_c_api::alive_dec();
	} catch (...) {
	}
}

int oaknode_keyframe_get_time(const OakNodeKeyframe *keyframe,
							  int64_t *out_num, int64_t *out_den)
{
	if (!keyframe || !out_num || !out_den) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::core::Rational &time = to_key(keyframe)->time();
		*out_num = time.numerator();
		*out_den = time.denominator();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_time(OakNodeKeyframe *keyframe, int64_t time_num,
							  int64_t time_den)
{
	if (!keyframe) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_key(keyframe)->set_time(olive::core::Rational(
			static_cast<int>(time_num), static_cast<int>(time_den)));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_time_undoable(OakNodeKeyframe *keyframe,
									   int64_t time_num, int64_t time_den,
									   OakUndoCommand *out_command)
{
	if (!keyframe || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeParamSetKeyframeTimeCommand(
				to_key(keyframe),
				olive::core::Rational(static_cast<int>(time_num),
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

int oaknode_keyframe_get_value(const OakNodeKeyframe *keyframe,
							   oaknode_value *out)
{
	if (!keyframe || !out) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeKeyframe *key = to_key(keyframe);
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

int oaknode_keyframe_set_value(OakNodeKeyframe *keyframe,
							   const oaknode_value *v)
{
	if (!keyframe || !v) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Variant variant;
		if (!oaknode_c_api::variant_from_value(v, &variant)) {
			return OAKNODE_E_INVALID;
		}
		to_key(keyframe)->set_value(variant);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_value_undoable(OakNodeKeyframe *keyframe,
										const oaknode_value *v,
										OakUndoCommand *out_command)
{
	if (!keyframe || !v || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Variant variant;
		if (!oaknode_c_api::variant_from_value(v, &variant)) {
			return OAKNODE_E_INVALID;
		}

		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeParamSetKeyframeValueCommand(to_key(keyframe),
														variant));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_value_string(const OakNodeKeyframe *keyframe,
									  char *buf, int buf_size)
{
	if (!keyframe) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(to_key(keyframe)->value().to_string(),
										  buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_value_string(OakNodeKeyframe *keyframe,
									  const char *value)
{
	if (!keyframe || !value) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_key(keyframe)->set_value(olive::Variant(value));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_value_string_undoable(OakNodeKeyframe *keyframe,
											   const char *value,
											   OakUndoCommand *out_command)
{
	if (!keyframe || !value || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new olive::NodeParamSetKeyframeValueCommand(
				to_key(keyframe), olive::Variant(value)));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_type(const OakNodeKeyframe *keyframe, int *out_type)
{
	if (!keyframe || !out_type) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_type = static_cast<int>(to_key(keyframe)->type());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_type(OakNodeKeyframe *keyframe, int type)
{
	if (!keyframe) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeKeyframe::Type keyframe_type;
		if (!keyframe_type_from_oak(type, &keyframe_type)) {
			return OAKNODE_E_INVALID;
		}
		to_key(keyframe)->set_type(keyframe_type);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_type_undoable(OakNodeKeyframe *keyframe, int type,
									   OakUndoCommand *out_command)
{
	if (!keyframe || !out_command) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::NodeKeyframe::Type keyframe_type;
		if (!keyframe_type_from_oak(type, &keyframe_type)) {
			return OAKNODE_E_INVALID;
		}

		OakUndoCommand handle = oaknode_c_api::wrap_command(
			new KeyframeSetTypeCommand(to_key(keyframe), keyframe_type));
		if (!handle.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_bezier_control(const OakNodeKeyframe *keyframe,
										int handle, double *out_x,
										double *out_y)
{
	if (!keyframe || !out_x || !out_y) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::PointF point;
		if (handle == OAKNODE_KEYFRAME_IN_HANDLE) {
			point = to_key(keyframe)->bezier_control_in();
		} else if (handle == OAKNODE_KEYFRAME_OUT_HANDLE) {
			point = to_key(keyframe)->bezier_control_out();
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

int oaknode_keyframe_set_bezier_control(OakNodeKeyframe *keyframe, int handle,
										double x, double y)
{
	if (!keyframe) {
		return OAKNODE_E_INVALID;
	}

	try {
		if (handle == OAKNODE_KEYFRAME_IN_HANDLE) {
			to_key(keyframe)->set_bezier_control_in(olive::PointF(x, y));
		} else if (handle == OAKNODE_KEYFRAME_OUT_HANDLE) {
			to_key(keyframe)->set_bezier_control_out(olive::PointF(x, y));
		} else {
			return OAKNODE_E_INVALID;
		}
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_set_bezier_control_undoable(OakNodeKeyframe *keyframe,
												 int handle, double x, double y,
												 OakUndoCommand *out_command)
{
	if (!keyframe || !out_command) {
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

		OakUndoCommand handle_ptr = oaknode_c_api::wrap_command(
			new KeyframeSetBezierControlCommand(to_key(keyframe), bezier_handle,
												olive::PointF(x, y)));
		if (!handle_ptr.ctx) {
			return OAKNODE_E_NOMEM;
		}
		*out_command = handle_ptr;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_track(const OakNodeKeyframe *keyframe,
							   int *out_track)
{
	if (!keyframe || !out_track) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_track = to_key(keyframe)->track();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_element(const OakNodeKeyframe *keyframe,
								 int *out_element)
{
	if (!keyframe || !out_element) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_element = to_key(keyframe)->element();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_input(const OakNodeKeyframe *keyframe, char *buf,
							   int buf_size)
{
	if (!keyframe) {
		return OAKNODE_E_INVALID;
	}

	try {
		return oaknode_c_api::copy_string(to_key(keyframe)->input(), buf,
										  buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_keyframe_get_parent(const OakNodeKeyframe *keyframe,
								OakNodeNode **out_node)
{
	if (!keyframe || !out_node) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_node = from_node(to_key(keyframe)->parent());
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
