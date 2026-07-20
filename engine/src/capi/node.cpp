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

#include "oakengine/node.h"

#include <cstdio>
#include <cstring>

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include "coreengine.h"
#include "node/factory.h"
#include "node/keyframe.h"
#include "node/node.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "node/value.h"
#include "undo/undocommand.h"
#include "undo/undostack.h"

namespace
{

// Last node error per thread.
thread_local QString g_last_error;

void set_error(const QString &error)
{
	g_last_error = error;
}

olive::Node *impl(OakEngineNode *h)
{
	return reinterpret_cast<olive::Node *>(h);
}

const olive::Node *impl(const OakEngineNode *h)
{
	return reinterpret_cast<const olive::Node *>(h);
}

OakEngineNode *wrap(olive::Node *n)
{
	return reinterpret_cast<OakEngineNode *>(n);
}

olive::Project *impl(OakEngineProject *h)
{
	return reinterpret_cast<olive::Project *>(h);
}

const olive::Project *impl(const OakEngineProject *h)
{
	return reinterpret_cast<const olive::Project *>(h);
}

// buf/size convention: returns the would-be length excluding the NUL.
int string_to_buf(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf = s.toUtf8();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", utf.constData());
	}
	return int(utf.size());
}

// Push an undoable command onto the global undo stack when the engine is
// initialized, otherwise execute it directly.
void push_or_run(olive::UndoCommand *command, const QString &name)
{
	if (olive::EngineCore::instance()) {
		olive::EngineCore::instance()->undo_stack()->push(command, name);
	} else {
		command->redo_now();
		delete command;
	}
}

// NodeValue::Type -> facade value type; types without a POD representation
// map to OAK_NODE_VALUE_NONE.
oak_node_value_type to_c_type(olive::NodeValue::Type t)
{
	switch (t) {
	case olive::NodeValue::k_int:
		return OAK_NODE_VALUE_INT;
	case olive::NodeValue::k_float:
		return OAK_NODE_VALUE_FLOAT;
	case olive::NodeValue::k_boolean:
		return OAK_NODE_VALUE_BOOL;
	case olive::NodeValue::k_rational:
		return OAK_NODE_VALUE_RATIONAL;
	case olive::NodeValue::k_color:
		return OAK_NODE_VALUE_COLOR;
	case olive::NodeValue::k_vec2:
		return OAK_NODE_VALUE_VEC2;
	case olive::NodeValue::k_vec3:
		return OAK_NODE_VALUE_VEC3;
	case olive::NodeValue::k_vec4:
		return OAK_NODE_VALUE_VEC4;
	case olive::NodeValue::k_combo:
		return OAK_NODE_VALUE_COMBO;
	case olive::NodeValue::k_file:
		return OAK_NODE_VALUE_STRING;
	default:
		return OAK_NODE_VALUE_NONE;
	}
}

// Map an engine standard value into the POD. Returns false when the type
// has no POD representation (including STRING, which uses dedicated APIs).
bool value_to_c(const olive::NodeValue &v, oak_node_value *out)
{
	memset(out, 0, sizeof(*out));
	out->type = to_c_type(v.type());
	switch (v.type()) {
	case olive::NodeValue::k_int:
	case olive::NodeValue::k_combo:
		out->num = v.to_int();
		return true;
	case olive::NodeValue::k_float:
		out->f[0] = v.to_double();
		return true;
	case olive::NodeValue::k_boolean:
		out->num = v.to_bool() ? 1 : 0;
		return true;
	case olive::NodeValue::k_rational: {
		const olive::Rational r = v.to_rational();
		out->num = r.numerator();
		out->den = r.denominator();
		return true;
	}
	case olive::NodeValue::k_color: {
		const olive::Color c = v.to_color();
		out->f[0] = c.red();
		out->f[1] = c.green();
		out->f[2] = c.blue();
		out->f[3] = c.alpha();
		return true;
	}
	case olive::NodeValue::k_vec2: {
		const QVector2D c = v.to_vec2();
		out->f[0] = c.x();
		out->f[1] = c.y();
		return true;
	}
	case olive::NodeValue::k_vec3: {
		const QVector3D c = v.to_vec3();
		out->f[0] = c.x();
		out->f[1] = c.y();
		out->f[2] = c.z();
		return true;
	}
	case olive::NodeValue::k_vec4: {
		const QVector4D c = v.to_vec4();
		out->f[0] = c.x();
		out->f[1] = c.y();
		out->f[2] = c.z();
		out->f[3] = c.w();
		return true;
	}
	default:
		return false;
	}
}

// Map a POD back into an engine QVariant, checking the type against the
// input's declared type. Returns false on a type mismatch.
bool value_from_c(const oak_node_value *v, olive::NodeValue::Type declared,
				  QVariant *out)
{
	if (int(v->type) != int(to_c_type(declared))) {
		return false;
	}
	switch (declared) {
	case olive::NodeValue::k_int:
	case olive::NodeValue::k_combo:
		*out = QVariant::fromValue<qlonglong>(v->num);
		return true;
	case olive::NodeValue::k_float:
		*out = QVariant::fromValue(v->f[0]);
		return true;
	case olive::NodeValue::k_boolean:
		*out = QVariant::fromValue(v->num != 0);
		return true;
	case olive::NodeValue::k_rational:
		*out = QVariant::fromValue(
			olive::Rational(int(v->num), int(v->den)));
		return true;
	case olive::NodeValue::k_color:
		*out = QVariant::fromValue(olive::Color(
			float(v->f[0]), float(v->f[1]), float(v->f[2]), float(v->f[3])));
		return true;
	case olive::NodeValue::k_vec2:
		*out = QVariant::fromValue(
			QVector2D(float(v->f[0]), float(v->f[1])));
		return true;
	case olive::NodeValue::k_vec3:
		*out = QVariant::fromValue(
			QVector3D(float(v->f[0]), float(v->f[1]), float(v->f[2])));
		return true;
	case olive::NodeValue::k_vec4:
		*out = QVariant::fromValue(QVector4D(
			float(v->f[0]), float(v->f[1]), float(v->f[2]), float(v->f[3])));
		return true;
	default:
		return false;
	}
}

// Validate self + input id and return the declared type; reports the error.
olive::NodeValue::Type checked_input(const olive::Node *self,
									 const char *input_id)
{
	if (!self || !input_id) {
		return olive::NodeValue::k_none;
	}
	const QString id = QString::fromUtf8(input_id);
	if (!self->inputs().contains(id)) {
		return olive::NodeValue::k_none;
	}
	return self->get_input_data_type(id);
}

/* ---- Keyframe helpers ------------------------------------------------------ */

// facade easing type <-> engine NodeKeyframe::Type (different orders).
olive::NodeKeyframe::Type to_engine_easing(int type)
{
	switch (type) {
	case 1:
		return olive::NodeKeyframe::k_bezier;
	case 2:
		return olive::NodeKeyframe::k_hold;
	default:
		return olive::NodeKeyframe::k_linear;
	}
}

int from_engine_easing(olive::NodeKeyframe::Type type)
{
	switch (type) {
	case olive::NodeKeyframe::k_bezier:
		return 1;
	case olive::NodeKeyframe::k_hold:
		return 2;
	default:
		return 0;
	}
}

// Frame-timestamp timebase for keyframes: the frame rate of the project's
// first sequence, or the engine default (1001/30000 s per frame).
olive::Rational project_time_base(const olive::Node *node)
{
	if (const olive::Project *p = olive::Project::get_project_from_object(node)) {
		for (olive::Node *n : p->nodes()) {
			if (const olive::Sequence *s = dynamic_cast<olive::Sequence *>(n)) {
				const olive::Rational fr = s->get_video_params().frame_rate();
				if (!fr.isNull() && !fr.isNaN()) {
					return fr.flipped();
				}
			}
		}
	}
	return olive::Rational(1001, 30000);
}

int64_t kf_time_to_ts(const olive::Rational &time, const olive::Rational &tb)
{
	return olive::core::Timecode::time_to_timestamp(
		time, tb, olive::core::Timecode::k_round);
}

// Map a track-0 keyframe component value into the POD. For split-track
// types the component lands in f[0] (COLOR/VEC) or num; single-track types
// map fully. Returns false for unmappable types.
bool kf_value_to_c(olive::NodeValue::Type type, const QVariant &component,
				   oak_node_value *out)
{
	memset(out, 0, sizeof(*out));
	switch (type) {
	case olive::NodeValue::k_int:
	case olive::NodeValue::k_combo:
		out->type = OAK_NODE_VALUE_INT;
		out->num = component.toLongLong();
		return true;
	case olive::NodeValue::k_float:
		out->type = OAK_NODE_VALUE_FLOAT;
		out->f[0] = component.toDouble();
		return true;
	case olive::NodeValue::k_boolean:
		out->type = OAK_NODE_VALUE_BOOL;
		out->num = component.toBool() ? 1 : 0;
		return true;
	case olive::NodeValue::k_rational: {
		const olive::Rational r = component.value<olive::Rational>();
		out->type = OAK_NODE_VALUE_RATIONAL;
		out->num = r.numerator();
		out->den = r.denominator();
		return true;
	}
	case olive::NodeValue::k_color:
		out->type = OAK_NODE_VALUE_COLOR;
		out->f[0] = component.toFloat();
		return true;
	case olive::NodeValue::k_vec2:
		out->type = OAK_NODE_VALUE_VEC2;
		out->f[0] = component.toFloat();
		return true;
	case olive::NodeValue::k_vec3:
		out->type = OAK_NODE_VALUE_VEC3;
		out->f[0] = component.toFloat();
		return true;
	case olive::NodeValue::k_vec4:
		out->type = OAK_NODE_VALUE_VEC4;
		out->f[0] = component.toFloat();
		return true;
	case olive::NodeValue::k_file:
		out->type = OAK_NODE_VALUE_STRING;
		return true;
	default:
		return false;
	}
}

// Undo commands for easing changes. The engine's undo stack has no
// keyframe type/bezier commands (the application layer used to carry
// them in app/widget/keyframeviewundo.h, since migrated here), so the
// facade carries minimal equivalents with the same old/new semantics.
class KeyframeSetTypeCommand : public olive::UndoCommand {
public:
	KeyframeSetTypeCommand(olive::NodeKeyframe *key,
						   olive::NodeKeyframe::Type type)
		: key_(key)
		, new_type_(type)
	{
	}

	virtual olive::Project *get_relevant_project() const override
	{
		return key_->parent() ? key_->parent()->project() : nullptr;
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
	olive::NodeKeyframe::Type old_type_ = olive::NodeKeyframe::k_linear;
	olive::NodeKeyframe::Type new_type_;
};

class KeyframeSetBezierPointCommand : public olive::UndoCommand {
public:
	KeyframeSetBezierPointCommand(olive::NodeKeyframe *key,
								  olive::NodeKeyframe::BezierType mode,
								  const QPointF &point)
		: key_(key)
		, mode_(mode)
		, new_point_(point)
	{
	}

	// Explicit old point for callers that already live-set the new one
	// (mirrors the application class's second constructor).
	KeyframeSetBezierPointCommand(olive::NodeKeyframe *key,
								  olive::NodeKeyframe::BezierType mode,
								  const QPointF &new_point,
								  const QPointF &old_point)
		: key_(key)
		, mode_(mode)
		, has_old_(true)
		, old_point_(old_point)
		, new_point_(new_point)
	{
	}

	virtual olive::Project *get_relevant_project() const override
	{
		return key_->parent() ? key_->parent()->project() : nullptr;
	}

protected:
	virtual void redo() override
	{
		if (!has_old_) {
			old_point_ = key_->bezier_control(mode_);
			has_old_ = true;
		}
		key_->set_bezier_control(mode_, new_point_);
	}

	virtual void undo() override
	{
		key_->set_bezier_control(mode_, old_point_);
	}

private:
	olive::NodeKeyframe *key_;
	olive::NodeKeyframe::BezierType mode_;
	bool has_old_ = false;
	QPointF old_point_;
	QPointF new_point_;
};

// Validate a keyframing target: node + known input id + mappable type.
// Returns the declared type (k_none on failure; error reported).
olive::NodeValue::Type checked_keyframe_input(const olive::Node *self,
											  const char *input_id)
{
	const olive::NodeValue::Type type = checked_input(self, input_id);
	if (!self || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return olive::NodeValue::k_none;
	}
	if (type == olive::NodeValue::k_none) {
		set_error(QStringLiteral("unknown input id \"%1\"")
					  .arg(QString::fromUtf8(input_id)));
		return olive::NodeValue::k_none;
	}
	return type;
}

// Time-exact keyframe lookup that does not depend on the input's
// keyframing-enabled flag (Node::get_keyframe_at_time_on_track reports
// nothing when keyframing is off, but the application's keyframe editing
// operates on the keyframe objects regardless of the flag).
olive::NodeKeyframe *find_keyframe(const olive::Node *node,
								   const olive::NodeInput &input,
								   const olive::Rational &time, int track)
{
	const QVector<olive::NodeKeyframeTrack> &tracks =
		node->get_keyframe_tracks(input.input(), input.element());
	if (track < 0 || track >= tracks.size()) {
		return nullptr;
	}
	for (olive::NodeKeyframe *key : tracks.at(track)) {
		if (key->time() == time) {
			return key;
		}
	}
	return nullptr;
}

} // namespace

extern "C"
{

int oakengine_node_last_error(char *buf, int buf_size)
{
	return string_to_buf(g_last_error, buf, buf_size);
}

int oakengine_project_node_count(const OakEngineProject *self)
{
	return self ? impl(self)->nodes().size() : 0;
}

OakEngineNode *oakengine_project_node_at(const OakEngineProject *self,
										 int index)
{
	if (!self || index < 0 || index >= impl(self)->nodes().size()) {
		return nullptr;
	}
	return wrap(impl(self)->nodes().at(index));
}

int oakengine_node_get_type_id(const OakEngineNode *self, char *buf,
							   int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->id(), buf, buf_size);
}

int oakengine_node_get_name(const OakEngineNode *self, char *buf,
							int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->name(), buf, buf_size);
}

int oakengine_node_get_label(const OakEngineNode *self, char *buf,
							 int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->get_label(), buf, buf_size);
}

int oakengine_node_set_label(OakEngineNode *self, const char *label)
{
	return oakengine_node_set_label_ex(self, label, 1);
}

int oakengine_node_set_label_ex(OakEngineNode *self, const char *label,
								int undoable)
{
	set_error(QString());
	if (!self) {
		set_error(QStringLiteral("invalid node"));
		return OAKENGINE_E_INVALID;
	}
	olive::UndoCommand *command = new olive::NodeRenameCommand(
		impl(self), QString::fromUtf8(label ? label : ""));
	if (undoable) {
		push_or_run(command, QStringLiteral("Rename Node"));
	} else {
		command->redo_now();
		delete command;
	}
	return OAKENGINE_OK;
}

int oakengine_node_input_count(const OakEngineNode *self)
{
	return self ? impl(self)->inputs().size() : 0;
}

int oakengine_node_input_id(const OakEngineNode *self, int index, char *buf,
							int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const QVector<QString> &ids = impl(self)->inputs();
	if (index < 0 || index >= ids.size()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	return string_to_buf(ids.at(index), buf, buf_size);
}

int oakengine_node_input_get_type(const OakEngineNode *self,
								  const char *input_id)
{
	if (!self || !input_id) {
		return OAK_NODE_VALUE_NONE;
	}
	return to_c_type(checked_input(impl(self), input_id));
}

int oakengine_node_input_is_connected(const OakEngineNode *self,
									  const char *input_id)
{
	if (!self || !input_id) {
		return 0;
	}
	return impl(self)->is_input_connected(QString::fromUtf8(input_id)) ? 1 :
																		 0;
}

int oakengine_node_get_input(const OakEngineNode *self, const char *input_id,
							 oak_node_value *out)
{
	set_error(QString());
	if (!self || !input_id || !out) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->inputs().contains(id)) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::NodeValue::Type type = node->get_input_data_type(id);
	if (type == olive::NodeValue::k_file) {
		set_error(QStringLiteral(
			"\"%1\" is a string input; use oakengine_node_get_input_string()")
					  .arg(id));
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeValue v(type, node->get_standard_value(id));
	if (!value_to_c(v, out)) {
		set_error(QStringLiteral(
			"input \"%1\" has no POD value representation")
					  .arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	return OAKENGINE_OK;
}

int oakengine_node_set_input(OakEngineNode *self, const char *input_id,
							 const oak_node_value *v)
{
	set_error(QString());
	if (!self || !input_id || !v) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->inputs().contains(id)) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::NodeValue::Type type = node->get_input_data_type(id);
	if (type == olive::NodeValue::k_file) {
		set_error(QStringLiteral(
			"\"%1\" is a string input; use oakengine_node_set_input_string()")
					  .arg(id));
		return OAKENGINE_E_INVALID;
	}
	QVariant value;
	if (!value_from_c(v, type, &value)) {
		set_error(QStringLiteral("value type %1 does not match the declared "
								 "type of \"%2\"")
					  .arg(v->type)
					  .arg(id));
		return OAKENGINE_E_INVALID;
	}
	// The undo stack's set-value commands work on split (per-component
	// track) values; split_normal_value_into_track_values() is the same
	// conversion Node::set_standard_value() performs.
	push_or_run(new olive::NodeParamSetSplitStandardValueCommand(
					olive::NodeInput(node, id),
					olive::NodeValue::split_normal_value_into_track_values(
						type, value)),
				QStringLiteral("Set Node Value"));
	return OAKENGINE_OK;
}

int oakengine_node_get_input_string(const OakEngineNode *self,
									const char *input_id, char *buf,
									int buf_size)
{
	set_error(QString());
	if (!self || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->inputs().contains(id)) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (node->get_input_data_type(id) != olive::NodeValue::k_file) {
		set_error(QStringLiteral("\"%1\" is not a string input").arg(id));
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(node->get_standard_value(id).toString(), buf,
						 buf_size);
}

int oakengine_node_set_input_string(OakEngineNode *self,
									const char *input_id, const char *s)
{
	set_error(QString());
	if (!self || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->inputs().contains(id)) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (node->get_input_data_type(id) != olive::NodeValue::k_file) {
		set_error(QStringLiteral("\"%1\" is not a string input").arg(id));
		return OAKENGINE_E_INVALID;
	}
	const QVariant value = QVariant::fromValue(QString::fromUtf8(s ? s : ""));
	push_or_run(new olive::NodeParamSetSplitStandardValueCommand(
					olive::NodeInput(node, id),
					olive::NodeValue::split_normal_value_into_track_values(
						olive::NodeValue::k_file, value)),
				QStringLiteral("Set Node Value"));
	return OAKENGINE_OK;
}

int oakengine_node_frame_time_base(const OakEngineNode *self, int *num,
								   int *den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Rational tb = project_time_base(impl(self));
	if (num) {
		*num = tb.numerator();
	}
	if (den) {
		*den = tb.denominator();
	}
	return OAKENGINE_OK;
}

// Component QVariant of a per-track POD for set_value_at_time: the
// panel's sliders carry one scalar per track (int64/double/Rational/
// bool). Returns false on a type that has no scalar component here.
static bool component_from_c(const oak_node_value *v,
							 olive::NodeValue::Type declared, int component,
							 QVariant *out)
{
	switch (declared) {
	case olive::NodeValue::k_int:
	case olive::NodeValue::k_combo:
		if (v->type != OAK_NODE_VALUE_INT &&
			v->type != OAK_NODE_VALUE_COMBO) {
			return false;
		}
		*out = QVariant::fromValue<qlonglong>(v->num);
		return true;
	case olive::NodeValue::k_float:
	case olive::NodeValue::k_bezier:
		if (v->type != OAK_NODE_VALUE_FLOAT) {
			return false;
		}
		*out = QVariant::fromValue(v->f[0]);
		return true;
	case olive::NodeValue::k_boolean:
		if (v->type != OAK_NODE_VALUE_BOOL) {
			return false;
		}
		*out = QVariant::fromValue(v->num != 0);
		return true;
	case olive::NodeValue::k_rational:
		if (v->type != OAK_NODE_VALUE_RATIONAL) {
			return false;
		}
		*out = QVariant::fromValue(
			olive::Rational(int(v->num), int(v->den)));
		return true;
	case olive::NodeValue::k_color:
	case olive::NodeValue::k_vec2:
	case olive::NodeValue::k_vec3:
	case olive::NodeValue::k_vec4:
		if (int(v->type) != int(to_c_type(declared))) {
			return false;
		}
		*out = QVariant::fromValue(v->f[component]);
		return true;
	default:
		return false;
	}
}

int oakengine_node_set_input_at_time(OakEngineNode *self,
									 const char *input_id, int element,
									 int64_t time_ts, int track,
									 const oak_node_value *v,
									 int insert_on_all_tracks)
{
	set_error(QString());
	olive::Node *node = impl(self);
	const olive::NodeValue::Type declared = checked_input(node, input_id);
	if (declared == olive::NodeValue::k_none) {
		set_error(self && input_id ?
					  QStringLiteral("unknown input id \"%1\"")
						  .arg(QString::fromUtf8(input_id)) :
					  QStringLiteral("invalid arguments"));
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	if (declared == olive::NodeValue::k_file ||
		declared == olive::NodeValue::k_text ||
		declared == olive::NodeValue::k_font ||
		declared == olive::NodeValue::k_str_combo) {
		set_error(QStringLiteral(
			"string inputs use oakengine_node_set_input_string_at_time"));
		return OAKENGINE_E_INVALID;
	}
	const int nb_tracks =
		olive::NodeValue::get_number_of_keyframe_tracks(declared);
	if (!v || track < -1 || track >= nb_tracks || nb_tracks == 0) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}

	const QString id = QString::fromUtf8(input_id);
	const olive::Rational time = olive::core::Timecode::timestamp_to_time(
		time_ts, project_time_base(node));
	const olive::NodeInput input(node, id, element);

	// The panel's commit path (Node::set_value_at_time), one undoable
	// command: keyframed inputs insert/update the keyframe at the time,
	// others set the standard value on the track.
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	if (track == -1) {
		for (int i = 0; i < nb_tracks; i++) {
			QVariant component;
			if (!component_from_c(v, declared, i, &component)) {
				set_error(QStringLiteral(
					"value type does not match the declared input type"));
				delete command;
				return OAKENGINE_E_INVALID;
			}
			olive::Node::set_value_at_time(input, time, component, i,
										   command, false);
		}
	} else {
		QVariant component;
		if (!component_from_c(v, declared, 0, &component)) {
			set_error(QStringLiteral(
				"value type does not match the declared input type"));
			delete command;
			return OAKENGINE_E_INVALID;
		}
		olive::Node::set_value_at_time(input, time, component, track,
									   command, insert_on_all_tracks != 0);
	}
	push_or_run(command, QStringLiteral("Set Input Value"));
	return OAKENGINE_OK;
}

int oakengine_node_set_input_string_at_time(OakEngineNode *self,
											const char *input_id, int element,
											int64_t time_ts, const char *value)
{
	set_error(QString());
	olive::Node *node = impl(self);
	const olive::NodeValue::Type declared = checked_input(node, input_id);
	if (declared == olive::NodeValue::k_none) {
		set_error(self && input_id ?
					  QStringLiteral("unknown input id \"%1\"")
						  .arg(QString::fromUtf8(input_id)) :
					  QStringLiteral("invalid arguments"));
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	if (declared != olive::NodeValue::k_file &&
		declared != olive::NodeValue::k_text &&
		declared != olive::NodeValue::k_font &&
		declared != olive::NodeValue::k_str_combo) {
		set_error(QStringLiteral("\"%1\" is not a string input")
					  .arg(QString::fromUtf8(input_id)));
		return OAKENGINE_E_INVALID;
	}
	const olive::Rational time = olive::core::Timecode::timestamp_to_time(
		time_ts, project_time_base(node));
	const QVariant v =
		QVariant::fromValue(QString::fromUtf8(value ? value : ""));
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	olive::Node::set_value_at_time(
		olive::NodeInput(node, QString::fromUtf8(input_id), element), time, v,
		0, command, true);
	push_or_run(command, QStringLiteral("Set Input Value"));
	return OAKENGINE_OK;
}

int oakengine_node_array_insert_at(OakEngineNode *self, const char *input_id,
								   int index)
{
	set_error(QString());
	olive::Node *node = impl(self);
	if (checked_input(node, input_id) == olive::NodeValue::k_none ||
		index < 0) {
		set_error(self && input_id && index >= 0 ?
					  QStringLiteral("unknown input id \"%1\"")
						  .arg(QString::fromUtf8(input_id)) :
					  QStringLiteral("invalid arguments"));
		return self && input_id && index >= 0 ? OAKENGINE_E_NOT_FOUND :
												OAKENGINE_E_INVALID;
	}
	push_or_run(new olive::NodeArrayInsertCommand(
					node, QString::fromUtf8(input_id), index),
				QStringLiteral("Insert Array Element"));
	return OAKENGINE_OK;
}

int oakengine_node_array_remove_at(OakEngineNode *self, const char *input_id,
								   int index)
{
	set_error(QString());
	olive::Node *node = impl(self);
	if (checked_input(node, input_id) == olive::NodeValue::k_none ||
		index < 0) {
		set_error(self && input_id && index >= 0 ?
					  QStringLiteral("unknown input id \"%1\"")
						  .arg(QString::fromUtf8(input_id)) :
					  QStringLiteral("invalid arguments"));
		return self && input_id && index >= 0 ? OAKENGINE_E_NOT_FOUND :
												OAKENGINE_E_INVALID;
	}
	const QString id = QString::fromUtf8(input_id);
	const int size = olive::NodeInput(node, id).get_array_size();
	if (index >= size) {
		set_error(QStringLiteral("array index %1 out of range (size %2)")
					  .arg(index)
					  .arg(size));
		return OAKENGINE_E_NOT_FOUND;
	}
	push_or_run(new olive::NodeArrayRemoveCommand(node, id, index),
				QStringLiteral("Remove Array Element"));
	return OAKENGINE_OK;
}


OakEngineNode *oakengine_project_add_node(OakEngineProject *project,
										  const char *type_id)
{
	set_error(QString());
	olive::Project *p = reinterpret_cast<olive::Project *>(project);
	if (!p || !type_id) {
		set_error(QStringLiteral("invalid project or type id"));
		return nullptr;
	}
	const QString id = QString::fromUtf8(type_id);
	olive::Node *node = olive::NodeFactory::create_from_id(id);
	if (!node) {
		set_error(QStringLiteral("unknown node type id \"%1\"").arg(id));
		return nullptr;
	}
	push_or_run(new olive::NodeAddCommand(p, node),
				QStringLiteral("Add Node"));
	return wrap(node);
}

int oakengine_project_remove_node(OakEngineProject *project,
								  OakEngineNode *node)
{
	set_error(QString());
	olive::Project *p = reinterpret_cast<olive::Project *>(project);
	olive::Node *n = impl(node);
	if (!p || !n) {
		set_error(QStringLiteral("invalid project or node"));
		return OAKENGINE_E_INVALID;
	}
	if (olive::Project::get_project_from_object(n) != p) {
		set_error(QStringLiteral("node does not belong to this project"));
		return OAKENGINE_E_INVALID;
	}
	push_or_run(new olive::NodeRemoveAndDisconnectCommand(n),
				QStringLiteral("Remove Node"));
	return OAKENGINE_OK;
}

int oakengine_node_connect(OakEngineNode *output_node,
						   OakEngineNode *input_node, const char *input_id)
{
	set_error(QString());
	olive::Node *out_node = impl(output_node);
	olive::Node *in_node = impl(input_node);
	if (!out_node || !in_node || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const QString id = QString::fromUtf8(input_id);
	if (!in_node->inputs().contains(id)) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (!in_node->is_input_connectable(id)) {
		set_error(QStringLiteral("input \"%1\" is not connectable").arg(id));
		return OAKENGINE_E_INVALID;
	}
	if (in_node->is_input_connected(id)) {
		set_error(QStringLiteral(
			"input \"%1\" is already connected; disconnect first")
					  .arg(id));
		return OAKENGINE_E_STATE;
	}
	push_or_run(new olive::NodeEdgeAddCommand(
					out_node, olive::NodeInput(in_node, id)),
				QStringLiteral("Connect Nodes"));
	return OAKENGINE_OK;
}

int oakengine_node_disconnect(OakEngineNode *input_node, const char *input_id)
{
	return oakengine_node_disconnect_ex(input_node, input_id, -1);
}

int oakengine_node_disconnect_ex(OakEngineNode *input_node,
								 const char *input_id, int element)
{
	set_error(QString());
	olive::Node *in_node = impl(input_node);
	if (!in_node || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const QString id = QString::fromUtf8(input_id);
	if (!in_node->inputs().contains(id)) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::NodeInput input(in_node, id, element);
	olive::Node *connected = in_node->get_connected_output(input);
	if (!connected) {
		set_error(QStringLiteral("input \"%1\" is not connected").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	push_or_run(new olive::NodeEdgeRemoveCommand(connected, input),
				QStringLiteral("Disconnect Nodes"));
	return OAKENGINE_OK;
}

/* ---- Parameter animation (keyframes) -------------------------------------- */

int oakengine_node_input_is_keyframed(const OakEngineNode *self,
									  const char *input_id)
{
	if (!self || !input_id) {
		return 0;
	}
	return impl(self)->is_input_keyframing(QString::fromUtf8(input_id)) ? 1 :
																		  0;
}

int oakengine_node_keyframe_count(const OakEngineNode *self,
								  const char *input_id)
{
	if (!self || !input_id) {
		return 0;
	}
	const QVector<olive::NodeKeyframeTrack> &tracks =
		impl(self)->get_keyframe_tracks(QString::fromUtf8(input_id), -1);
	return tracks.isEmpty() ? 0 : tracks.first().size();
}

int oakengine_node_keyframe_at(const OakEngineNode *self,
							   const char *input_id, int index,
							   int64_t *time_ts, oak_node_value *value)
{
	set_error(QString());
	const olive::NodeValue::Type type = checked_keyframe_input(impl(self), input_id);
	if (type == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	const QVector<olive::NodeKeyframeTrack> &tracks =
		impl(self)->get_keyframe_tracks(QString::fromUtf8(input_id), -1);
	if (tracks.isEmpty() || index < 0 || index >= tracks.first().size()) {
		set_error(QStringLiteral("no keyframe at index %1").arg(index));
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::NodeKeyframe *key = tracks.first().at(index);
	if (time_ts) {
		*time_ts = kf_time_to_ts(key->time(), project_time_base(impl(self)));
	}
	if (value && !kf_value_to_c(type, key->value(), value)) {
		set_error(QStringLiteral(
			"input \"%1\" has no POD keyframe representation")
					  .arg(QString::fromUtf8(input_id)));
		return OAKENGINE_E_NOT_FOUND;
	}
	return OAKENGINE_OK;
}

int oakengine_node_keyframe_get_easing(const OakEngineNode *self,
									   const char *input_id, int index,
									   float *x1, float *y1, float *x2,
									   float *y2, int *type)
{
	set_error(QString());
	if (checked_keyframe_input(impl(self), input_id) == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	const QVector<olive::NodeKeyframeTrack> &tracks =
		impl(self)->get_keyframe_tracks(QString::fromUtf8(input_id), -1);
	if (tracks.isEmpty() || index < 0 || index >= tracks.first().size()) {
		set_error(QStringLiteral("no keyframe at index %1").arg(index));
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::NodeKeyframe *key = tracks.first().at(index);
	if (type) {
		*type = from_engine_easing(key->type());
	}
	const bool bezier = key->type() == olive::NodeKeyframe::k_bezier;
	const QPointF in =
		bezier ? key->bezier_control_in() : QPointF(0, 0);
	const QPointF out =
		bezier ? key->bezier_control_out() : QPointF(0, 0);
	if (x1) {
		*x1 = float(in.x());
	}
	if (y1) {
		*y1 = float(in.y());
	}
	if (x2) {
		*x2 = float(out.x());
	}
	if (y2) {
		*y2 = float(out.y());
	}
	return OAKENGINE_OK;
}

int oakengine_node_keyframe_add(OakEngineNode *self, const char *input_id,
								int64_t time_ts, const oak_node_value *value,
								int type, float x1, float y1, float x2,
								float y2)
{
	set_error(QString());
	olive::Node *node = impl(self);
	const olive::NodeValue::Type declared =
		checked_keyframe_input(node, input_id);
	if (declared == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	if (!value || type < 0 || type > 2) {
		set_error(QStringLiteral("invalid arguments or easing type"));
		return OAKENGINE_E_INVALID;
	}
	const QString id = QString::fromUtf8(input_id);
	const olive::Rational tb = project_time_base(node);
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);

	if (node->get_keyframe_at_time_on_track(id, time, 0)) {
		set_error(QStringLiteral(
			"a keyframe already exists at time %1 on \"%2\"")
					  .arg(time_ts)
					  .arg(id));
		return OAKENGINE_E_STATE;
	}

	// Map the POD to an engine value, then split it like
	// Node::set_standard_value() does; track 0 takes the first component.
	QVariant normal;
	if (!value_from_c(value, declared, &normal)) {
		set_error(QStringLiteral("value type %1 does not match the declared "
								 "type of \"%2\"")
					  .arg(value->type)
					  .arg(id));
		return OAKENGINE_E_INVALID;
	}
	const olive::SplitValue split =
		olive::NodeValue::split_normal_value_into_track_values(declared,
															   normal);
	if (split.isEmpty()) {
		set_error(QStringLiteral(
			"input \"%1\" has no keyframable representation")
					  .arg(id));
		return OAKENGINE_E_INVALID;
	}

	auto *key = new olive::NodeKeyframe(time, split.first(),
										to_engine_easing(type), 0, -1, id);
	if (type == 1) {
		key->set_bezier_control_in(QPointF(x1, y1));
		key->set_bezier_control_out(QPointF(x2, y2));
	}

	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	if (!node->is_input_keyframing(id)) {
		command->add_child(new olive::NodeParamSetKeyframingCommand(
			olive::NodeInput(node, id), true));
	}
	command->add_child(new olive::NodeParamInsertKeyframeCommand(node, key));
	push_or_run(command, QStringLiteral("Add Keyframe"));
	return OAKENGINE_OK;
}

int oakengine_node_keyframe_remove(OakEngineNode *self, const char *input_id,
								   int64_t time_ts)
{
	set_error(QString());
	olive::Node *node = impl(self);
	if (checked_keyframe_input(node, input_id) == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	const QString id = QString::fromUtf8(input_id);
	const olive::Rational time = olive::core::Timecode::timestamp_to_time(
		time_ts, project_time_base(node));
	olive::NodeKeyframe *key =
		node->get_keyframe_at_time_on_track(id, time, 0);
	if (!key) {
		set_error(QStringLiteral("no keyframe at time %1 on \"%2\"")
					  .arg(time_ts)
					  .arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	push_or_run(new olive::NodeParamRemoveKeyframeCommand(key),
				QStringLiteral("Remove Keyframe"));
	return OAKENGINE_OK;
}

int oakengine_node_keyframe_set_easing(OakEngineNode *self,
									   const char *input_id, int64_t time_ts,
									   int type, float x1, float y1,
									   float x2, float y2)
{
	set_error(QString());
	olive::Node *node = impl(self);
	if (checked_keyframe_input(node, input_id) == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	if (type < 0 || type > 2) {
		set_error(QStringLiteral("unknown easing type %1").arg(type));
		return OAKENGINE_E_INVALID;
	}
	const QString id = QString::fromUtf8(input_id);
	const olive::Rational time = olive::core::Timecode::timestamp_to_time(
		time_ts, project_time_base(node));
	olive::NodeKeyframe *key =
		node->get_keyframe_at_time_on_track(id, time, 0);
	if (!key) {
		set_error(QStringLiteral("no keyframe at time %1 on \"%2\"")
					  .arg(time_ts)
					  .arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}

	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	command->add_child(
		new KeyframeSetTypeCommand(key, to_engine_easing(type)));
	if (type == 1) {
		command->add_child(new KeyframeSetBezierPointCommand(
			key, olive::NodeKeyframe::k_in_handle, QPointF(x1, y1)));
		command->add_child(new KeyframeSetBezierPointCommand(
			key, olive::NodeKeyframe::k_out_handle, QPointF(x2, y2)));
	}
	push_or_run(command, QStringLiteral("Set Keyframe Easing"));
	return OAKENGINE_OK;
}

int oakengine_node_keyframes_set_type_many(OakEngineNode *self,
										   const char *input_id, int element,
										   const int64_t *times_ts,
										   const int *tracks, int count,
										   int type)
{
	set_error(QString());
	olive::Node *node = impl(self);
	if (checked_keyframe_input(node, input_id) == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	if (type < 0 || type > 2 || count < 0 || (count > 0 && (!times_ts || !tracks))) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (count == 0) {
		return 0;
	}
	const QString id = QString::fromUtf8(input_id);
	const olive::Rational tb = project_time_base(node);
	const olive::NodeInput input(node, id, element);

	// Resolve every keyframe first so a bad address fails without side
	// effects (same batch semantics as the view's context-menu action).
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	for (int i = 0; i < count; i++) {
		const olive::Rational time =
			olive::core::Timecode::timestamp_to_time(times_ts[i], tb);
		olive::NodeKeyframe *key =
			find_keyframe(node, input, time, tracks[i]);
		if (!key) {
			set_error(QStringLiteral("no keyframe at time %1 track %2 on "
									 "\"%3\"")
						  .arg(times_ts[i])
						  .arg(tracks[i])
						  .arg(id));
			delete command;
			return OAKENGINE_E_NOT_FOUND;
		}
		command->add_child(
			new KeyframeSetTypeCommand(key, to_engine_easing(type)));
	}
	push_or_run(command, QStringLiteral("Set Keyframe Type"));
	return count;
}

int oakengine_node_keyframes_set_time_many(OakEngineNode *self,
										   const char *input_id, int element,
										   const int64_t *old_times_ts,
										   const int *tracks, int count,
										   int64_t new_time_ts)
{
	set_error(QString());
	olive::Node *node = impl(self);
	if (checked_keyframe_input(node, input_id) == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	if (count < 0 || (count > 0 && (!old_times_ts || !tracks)) ||
		new_time_ts < 0) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (count == 0) {
		return 0;
	}
	const QString id = QString::fromUtf8(input_id);
	const olive::Rational tb = project_time_base(node);
	const olive::NodeInput input(node, id, element);
	const olive::Rational new_time =
		olive::core::Timecode::timestamp_to_time(new_time_ts, tb);

	// Resolve and conflict-check every key first so a failure has no side
	// effects.
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	for (int i = 0; i < count; i++) {
		const olive::Rational old_time =
			olive::core::Timecode::timestamp_to_time(old_times_ts[i], tb);
		olive::NodeKeyframe *key =
			find_keyframe(node, input, old_time, tracks[i]);
		if (!key) {
			set_error(QStringLiteral("no keyframe at time %1 track %2 on "
									 "\"%3\"")
						  .arg(old_times_ts[i])
						  .arg(tracks[i])
						  .arg(id));
			delete command;
			return OAKENGINE_E_NOT_FOUND;
		}
		olive::NodeKeyframe *occupant =
			find_keyframe(node, input, new_time, tracks[i]);
		if (occupant && occupant != key) {
			set_error(QStringLiteral("a keyframe already exists at time %1 "
									 "on track %2")
						  .arg(new_time_ts)
						  .arg(tracks[i]));
			delete command;
			return OAKENGINE_E_STATE;
		}
		command->add_child(new olive::NodeParamSetKeyframeTimeCommand(
			key, new_time, key->time()));
	}
	push_or_run(command, QStringLiteral("Set Keyframe Time"));
	return count;
}

int oakengine_node_keyframes_set_value_many(OakEngineNode *self,
											const char *input_id, int element,
											const int64_t *times_ts,
											const int *tracks, int count,
											const oak_node_value *values,
											const oak_node_value *old_values)
{
	set_error(QString());
	olive::Node *node = impl(self);
	const olive::NodeValue::Type declared =
		checked_keyframe_input(node, input_id);
	if (declared == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	if (count < 0 || (count > 0 && (!times_ts || !tracks || !values))) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (count == 0) {
		return 0;
	}
	const QString id = QString::fromUtf8(input_id);
	const olive::Rational tb = project_time_base(node);
	const olive::NodeInput input(node, id, element);

	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	for (int i = 0; i < count; i++) {
		const olive::Rational time =
			olive::core::Timecode::timestamp_to_time(times_ts[i], tb);
		olive::NodeKeyframe *key =
			find_keyframe(node, input, time, tracks[i]);
		if (!key) {
			set_error(QStringLiteral("no keyframe at time %1 track %2 on "
									 "\"%3\"")
						  .arg(times_ts[i])
						  .arg(tracks[i])
						  .arg(id));
			delete command;
			return OAKENGINE_E_NOT_FOUND;
		}
		QVariant new_value;
		if (!component_from_c(&values[i], declared, 0, &new_value)) {
			set_error(QStringLiteral(
				"value type does not match the declared input type"));
			delete command;
			return OAKENGINE_E_INVALID;
		}
		if (old_values) {
			QVariant old_value;
			if (!component_from_c(&old_values[i], declared, 0, &old_value)) {
				set_error(QStringLiteral(
					"old value type does not match the declared input type"));
				delete command;
				return OAKENGINE_E_INVALID;
			}
			command->add_child(new olive::NodeParamSetKeyframeValueCommand(
				key, new_value, old_value));
		} else {
			command->add_child(new olive::NodeParamSetKeyframeValueCommand(
				key, new_value, key->value()));
		}
	}
	push_or_run(command, QStringLiteral("Set Keyframe Value"));
	return count;
}

int oakengine_node_keyframes_set_bezier_many(OakEngineNode *self,
											 const char *input_id,
											 int element,
											 const int64_t *times_ts,
											 const int *tracks, int count,
											 double in_x, double in_y,
											 double out_x, double out_y)
{
	set_error(QString());
	olive::Node *node = impl(self);
	if (checked_keyframe_input(node, input_id) == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	if (count < 0 || (count > 0 && (!times_ts || !tracks))) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (count == 0) {
		return 0;
	}
	const QString id = QString::fromUtf8(input_id);
	const olive::Rational tb = project_time_base(node);
	const olive::NodeInput input(node, id, element);

	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	for (int i = 0; i < count; i++) {
		const olive::Rational time =
			olive::core::Timecode::timestamp_to_time(times_ts[i], tb);
		olive::NodeKeyframe *key =
			find_keyframe(node, input, time, tracks[i]);
		if (!key) {
			set_error(QStringLiteral("no keyframe at time %1 track %2 on "
									 "\"%3\"")
						  .arg(times_ts[i])
						  .arg(tracks[i])
						  .arg(id));
			delete command;
			return OAKENGINE_E_NOT_FOUND;
		}
		command->add_child(new KeyframeSetBezierPointCommand(
			key, olive::NodeKeyframe::k_in_handle, QPointF(in_x, in_y)));
		command->add_child(new KeyframeSetBezierPointCommand(
			key, olive::NodeKeyframe::k_out_handle, QPointF(out_x, out_y)));
	}
	push_or_run(command, QStringLiteral("Set Keyframe Bezier Points"));
	return count;
}

int oakengine_node_keyframe_set_bezier_point(
	OakEngineNode *self, const char *input_id, int element, int64_t time_ts,
	int track, int point_index, double x, double y, double old_x,
	double old_y)
{
	set_error(QString());
	olive::Node *node = impl(self);
	if (checked_keyframe_input(node, input_id) == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	if (point_index < 0 || point_index > 1) {
		set_error(QStringLiteral("invalid bezier point index %1")
					  .arg(point_index));
		return OAKENGINE_E_INVALID;
	}
	const QString id = QString::fromUtf8(input_id);
	const olive::Rational tb = project_time_base(node);
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	olive::NodeKeyframe *key = find_keyframe(
		node, olive::NodeInput(node, id, element), time, track);
	if (!key) {
		set_error(QStringLiteral("no keyframe at time %1 track %2 on \"%3\"")
					  .arg(time_ts)
					  .arg(track)
					  .arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::NodeKeyframe::BezierType mode =
		(point_index == 0) ? olive::NodeKeyframe::k_in_handle :
							 olive::NodeKeyframe::k_out_handle;
	if (std::isnan(old_x) || std::isnan(old_y)) {
		push_or_run(new KeyframeSetBezierPointCommand(key, mode,
													  QPointF(x, y)),
					QStringLiteral("Set Keyframe Bezier Point"));
	} else {
		push_or_run(new KeyframeSetBezierPointCommand(
						key, mode, QPointF(x, y), QPointF(old_x, old_y)),
					QStringLiteral("Set Keyframe Bezier Point"));
	}
	return OAKENGINE_OK;
}

int oakengine_node_keyframes_clear(OakEngineNode *self, const char *input_id)
{
	set_error(QString());
	olive::Node *node = impl(self);
	if (checked_keyframe_input(impl(self), input_id) ==
		olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	const QString id = QString::fromUtf8(input_id);
	olive::NodeInputImmediate *imm = node->get_immediate(id, -1);
	if (!imm) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	// Nothing to clear: succeed without pushing an empty undo command.
	if (node->get_keyframe_tracks(id, -1).isEmpty() ||
		node->get_keyframe_tracks(id, -1).first().isEmpty()) {
		return OAKENGINE_OK;
	}
	push_or_run(new olive::NodeImmediateRemoveAllKeyframesCommand(imm),
				QStringLiteral("Clear Keyframes"));
	return OAKENGINE_OK;
}

} // extern "C"
