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
#include <QBrush>
#include <QString>
#include <QVariant>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include "coreengine.h"
#include "config/config.h"
#include "node/factory.h"
#include "node/keyframe.h"
#include "node/node.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "node/value.h"
#include "node/group/group.h"
#include "node/input/multicam/multicamnode.h"
#include "node/audio/volume/volume.h"
#include "node/distort/transform/transformdistortnode.h"
#include "node/block/transition/transition.h"
#include "node/block/subtitle/subtitle.h"
#include "node/block/clip/clip.h"
#include "node/output/track/track.h"
#include "node/output/viewer/viewer.h"
#include "node/project/footage/footage.h"
#include "node/project/folder/folder.h"
#include "node/gizmo/gizmo.h"
#include "pluginSupport/oliveplugininstance.h"
#include "node/generator/shape/shapenodebase.h"
#include "audio/audiovisualwaveform.h"
#include "node/inputimmediate.h"
#include "undo/undocommand.h"
#include "undo/undostack.h"
#include "undointernal.h"

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
	oakengine_undo_push_or_run(command, name);
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
	case olive::NodeValue::k_text:
		return OAK_NODE_VALUE_TEXT;
	case olive::NodeValue::k_font:
		return OAK_NODE_VALUE_FONT;
	case olive::NodeValue::k_str_combo:
		return OAK_NODE_VALUE_STR_COMBO;
	case olive::NodeValue::k_binary:
		return OAK_NODE_VALUE_BINARY;
	case olive::NodeValue::k_bezier:
		return OAK_NODE_VALUE_BEZIER;
	case olive::NodeValue::k_texture:
		return OAK_NODE_VALUE_TEXTURE;
	case olive::NodeValue::k_samples:
		return OAK_NODE_VALUE_SAMPLES;
	case olive::NodeValue::k_video_params:
		return OAK_NODE_VALUE_VIDEO_PARAMS;
	case olive::NodeValue::k_audio_params:
		return OAK_NODE_VALUE_AUDIO_PARAMS;
	default:
		return OAK_NODE_VALUE_NONE;
	}
}

// Convert a raw QVariant (not wrapped in NodeValue) to C POD based on type.
// Used for default values where the QVariant holds the native type directly.
static bool qvariant_to_pod(olive::NodeValue::Type type, const QVariant &qv,
                            oak_node_value *out)
{
    switch (type) {
    case olive::NodeValue::k_int:
    case olive::NodeValue::k_combo:
        out->num = qv.toLongLong();
        return true;
    case olive::NodeValue::k_float:
        out->f[0] = qv.toDouble();
        return true;
    case olive::NodeValue::k_boolean:
        out->num = qv.toBool() ? 1 : 0;
        return true;
    case olive::NodeValue::k_rational: {
        const olive::Rational r = qv.value<olive::Rational>();
        out->num = r.numerator();
        out->den = r.denominator();
        return true;
    }
    case olive::NodeValue::k_color: {
        const olive::core::Color c = qv.value<olive::core::Color>();
        out->f[0] = c.red();
        out->f[1] = c.green();
        out->f[2] = c.blue();
        out->f[3] = c.alpha();
        return true;
    }
    case olive::NodeValue::k_vec2:
        if (qv.canConvert<QVector2D>()) {
            const QVector2D v2 = qv.value<QVector2D>();
            out->f[0] = v2.x();
            out->f[1] = v2.y();
            return true;
        }
        return false;
    case olive::NodeValue::k_vec3:
        if (qv.canConvert<QVector3D>()) {
            const QVector3D v3 = qv.value<QVector3D>();
            out->f[0] = v3.x();
            out->f[1] = v3.y();
            out->f[2] = v3.z();
            return true;
        }
        return false;
    case olive::NodeValue::k_vec4:
        if (qv.canConvert<QVector4D>()) {
            const QVector4D v4 = qv.value<QVector4D>();
            out->f[0] = v4.x();
            out->f[1] = v4.y();
            out->f[2] = v4.z();
            out->f[3] = v4.w();
            return true;
        }
        return false;
    default:
        return false;
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

int oakengine_node_factory_id_count(void)
{
	return olive::NodeFactory::get_library().size();
}

OakEngineNode *oakengine_node_factory_create_from_id(const char *type_id)
{
	if (!type_id) {
		return nullptr;
	}
	return wrap(olive::NodeFactory::create_from_id(QString::fromUtf8(type_id)));
}

int oakengine_node_factory_name_from_id(const char *type_id, char *buf,
										int buf_size)
{
	if (!type_id) {
		if (buf && buf_size > 0) buf[0] = '\0';
		return 0;
	}
	const QString name = olive::NodeFactory::get_name_from_id(
		QString::fromUtf8(type_id));
	return string_to_buf(name, buf, buf_size);
}

OakEngineNode *oakengine_node_factory_node_at(int index)
{
	const QList<olive::Node *> &lib = olive::NodeFactory::get_library();
	if (index < 0 || index >= lib.size()) {
		return nullptr;
	}
	return wrap(lib.at(index));
}

int oakengine_node_category_count(const OakEngineNode *self)
{
	return self ? impl(self)->category().size() : 0;
}

int oakengine_node_category_at(const OakEngineNode *self, int index)
{
	if (!self) {
		return -1;
	}
	const QVector<olive::Node::CategoryID> cats = impl(self)->category();
	if (index < 0 || index >= cats.size()) {
		return -1;
	}
	return int(cats.at(index));
}

uint64_t oakengine_node_get_flags(const OakEngineNode *self)
{
	return self ? impl(self)->get_flags() : 0;
}

uint64_t oakengine_node_flag_dont_show_in_create_menu(void)
{
	return olive::Node::k_dont_show_in_create_menu;
}

uint64_t oakengine_node_flag_dont_show_in_param_view(void)
{
	return olive::Node::k_dont_show_in_param_view;
}

uint64_t oakengine_node_flag_video_effect(void)
{
	return olive::Node::k_video_effect;
}

uint64_t oakengine_node_flag_audio_effect(void)
{
	return olive::Node::k_audio_effect;
}

void oakengine_node_retranslate(OakEngineNode *self)
{
	if (self) {
		impl(self)->retranslate();
	}
}

int oakengine_node_get_sub_category(const OakEngineNode *self, char *buf,
									int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->sub_category(), buf, buf_size);
}

int oakengine_node_get_description(const OakEngineNode *self, char *buf,
								   int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->description(), buf, buf_size);
}

OakEngineNode *oakengine_node_create_copy(const OakEngineNode *self)
{
	return self ? wrap(impl(self)->copy()) : nullptr;
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

int oakengine_node_get_short_name(const OakEngineNode *self, char *buf,
								  int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->short_name(), buf, buf_size);
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

int oakengine_node_rename_many(OakEngineNode **nodes, int count,
								 const char *label,
								 void *parent_multi_or_NULL)
{
	set_error(QString());
	if (count < 0 || (count > 0 && !nodes)) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (count == 0) {
		return OAKENGINE_OK;
	}
	// One multi-node rename command, like Core::label_nodes().
	auto *command = new olive::NodeRenameCommand();
	const QString text = QString::fromUtf8(label ? label : "");
	for (int i = 0; i < count; i++) {
		if (!nodes[i]) {
			set_error(QStringLiteral("invalid node at index %1").arg(i));
			delete command;
			return OAKENGINE_E_INVALID;
		}
		command->add_node(impl(nodes[i]), text);
	}
	if (parent_multi_or_NULL) {
		static_cast<olive::MultiUndoCommand *>(parent_multi_or_NULL)->add_child(
			command);
	} else {
		push_or_run(command, QStringLiteral("Rename Nodes"));
	}
	return OAKENGINE_OK;
}

extern "C" void *oakengine_node_rename_command(OakEngineNode *node,
											   const char *label)
{
	if (!node) {
		return nullptr;
	}
	return new olive::NodeRenameCommand(impl(node),
										QString::fromUtf8(label ? label : ""));
}

int oakengine_node_set_label_many(OakEngineNode **nodes, int count,
								  const char *label)
{
	return oakengine_node_rename_many(nodes, count, label, nullptr);
}

int oakengine_node_set_color_label(OakEngineNode **nodes, int count,
								   int color_index)
{
	set_error(QString());
	if (count < 0 || (count > 0 && !nodes)) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (count == 0) {
		return OAKENGINE_OK;
	}
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	for (int i = 0; i < count; i++) {
		if (!nodes[i]) {
			set_error(QStringLiteral("invalid node at index %1").arg(i));
			delete command;
			return OAKENGINE_E_INVALID;
		}
		command->add_child(
			new olive::NodeOverrideColorCommand(impl(nodes[i]),
												color_index));
	}
	push_or_run(command, QStringLiteral("Set Node Color Labels"));
	return OAKENGINE_OK;
}

extern "C" void *oakengine_node_set_color_label_command(OakEngineNode *node,
													  int color_index)
{
	olive::Node *n = impl(node);
	if (!n) {
		return nullptr;
	}
	return new olive::NodeOverrideColorCommand(n, color_index);
}

int oakengine_node_get_color_label(const OakEngineNode *self)
{
	if (!self) {
		return -1;
	}
	return impl(self)->get_override_color();
}

int oakengine_node_get_effective_color_label(const OakEngineNode *self)
{
	if (!self) {
		return -1;
	}
	const olive::Node *n = impl(self);
	int c = n->get_override_color();
	if (c < 0) {
		c = olive::Config::current()[QStringLiteral("CatColor%1").arg(
				n->category().first())]
				.toInt();
	}
	return c;
}

int oakengine_node_input_count(const OakEngineNode *self)
{
	return self ? impl(self)->inputs().size() : 0;
}

void oakengine_node_get_brush(const OakEngineNode *self, double top,
							  double bottom, void *out_qbrush)
{
	if (!self || !out_qbrush) {
		return;
	}
	*static_cast<QBrush *>(out_qbrush) = impl(self)->brush(top, bottom);
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

extern "C" void *oakengine_node_set_standard_value_command(
	OakEngineNode *self, const char *input_id, int element, int track,
	const oak_node_value *v)
{
	if (!self || !input_id || !v) {
		return nullptr;
	}
	olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->inputs().contains(id)) {
		return nullptr;
	}
	const olive::NodeValue::Type declared = node->get_input_data_type(id);
	if (to_c_type(declared) == OAK_NODE_VALUE_NONE) {
		return nullptr;
	}
	QVariant value;
	if (track < 0) {
		// Track -1 writes the whole single-track value.
		if (!value_from_c(v, declared, &value)) {
			return nullptr;
		}
	} else {
		// Per-track component (the command stores the value on one track).
		if (!component_from_c(v, declared, 0, &value)) {
			return nullptr;
		}
	}
	return new olive::NodeParamSetStandardValueCommand(
		olive::NodeKeyframeTrackReference(olive::NodeInput(node, id, element),
									  track),
		value);
}

extern "C" void *oakengine_node_set_input_video_params_command(
	OakEngineNode *self, const char *input_id, const oak_video_params *params)
{
	if (!self || !input_id || !params) {
		return nullptr;
	}
	olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->inputs().contains(id)) {
		return nullptr;
	}
	if (node->get_input_data_type(id) != olive::NodeValue::k_video_params) {
		return nullptr;
	}
	const olive::VideoParams params_cpp(
		params->width, params->height, olive::Rational(params->time_base_num,
												   params->time_base_den),
		static_cast<olive::PixelFormat::Format>(params->format),
		olive::VideoParams::k_internal_channel_count,
		olive::Rational(params->pixel_aspect_num, params->pixel_aspect_den),
		static_cast<olive::VideoParams::Interlacing>(params->interlacing),
		params->divider);
	return new olive::NodeParamSetStandardValueCommand(
		olive::NodeKeyframeTrackReference(olive::NodeInput(node, id)),
		QVariant::fromValue(params_cpp));
}

extern "C" void *oakengine_node_set_value_at_time_command(
	void *node, const char *input, int element, int64_t time_num,
	int64_t time_den, const oak_node_value *value, int track,
	int insert_on_all_tracks_if_no_key)
{
	if (!node || !input || !value || time_den == 0) {
		return nullptr;
	}
	olive::Node *n = reinterpret_cast<olive::Node *>(node);
	const QString id = QString::fromUtf8(input);
	if (!n->inputs().contains(id)) {
		return nullptr;
	}
	const olive::NodeValue::Type declared = n->get_input_data_type(id);
	const int nb_tracks =
		olive::NodeValue::get_number_of_keyframe_tracks(declared);
	if (track < -1 || track >= nb_tracks || nb_tracks == 0) {
		return nullptr;
	}
	if (declared == olive::NodeValue::k_file ||
		declared == olive::NodeValue::k_text ||
		declared == olive::NodeValue::k_font ||
		declared == olive::NodeValue::k_str_combo) {
		return nullptr;
	}

	const olive::Rational time(time_num, time_den);
	const olive::NodeInput node_input(n, id, element);
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	if (track == -1) {
		for (int i = 0; i < nb_tracks; i++) {
			QVariant component;
			if (!component_from_c(value, declared, i, &component)) {
				delete command;
				return nullptr;
			}
			olive::Node::set_value_at_time(node_input, time, component, i,
									   command, false);
		}
	} else {
		QVariant component;
		if (!component_from_c(value, declared, 0, &component)) {
			delete command;
			return nullptr;
		}
		olive::Node::set_value_at_time(
			node_input, time, component, track, command,
			insert_on_all_tracks_if_no_key != 0);
	}
	return command;
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

void oakengine_node_delete_later(OakEngineNode *node)
{
	if (olive::Node *n = impl(node)) {
		n->deleteLater();
	}
}

void oakengine_node_free(OakEngineNode *node)
{
	// Immediate destruction of an owned, orphaned node (see the header
	// comment for the strict preconditions).
	delete impl(node);
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

extern "C" void *oakengine_node_connect_command(OakEngineNode *output_node,
												OakEngineNode *input_node,
												const char *input_id,
												int element)
{
	olive::Node *out_node = impl(output_node);
	olive::Node *in_node = impl(input_node);
	if (!out_node || !in_node || !input_id) {
		return nullptr;
	}
	const QString id = QString::fromUtf8(input_id);
	if (!in_node->inputs().contains(id)) {
		return nullptr;
	}
	return new olive::NodeEdgeAddCommand(
		out_node, olive::NodeInput(in_node, id, element));
}

extern "C" void *oakengine_node_disconnect_command(OakEngineNode *input_node,
												   const char *input_id,
												   int element)
{
	olive::Node *in_node = impl(input_node);
	if (!in_node || !input_id) {
		return nullptr;
	}
	const QString id = QString::fromUtf8(input_id);
	if (!in_node->inputs().contains(id)) {
		return nullptr;
	}
	const olive::NodeInput input(in_node, id, element);
	olive::Node *connected = in_node->get_connected_output(input);
	if (!connected) {
		return nullptr;
	}
	return new olive::NodeEdgeRemoveCommand(connected, input);
}

extern "C" int oakengine_block_link(void *a, void *b, int linked)
{
	if (!a || !b) {
		return OAKENGINE_E_INVALID;
	}
	olive::Node *na = reinterpret_cast<olive::Node *>(a);
	olive::Node *nb = reinterpret_cast<olive::Node *>(b);
	const bool ok = linked ? olive::Node::link(na, nb) :
							 olive::Node::unlink(na, nb);
	return ok ? 1 : 0;
}

extern "C" void *oakengine_node_add_to_project_command(OakEngineProject *project,
												   OakEngineNode *node)
{
	olive::Project *p = impl(project);
	olive::Node *n = impl(node);
	if (!p || !n) {
		return nullptr;
	}
	return new olive::NodeAddCommand(p, n);
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

extern "C" void *oakengine_node_insert_keyframe_command(
	OakEngineNode *self, const char *input_id, int element, int track,
	int64_t time_ts, const oak_node_value *value, int type, float x1, float y1,
	float x2, float y2)
{
	if (!self || !input_id || !value || type < 0 || type > 2) {
		return nullptr;
	}
	olive::Node *node = impl(self);
	const olive::NodeValue::Type declared =
		checked_keyframe_input(node, input_id);
	if (declared == olive::NodeValue::k_none) {
		return nullptr;
	}
	QVariant normal;
	if (!value_from_c(value, declared, &normal)) {
		return nullptr;
	}
	const olive::SplitValue split =
		olive::NodeValue::split_normal_value_into_track_values(declared,
														   normal);
	if (track < 0 || track >= split.size()) {
		return nullptr;
	}
	const QString id = QString::fromUtf8(input_id);
	const olive::Rational time = olive::core::Timecode::timestamp_to_time(
		time_ts, project_time_base(node));
	auto *key = new olive::NodeKeyframe(time, split.at(track),
									to_engine_easing(type), track, element,
									id);
	if (type == 1) {
		key->set_bezier_control_in(QPointF(x1, y1));
		key->set_bezier_control_out(QPointF(x2, y2));
	}
	return new olive::NodeParamInsertKeyframeCommand(node, key);
}

extern "C" void *oakengine_node_remove_keyframe_command(
	OakEngineKeyframe *keyframe)
{
	auto *key = reinterpret_cast<olive::NodeKeyframe *>(keyframe);
	if (!key) {
		return nullptr;
	}
	return new olive::NodeParamRemoveKeyframeCommand(key);
}

extern "C" void *oakengine_keyframe_set_time_command(
	OakEngineKeyframe *keyframe, int64_t new_time_ts)
{
	auto *key = reinterpret_cast<olive::NodeKeyframe *>(keyframe);
	if (!key || !key->parent()) {
		return nullptr;
	}
	const olive::Rational tb = project_time_base(key->parent());
	const olive::Rational new_time =
		olive::core::Timecode::timestamp_to_time(new_time_ts, tb);
	return new olive::NodeParamSetKeyframeTimeCommand(key, new_time);
}

extern "C" void *oakengine_keyframe_set_value_command(
	OakEngineKeyframe *keyframe, const oak_node_value *value)
{
	auto *key = reinterpret_cast<olive::NodeKeyframe *>(keyframe);
	if (!key || !value || !key->parent()) {
		return nullptr;
	}
	const olive::NodeValue::Type declared =
		key->parent()->get_input_data_type(key->input());
	QVariant v;
	if (!component_from_c(value, declared, key->track(), &v)) {
		return nullptr;
	}
	return new olive::NodeParamSetKeyframeValueCommand(key, v);
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

/* ---- Extended input introspection ----------------------------------------- */

int oakengine_node_input_is_array(const OakEngineNode *self,
								  const char *input_id)
{
	if (!self || !input_id) {
		return 0;
	}
	return impl(self)->input_is_array(QString::fromUtf8(input_id)) ? 1 : 0;
}

int oakengine_node_input_array_size(const OakEngineNode *self,
									const char *input_id)
{
	if (!self || !input_id) {
		return 0;
	}
	return impl(self)->input_array_size(QString::fromUtf8(input_id));
}

int oakengine_node_input_get_flags(const OakEngineNode *self,
								   const char *input_id)
{
	if (!self || !input_id) {
		return 0;
	}
	return int(impl(self)->get_input_flags(QString::fromUtf8(input_id)));
}

int oakengine_node_input_get_data_type(const OakEngineNode *self,
									   const char *input_id)
{
	if (!self || !input_id) {
		return -1;
	}
	return int(impl(self)->get_input_data_type(QString::fromUtf8(input_id)));
}

int oakengine_node_input_is_connectable(const OakEngineNode *self,
										const char *input_id)
{
	if (!self || !input_id) {
		return 0;
	}
	return impl(self)->is_input_connectable(QString::fromUtf8(input_id)) ? 1 : 0;
}

int oakengine_node_input_is_keyframable(const OakEngineNode *self,
										const char *input_id)
{
	if (!self || !input_id) {
		return 0;
	}
	return impl(self)->is_input_keyframable(QString::fromUtf8(input_id)) ? 1 : 0;
}

int oakengine_node_input_is_hidden(const OakEngineNode *self,
								   const char *input_id)
{
	if (!self || !input_id) {
		return 0;
	}
	return impl(self)->is_input_hidden(QString::fromUtf8(input_id)) ? 1 : 0;
}

int oakengine_node_input_is_keyframed_ex(const OakEngineNode *self,
										 const char *input_id, int element)
{
	if (!self || !input_id) {
		return 0;
	}
	return impl(self)->is_input_keyframing(QString::fromUtf8(input_id),
										   element) ? 1 : 0;
}

int oakengine_node_get_label_and_name(const OakEngineNode *self, char *buf,
									  int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->get_label_and_name(), buf, buf_size);
}

int oakengine_node_get_input_name(const OakEngineNode *self,
								  const char *input_id, char *buf,
								  int buf_size)
{
	if (!self || !input_id) {
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeInput input(const_cast<olive::Node *>(impl(self)),
								 QString::fromUtf8(input_id));
	return string_to_buf(input.get_input_name(), buf, buf_size);
}

int oakengine_node_input_get_default_value(const OakEngineNode *self,
										   const char *input_id, int track,
										   oak_node_value *out)
{
	set_error(QString());
	if (!self || !input_id || !out) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeInput input(const_cast<olive::Node *>(impl(self)),
								 QString::fromUtf8(input_id));
	const olive::NodeValue::Type type = input.get_data_type();
	if (type == olive::NodeValue::k_none) {
		set_error(QStringLiteral("unknown input id \"%1\"")
					  .arg(QString::fromUtf8(input_id)));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (track >= 0) {
		// Validate track count: inputs may have split tracks (Color→4, Vec2→2, etc.)
		// or a single whole-value track (track 0).
		int num_tracks = impl(self)->get_number_of_keyframe_tracks(
			QString::fromUtf8(input_id));
		if (track >= qMax(1, num_tracks)) {
			set_error(QStringLiteral("track index out of range"));
			return OAKENGINE_E_NOT_FOUND;
		}
	}
	const QVariant def = (track >= 0) ?
		input.get_split_default_value_for_track(track) :
		input.get_default_value();
	// Convert the default QVariant directly to C POD. Bypass NodeValue
	// constructor because passing QVariant to the template constructor
	// nests it inside another QVariant, breaking value_to_c extraction.
	// Also handle split defaults: for k_color the QVariant may be a float
	// (single channel) instead of a full Color.
	memset(out, 0, sizeof(*out));
	out->type = to_c_type(type);
	if (type == olive::NodeValue::k_color && def.typeId() == QMetaType::Float) {
		out->f[0] = def.toFloat();
		out->f[1] = 0.0;
		out->f[2] = 0.0;
		out->f[3] = 1.0;
	} else if (!qvariant_to_pod(type, def, out)) {
		set_error(QStringLiteral("default value has no POD representation"));
		return OAKENGINE_E_NOT_FOUND;
	}
	return OAKENGINE_OK;
}

OakEngineProject *oakengine_node_get_project(const OakEngineNode *self)
{
	if (!self) {
		return nullptr;
	}
	olive::Project *p = impl(self)->project();
	return reinterpret_cast<OakEngineProject *>(p);
}

OakEngineProject *oakengine_node_parent(const OakEngineNode *self)
{
	if (!self) {
		return nullptr;
	}
	olive::Project *p = impl(self)->parent();
	return reinterpret_cast<OakEngineProject *>(p);
}

int oakengine_node_is_item(const OakEngineNode *self)
{
	if (!self) {
		return 0;
	}
	return impl(self)->is_item() ? 1 : 0;
}

OakEngineNode *oakengine_node_folder(const OakEngineNode *self)
{
	if (!self) {
		return nullptr;
	}
	return wrap(const_cast<olive::Folder *>(impl(self)->folder()));
}

OakEngineNode *oakengine_node_input_get_connected_node(
	const OakEngineNode *self, const char *input_id, int element)
{
	if (!self || !input_id) {
		return nullptr;
	}
	olive::Node *conn = const_cast<olive::Node *>(impl(self))
							->get_connected_output(QString::fromUtf8(input_id),
												   element);
	return wrap(conn);
}

int oakengine_node_copy_inputs(OakEngineNode *dest, const OakEngineNode *src)
{
	set_error(QString());
	if (!dest || !src) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	olive::Node::copy_inputs(impl(src), impl(dest), false, command);
	push_or_run(command, QStringLiteral("Copy Inputs"));
	return OAKENGINE_OK;
}

int oakengine_node_get_input_at_time(const OakEngineNode *self,
									 const char *input_id, int element,
									 int track, int64_t time_ts,
									 int track_for_time, oak_node_value *out)
{
	set_error(QString());
	// Facade contract: `track` is the 0-based component selector (-1 =
	// whole value), time_ts is in SECONDS, track_for_time is the 1-based
	// keyframe track selector (accepted for signature compatibility).
	(void)track_for_time;
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
	if (type == olive::NodeValue::k_file || type == olive::NodeValue::k_text ||
		type == olive::NodeValue::k_font ||
		type == olive::NodeValue::k_str_combo) {
		set_error(QStringLiteral(
			"\"%1\" is a string input; use oakengine_node_get_input_string_at_time()")
					  .arg(id));
		return OAKENGINE_E_INVALID;
	}
	// Facade contract: time_ts is a frame timestamp in the project's frame
	// timebase (like the setter family and the timeline family).
	const olive::Rational tb = project_time_base(node);
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	// When a specific track is requested (track >= 0) and element is not
	// set (element == -1), use track as the element to get the per-track
	// component from the engine's split-value API.
	const int eff_element = (track >= 0 && element < 0) ? track : element;
	// If we're requesting a single track on a multi-component type, use
	// get_split_value_at_time to get the raw component value (float/int).
	// Then construct the output POD directly, bypassing NodeValue which
	// can't handle a scalar QVariant for a multi-component type.
	bool direct_ok = false;
	memset(out, 0, sizeof(*out));
	out->type = to_c_type(type);
	if (track >= 0) {
		// Per-component read: the component is the keyframe track with the
		// same 0-based index; the element is passed through unchanged (for
		// non-array inputs it stays -1, so the keyed path is found).
		const QVariant comp =
			node->get_split_value_at_time_on_track(id, time, track, element);
		if (comp.isValid()) {
			// Scalar components are reported in f[0] for float-like types
			// (color/vec) and in num for integer-like types (bool/int/
			// combo/rational) -- see the at-time readers in
			// oakengine_node_test/oakengine_keyframe_test.
			switch (type) {
			case olive::NodeValue::k_boolean:
			case olive::NodeValue::k_int:
			case olive::NodeValue::k_combo:
				out->num = comp.toLongLong();
				break;
			case olive::NodeValue::k_rational:
				out->num = comp.value<olive::core::Rational>().numerator();
				out->den = comp.value<olive::core::Rational>().denominator();
				break;
			default:
				out->f[0] = comp.toDouble();
				if (type == olive::NodeValue::k_color) {
					out->f[3] = 1.0;
				}
				break;
			}
			direct_ok = true;
		}
	}
	if (!direct_ok) {
		const QVariant sv = node->get_value_at_time(id, time, eff_element);
		if (type == olive::NodeValue::k_color && sv.canConvert<olive::core::Color>()) {
			olive::core::Color c = sv.value<olive::core::Color>();
		}
		olive::NodeValue nv(type, sv);
		if (!value_to_c(nv, out)) {
			set_error(QStringLiteral(
				"input \"%1\" has no POD value at time").arg(id));
			return OAKENGINE_E_NOT_FOUND;
		}
	}
	return OAKENGINE_OK;
}

int oakengine_node_get_input_string_at_time(const OakEngineNode *self,
											const char *input_id, int element,
											int64_t time_ts, int track,
											char *buf, int buf_size)
{
	set_error(QString());
	(void)track;
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
	const olive::NodeValue::Type type = node->get_input_data_type(id);
	if (type != olive::NodeValue::k_file &&
		type != olive::NodeValue::k_text &&
		type != olive::NodeValue::k_font &&
		type != olive::NodeValue::k_str_combo) {
		set_error(QStringLiteral("\"%1\" is not a string input").arg(id));
		return OAKENGINE_E_INVALID;
	}
	const olive::Rational tb = project_time_base(node);
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	const QVariant sv = node->get_value_at_time(id, time, element);
	return string_to_buf(sv.toString(), buf, buf_size);
}

int oakengine_node_get_input_bezier_at_time(const OakEngineNode *self,
											const char *input_id, int element,
											int64_t time_ts, int track,
											double *out_6)
{
	set_error(QString());
	if (!self || !input_id || !out_6) {
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
	if (type != olive::NodeValue::k_bezier) {
		set_error(QStringLiteral("\"%1\" is not a bezier input").arg(id));
		return OAKENGINE_E_INVALID;
	}
	const olive::Rational tb = project_time_base(node);
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	const int nb_tracks =
		olive::NodeValue::get_number_of_keyframe_tracks(type);
	double vals[6] = { 0 };
	const int n = qMin(6, nb_tracks);
	for (int i = 0; i < n; ++i) {
		const QVariant comp =
			node->get_split_value_at_time_on_track(id, time, i, element);
		vals[i] = comp.toDouble();
	}
	out_6[0] = vals[0];
	out_6[1] = vals[1];
	out_6[2] = vals[2];
	out_6[3] = vals[3];
	out_6[4] = vals[4];
	out_6[5] = vals[5];
	return OAKENGINE_OK;
}

int oakengine_node_get_input_binary_at_time(const OakEngineNode *self,
											const char *input_id, int element,
											int64_t time_ts, int track,
											char *buf, int buf_size)
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
	const olive::NodeValue::Type type = node->get_input_data_type(id);
	if (type != olive::NodeValue::k_binary) {
		set_error(QStringLiteral("\"%1\" is not a binary input").arg(id));
		return OAKENGINE_E_INVALID;
	}
	const olive::Rational tb = project_time_base(node);
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	const QVariant sv = node->get_value_at_time(id, time, element);
	const QByteArray bytes = sv.toByteArray();
	if (buf && buf_size > 0) {
		const int n = qMin(buf_size, bytes.size());
		if (n > 0) {
			memcpy(buf, bytes.constData(), size_t(n));
		}
	}
	return bytes.size();
}

/* ---- Input properties ----------------------------------------------------- */

int oakengine_node_input_has_property(const OakEngineNode *self,
									  const char *input_id, const char *key)
{
	if (!self || !input_id || !key) {
		return 0;
	}
	return impl(self)->has_input_property(QString::fromUtf8(input_id),
										  QString::fromUtf8(key)) ? 1 : 0;
}

int oakengine_node_set_input_property_string(OakEngineNode *self,
											 const char *input_id,
											 const char *key,
											 const char *value, int notify)
{
	set_error(QString());
	if (!self || !input_id || !key) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	// The engine's set_input_property is direct (no undo). We wrap it in
	// the push_or_run pattern when notify != 0.
	olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->inputs().contains(id)) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	// Apply directly; property changes are typically not undoable at the
	// engine level (they are UI hints). The engine's set_input_property
	// always emits input_property_changed; the facade's `notify` flag
	// controls whether that emission (and therefore the facade event)
	// fires.
	if (notify) {
		node->set_input_property(id, QString::fromUtf8(key),
								 QVariant::fromValue(QString::fromUtf8(value ? value : "")));
	} else {
		const QSignalBlocker blocker(node);
		node->set_input_property(id, QString::fromUtf8(key),
								 QVariant::fromValue(QString::fromUtf8(value ? value : "")));
	}
	return OAKENGINE_OK;
}

int oakengine_node_input_get_property_string(const OakEngineNode *self,
											 const char *input_id,
											 const char *key, char *buf,
											 int buf_size)
{
	set_error(QString());
	if (!self || !input_id || !key) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->has_input_property(id, QString::fromUtf8(key))) {
		set_error(QStringLiteral("property \"%1\" not found on \"%2\"")
					  .arg(QString::fromUtf8(key)).arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	return string_to_buf(
		node->get_input_property(id, QString::fromUtf8(key)).toString(),
		buf, buf_size);
}

int oakengine_node_input_get_property_number(const OakEngineNode *self,
											 const char *input_id,
											 const char *key, int track,
											 double *out)
{
	set_error(QString());
	if (!self || !input_id || !key || !out) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->has_input_property(id, QString::fromUtf8(key))) {
		set_error(QStringLiteral("property \"%1\" not found on \"%2\"")
					  .arg(QString::fromUtf8(key)).arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	const QVariant v = node->get_input_property(id, QString::fromUtf8(key));
	bool ok = false;
	const double d = v.toDouble(&ok);
	if (!ok) {
		set_error(QStringLiteral("property \"%1\" is not a number")
					  .arg(QString::fromUtf8(key)));
		return OAKENGINE_E_INVALID;
	}
	*out = d;
	return OAKENGINE_OK;
}

int oakengine_node_input_get_property_int(const OakEngineNode *self,
										  const char *input_id,
										  const char *key, int64_t *out)
{
	set_error(QString());
	if (!self || !input_id || !key || !out) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->has_input_property(id, QString::fromUtf8(key))) {
		set_error(QStringLiteral("property \"%1\" not found on \"%2\"")
					  .arg(QString::fromUtf8(key)).arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	const QVariant v = node->get_input_property(id, QString::fromUtf8(key));
	bool ok = false;
	const qlonglong i = v.toLongLong(&ok);
	if (!ok) {
		set_error(QStringLiteral("property \"%1\" is not an integer")
					  .arg(QString::fromUtf8(key)));
		return OAKENGINE_E_INVALID;
	}
	*out = int64_t(i);
	return OAKENGINE_OK;
}

int oakengine_node_input_get_property_rational(const OakEngineNode *self,
											   const char *input_id,
											   const char *key, int *num,
											   int *den)
{
	set_error(QString());
	if (!self || !input_id || !key) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->has_input_property(id, QString::fromUtf8(key))) {
		set_error(QStringLiteral("property \"%1\" not found on \"%2\"")
					  .arg(QString::fromUtf8(key)).arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	const QVariant v = node->get_input_property(id, QString::fromUtf8(key));
	// Try rational first; fall back to converting a plain number.
	if (v.canConvert<olive::Rational>()) {
		const olive::Rational r = v.value<olive::Rational>();
		if (num) *num = r.numerator();
		if (den) *den = r.denominator();
		return OAKENGINE_OK;
	}
	// Fallback: treat as double and return (value, 1).
	bool ok = false;
	const double d = v.toDouble(&ok);
	if (ok) {
		if (num) *num = int(d);
		if (den) *den = 1;
		return OAKENGINE_OK;
	}
	if (num) *num = 0;
	if (den) *den = 1;
	return OAKENGINE_OK;
}

int oakengine_node_input_get_property_track_number(const OakEngineNode *self,
												   const char *input_id,
												   const char *key, int track,
												   double *out)
{
	set_error(QString());
	if (!self || !input_id || !key || !out) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->has_input_property(id, QString::fromUtf8(key))) {
		set_error(QStringLiteral("property \"%1\" not found on \"%2\"")
					  .arg(QString::fromUtf8(key)).arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	const QVariant v = node->get_input_property(id, QString::fromUtf8(key));
	const olive::NodeValue::Type dt = node->get_input_data_type(id);
	const int c_type = to_c_type(dt);
	oak_node_value normal;
	memset(&normal, 0, sizeof(normal));
	normal.type = c_type;
	if (!qvariant_to_pod(dt, v, &normal)) {
		set_error(QStringLiteral("property \"%1\" is not numeric")
					  .arg(QString::fromUtf8(key)));
		return OAKENGINE_E_INVALID;
	}
	const int tc = olive::NodeValue::get_number_of_keyframe_tracks(dt);
	QVector<oak_node_value> track_vals(tc);
	if (oakengine_node_value_split_to_tracks(c_type, &normal,
											 track_vals.data(), tc) !=
		OAKENGINE_OK) {
		set_error(QStringLiteral("property \"%1\" could not be split")
					  .arg(QString::fromUtf8(key)));
		return OAKENGINE_E_INVALID;
	}
	if (track < 0 || track >= tc) {
		set_error(QStringLiteral("track %1 out of range").arg(track));
		return OAKENGINE_E_INVALID;
	}
	*out = track_vals.at(track).f[0];
	return OAKENGINE_OK;
}

int oakengine_node_input_get_property_count(const OakEngineNode *self,
											const char *input_id)
{
	if (!self || !input_id) {
		return 0;
	}
	return impl(self)->get_input_properties(QString::fromUtf8(input_id)).size();
}

int oakengine_node_input_get_property_key(const OakEngineNode *self,
										  const char *input_id, int index,
										  char *buf, int buf_size)
{
	set_error(QString());
	if (!self || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const QHash<QString, QVariant> props =
		impl(self)->get_input_properties(QString::fromUtf8(input_id));
	const QList<QString> keys = props.keys();
	if (index < 0 || index >= keys.size()) {
		set_error(QStringLiteral("property index %1 out of range").arg(index));
		return OAKENGINE_E_NOT_FOUND;
	}
	return string_to_buf(keys.at(index), buf, buf_size);
}

int oakengine_node_input_get_property_string_list_count(
	const OakEngineNode *self, const char *input_id, const char *key)
{
	if (!self || !input_id || !key) {
		return 0;
	}
	const QVariant v = impl(self)->get_input_property(
		QString::fromUtf8(input_id), QString::fromUtf8(key));
	if (v.typeId() == QMetaType::QStringList) {
		return v.toStringList().size();
	}
	if (v.typeId() == QMetaType::QString) {
		return 1;
	}
	return 0;
}

int oakengine_node_input_get_property_string_list(
	const OakEngineNode *self, const char *input_id, const char *key,
	int index, char *buf, int buf_size)
{
	set_error(QString());
	if (!self || !input_id || !key) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const QVariant v = impl(self)->get_input_property(
		QString::fromUtf8(input_id), QString::fromUtf8(key));
	QStringList list;
	if (v.typeId() == QMetaType::QStringList) {
		list = v.toStringList();
	} else if (v.typeId() == QMetaType::QString) {
		list = QStringList(v.toString());
	} else {
		set_error(QStringLiteral("property \"%1\" is not a string list")
					  .arg(QString::fromUtf8(key)));
		return OAKENGINE_E_INVALID;
	}
	if (index < 0 || index >= list.size()) {
		set_error(QStringLiteral("property \"%1\" index %2 out of range")
					  .arg(QString::fromUtf8(key)).arg(index));
		return OAKENGINE_E_NOT_FOUND;
	}
	return string_to_buf(list.at(index), buf, buf_size);
}

/* ---- Node type queries ---------------------------------------------------- */

int oakengine_node_is_group(const OakEngineNode *self)
{
	if (!self) {
		return 0;
	}
	return dynamic_cast<const olive::NodeGroup *>(impl(self)) != nullptr ? 1 : 0;
}

int oakengine_node_is_multicam(const OakEngineNode *self)
{
	if (!self) {
		return 0;
	}
	return dynamic_cast<const olive::MultiCamNode *>(impl(self)) != nullptr ? 1 : 0;
}

/* ---- Context positions ---------------------------------------------------- */

int oakengine_node_context_node_count(const OakEngineNode *context)
{
	if (!context) {
		return OAKENGINE_E_INVALID;
	}
	return impl(context)->get_context_positions().size();
}

int oakengine_node_context_contains_node(const OakEngineNode *context,
										 const OakEngineNode *node)
{
	if (!context || !node) {
		return OAKENGINE_E_INVALID;
	}
	return impl(context)->context_contains_node(const_cast<olive::Node *>(impl(node))) ?
		   1 : 0;
}

OakEngineNode *oakengine_node_context_node_at(OakEngineNode *context,
											  int index, double *x,
											  double *y, int *expanded)
{
	if (!context || index < 0) {
		return nullptr;
	}
	const olive::Node::PositionMap &map = impl(context)->get_context_positions();
	if (index >= map.size()) {
		return nullptr;
	}
	auto it = map.constBegin();
	for (int i = 0; i < index; i++) {
		++it;
	}
	if (x) {
		*x = it.value().position.x();
	}
	if (y) {
		*y = it.value().position.y();
	}
	if (expanded) {
		*expanded = it.value().expanded ? 1 : 0;
	}
	return wrap(it.key());
}

int oakengine_node_set_context_position(OakEngineNode *context,
										OakEngineNode *node, double x,
										double y)
{
	set_error(QString());
	if (!context || !node) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::Node *ctx = impl(context);
	olive::Node *n = impl(node);
	olive::Node::Position pos(QPointF(x, y));
	// A plain move must not reset the expanded flag (matches the C++
	// QPointF overload of Node::set_node_position_in_context).
	if (ctx->context_contains_node(n)) {
		pos.expanded = ctx->get_node_position_data_in_context(n).expanded;
	}
	push_or_run(new olive::NodeSetPositionCommand(n, ctx, pos),
				QStringLiteral("Set Position"));
	return OAKENGINE_OK;
}

int oakengine_node_get_context_position(const OakEngineNode *context,
										const OakEngineNode *node,
										double *x, double *y, int *expanded)
{
	set_error(QString());
	if (!context || !node) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Node *ctx = impl(context);
	const olive::Node *n = impl(node);
	if (!const_cast<olive::Node *>(ctx)->context_contains_node(const_cast<olive::Node *>(n))) {
		set_error(QStringLiteral("node not found in context"));
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::Node::Position pos =
		const_cast<olive::Node *>(ctx)->get_node_position_data_in_context(
			const_cast<olive::Node *>(n));
	if (x) {
		*x = pos.position.x();
	}
	if (y) {
		*y = pos.position.y();
	}
	if (expanded) {
		*expanded = pos.expanded ? 1 : 0;
	}
	return OAKENGINE_OK;
}

int oakengine_node_set_context_expanded(OakEngineNode *context,
										OakEngineNode *node, int expanded)
{
	set_error(QString());
	if (!context || !node) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::Node *ctx = impl(context);
	olive::Node *n = impl(node);
	ctx->set_node_expanded_in_context(n, expanded != 0);
	return OAKENGINE_OK;
}

/* ---- Effect input --------------------------------------------------------- */

int oakengine_node_get_effect_input(const OakEngineNode *self,
									char *input_id, int input_id_size,
									int *element)
{
	set_error(QString());
	if (!self) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeInput ei =
		const_cast<olive::Node *>(impl(self))->get_effect_input();
	if (!ei.is_valid()) {
		set_error(QStringLiteral("node has no effect input"));
		return OAKENGINE_E_NOT_FOUND;
	}
	const int len = string_to_buf(ei.input(), input_id, input_id_size);
	if (element) {
		*element = ei.element();
	}
	return len;
}

/* ---- Group passthrough ---------------------------------------------------- */


int oakengine_group_input_passthrough_count(const OakEngineNode *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeGroup *g =
		dynamic_cast<const olive::NodeGroup *>(impl(self));
	if (!g) {
		return OAKENGINE_E_INVALID;
	}
	return g->get_input_passthroughs().size();
}

int oakengine_group_add_input_passthrough(OakEngineNode *self,
										  OakEngineNode *inner_node,
										  const char *inner_input,
										  int inner_element,
										  const char *preferred_id,
										  char *out_id, int out_id_size)
{
	set_error(QString());
	olive::NodeGroup *g = dynamic_cast<olive::NodeGroup *>(impl(self));
	if (!g || !inner_node || !inner_input) {
		set_error(QStringLiteral("invalid arguments or not a group"));
		return OAKENGINE_E_INVALID;
	}
	const QString force_id = preferred_id ?
		QString::fromUtf8(preferred_id) : QString();
	const QString result = g->add_input_passthrough(
		olive::NodeInput(impl(inner_node),
						 QString::fromUtf8(inner_input), inner_element),
		force_id);
	// buf/size convention: string_to_buf returns the id length even when
	// out_id is NULL (NULL queries the length), so the return is > 0 for a
	// successfully added passthrough either way.
	return string_to_buf(result, out_id, out_id_size);
}

int oakengine_group_input_passthrough_at(const OakEngineNode *self,
										 int index, char *id, int id_size,
										 OakEngineNode **node,
										 char *input_id, int input_id_size,
										 int *element)
{
	set_error(QString());
	const olive::NodeGroup *g =
		dynamic_cast<const olive::NodeGroup *>(impl(self));
	if (!g) {
		set_error(QStringLiteral("not a group"));
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeGroup::InputPassthroughs &pts = g->get_input_passthroughs();
	if (index < 0 || index >= pts.size()) {
		set_error(QStringLiteral("passthrough index %1 out of range").arg(index));
		return OAKENGINE_E_INVALID;
	}
	const auto &pt = pts.at(index);
	int total = string_to_buf(pt.first, id, id_size);
	if (node) {
		*node = wrap(pt.second.node());
	}
	if (input_id) {
		total += string_to_buf(pt.second.input(), input_id, input_id_size);
	}
	if (element) {
		*element = pt.second.element();
	}
	return total;
}

int oakengine_group_get_id_of_passthrough(const OakEngineNode *self,
										  OakEngineNode *inner_node,
										  const char *inner_input,
										  int inner_element, char *id,
										  int id_size)
{
	set_error(QString());
	const olive::NodeGroup *g =
		dynamic_cast<const olive::NodeGroup *>(impl(self));
	if (!g || !inner_node || !inner_input) {
		set_error(QStringLiteral("invalid arguments or not a group"));
		return OAKENGINE_E_INVALID;
	}
	const QString result = g->get_id_of_passthrough(
		olive::NodeInput(impl(inner_node),
						 QString::fromUtf8(inner_input), inner_element));
	if (result.isEmpty()) {
		set_error(QStringLiteral("no passthrough for that node/input"));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (id) {
		return string_to_buf(result, id, id_size);
	}
	return 0;
}

int oakengine_group_get_passthrough_from_id(const OakEngineNode *self,
											const char *id,
											OakEngineNode **out_node,
											char *out_input,
											int out_input_size,
											int *out_element)
{
	set_error(QString());
	const olive::NodeGroup *g =
		dynamic_cast<const olive::NodeGroup *>(impl(self));
	if (!g || !id) {
		set_error(QStringLiteral("invalid arguments or not a group"));
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeInput input = g->get_input_from_id(QString::fromUtf8(id));
	if (!input.is_valid()) {
		set_error(QStringLiteral("no passthrough with id \"%1\"").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (out_node) {
		*out_node = wrap(input.node());
	}
	if (out_input) {
		string_to_buf(input.input(), out_input, out_input_size);
	}
	if (out_element) {
		*out_element = input.element();
	}
	return OAKENGINE_OK;
}

OakEngineNode *oakengine_group_get_output_passthrough(
	const OakEngineNode *self)
{
	if (!self) {
		return nullptr;
	}
	const olive::NodeGroup *g =
		dynamic_cast<const olive::NodeGroup *>(impl(self));
	if (!g) {
		return nullptr;
	}
	return wrap(g->get_output_passthrough());
}

int oakengine_group_set_output_passthrough(OakEngineNode *self,
										   OakEngineNode *inner_node)
{
	set_error(QString());
	olive::NodeGroup *g = dynamic_cast<olive::NodeGroup *>(impl(self));
	if (!g) {
		set_error(QStringLiteral("not a group"));
		return OAKENGINE_E_INVALID;
	}
	g->set_output_passthrough(impl(inner_node));
	return OAKENGINE_OK;
}

OakEngineNode *oakengine_node_group_create(void)
{
	return wrap(new olive::NodeGroup());
}

int oakengine_node_group_get_inner(OakEngineNode **inout_node,
								   char *inout_input,
								   int inout_input_size,
								   int *inout_element)
{
	if (!inout_node || !*inout_node || !inout_input ||
		inout_input_size <= 0 || !inout_element) {
		return 0;
	}
	olive::Node *node = impl(*inout_node);
	olive::NodeGroup *group = dynamic_cast<olive::NodeGroup *>(node);
	if (!group) {
		return 0;
	}
	olive::NodeInput input(node, QString::fromUtf8(inout_input),
						   *inout_element);
	if (!olive::NodeGroup::get_inner(&input)) {
		return 0;
	}
	*inout_node = wrap(input.node());
	string_to_buf(input.input(), inout_input, inout_input_size);
	*inout_element = input.element();
	return 1;
}

int oakengine_group_resolve_input(const OakEngineNode *self, const char *id,
								  int element, OakEngineNode **out_node,
								  char *out_input, int out_input_size,
								  int *out_element)
{
	set_error(QString());
	if (!self || !id) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Node *node = impl(self);
	const QString pid = QString::fromUtf8(id);
	// For a group, resolve through the group's chain.
	if (dynamic_cast<const olive::NodeGroup *>(node)) {
		const olive::NodeInput resolved =
			olive::NodeGroup::resolve_input(
				olive::NodeInput(const_cast<olive::Node *>(node), pid, element));
		if (out_node) {
			*out_node = wrap(resolved.node());
		}
		if (out_input) {
			string_to_buf(resolved.input(), out_input, out_input_size);
		}
		if (out_element) {
			*out_element = resolved.element();
		}
		return OAKENGINE_OK;
	}
	// For a plain node, pass through unchanged.
	if (out_node) {
		*out_node = const_cast<OakEngineNode *>(self);
	}
	if (out_input) {
		string_to_buf(pid, out_input, out_input_size);
	}
	if (out_element) {
		*out_element = element;
	}
	return OAKENGINE_OK;
}

int oakengine_group_remove_input_passthrough(OakEngineNode *self,
											 OakEngineNode *inner_node,
											 const char *inner_input,
											 int inner_element)
{
	set_error(QString());
	olive::NodeGroup *g = dynamic_cast<olive::NodeGroup *>(impl(self));
	if (!g || !inner_node || !inner_input) {
		set_error(QStringLiteral("invalid arguments or not a group"));
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeInput input(impl(inner_node),
								 QString::fromUtf8(inner_input),
								 inner_element);
	if (!g->contains_input_passthrough(input)) {
		set_error(QStringLiteral("passthrough not found"));
		return OAKENGINE_E_NOT_FOUND;
	}
	g->remove_input_passthrough(input);
	return OAKENGINE_OK;
}

extern "C" void *oakengine_group_add_input_passthrough_command(
	OakEngineNode *self, OakEngineNode *inner_node,
	const char *inner_input, int inner_element,
	const char *preferred_id)
{
	olive::NodeGroup *g = dynamic_cast<olive::NodeGroup *>(impl(self));
	if (!g || !inner_node || !inner_input) {
		return nullptr;
	}
	const QString force_id = preferred_id ?
		QString::fromUtf8(preferred_id) : QString();
	return new olive::NodeGroupAddInputPassthrough(
		g, olive::NodeInput(impl(inner_node),
							QString::fromUtf8(inner_input),
							inner_element),
		force_id);
}

extern "C" void *oakengine_group_set_output_passthrough_command(
	OakEngineNode *self, OakEngineNode *inner_node)
{
	olive::NodeGroup *g = dynamic_cast<olive::NodeGroup *>(impl(self));
	if (!g || !inner_node) {
		return nullptr;
	}
	return new olive::NodeGroupSetOutputPassthrough(g, impl(inner_node));
}

int oakengine_group_add_input_passthrough_undoable(
	OakEngineNode *self, OakEngineNode *inner_node,
	const char *inner_input, int inner_element,
	const char *preferred_id)
{
	set_error(QString());
	void *cmd = oakengine_group_add_input_passthrough_command(
		self, inner_node, inner_input, inner_element, preferred_id);
	if (!cmd) {
		set_error(QStringLiteral("invalid arguments or not a group"));
		return OAKENGINE_E_INVALID;
	}
	push_or_run(static_cast<olive::UndoCommand *>(cmd),
				QStringLiteral("Add Input Passthrough"));
	return OAKENGINE_OK;
}

int oakengine_group_set_output_passthrough_undoable(
	OakEngineNode *self, OakEngineNode *inner_node)
{
	set_error(QString());
	void *cmd = oakengine_group_set_output_passthrough_command(self, inner_node);
	if (!cmd) {
		set_error(QStringLiteral("not a group"));
		return OAKENGINE_E_INVALID;
	}
	push_or_run(static_cast<olive::UndoCommand *>(cmd),
				QStringLiteral("Set Output Passthrough"));
	return OAKENGINE_OK;
}

/* ---- Multi-camera --------------------------------------------------------- */


const char *oakengine_multicam_input_current(void)
{
	static const char *s = "current_in";
	Q_UNUSED(olive::MultiCamNode::k_current_input);
	return s;
}

const char *oakengine_multicam_input_sources(void)
{
	static const char *s = "sources_in";
	Q_UNUSED(olive::MultiCamNode::k_sources_input);
	return s;
}

const char *oakengine_multicam_input_sequence(void)
{
	static const char *s = "sequence_in";
	Q_UNUSED(olive::MultiCamNode::k_sequence_input);
	return s;
}

const char *oakengine_multicam_input_sequence_type(void)
{
	static const char *s = "sequence_type_in";
	Q_UNUSED(olive::MultiCamNode::k_sequence_type_input);
	return s;
}

int oakengine_multicam_get_source_count(const OakEngineNode *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::MultiCamNode *m =
		dynamic_cast<const olive::MultiCamNode *>(impl(self));
	if (!m) {
		return OAKENGINE_E_INVALID;
	}
	return m->get_source_count();
}

int oakengine_multicam_get_rows_and_columns(int source_count, int *rows,
											int *cols)
{
	if (source_count < 0 || !rows || !cols) {
		return OAKENGINE_E_INVALID;
	}
	olive::MultiCamNode::get_rows_and_columns(source_count, rows, cols);
	return OAKENGINE_OK;
}

int oakengine_multicam_index_to_row_cols(int index, int rows, int cols,
										 int *out_row, int *out_col)
{
	if (index < 0 || rows < 1 || cols < 1 || !out_row || !out_col) {
		return OAKENGINE_E_INVALID;
	}
	olive::MultiCamNode::index_to_row_cols(index, rows, cols, out_row,
										   out_col);
	return OAKENGINE_OK;
}

int oakengine_multicam_rows_cols_to_index(int row, int col, int rows,
										  int cols)
{
	if (row < 0 || col < 0 || rows < 1 || cols < 1 ||
		row >= rows || col >= cols) {
		return OAKENGINE_E_INVALID;
	}
	return olive::MultiCamNode::rows_cols_to_index(row, col, rows, cols);
}

int oakengine_multicam_get_current_source(const OakEngineNode *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::MultiCamNode *m =
		dynamic_cast<const olive::MultiCamNode *>(impl(self));
	if (!m) {
		return OAKENGINE_E_INVALID;
	}
	return m->get_current_source();
}

int oakengine_shape_set_rect_undoable(OakEngineNode *node, double x, double y,
									  double w, double h,
									  const oak_video_params *video_params,
									  void *command)
{
	if (!node || !video_params || !command) {
		return OAKENGINE_E_INVALID;
	}
	olive::ShapeNodeBase *shape =
		dynamic_cast<olive::ShapeNodeBase *>(impl(node));
	if (!shape) {
		return OAKENGINE_E_INVALID;
	}
	olive::VideoParams params(
		video_params->width, video_params->height,
		olive::Rational(video_params->time_base_num, video_params->time_base_den),
		olive::PixelFormat::Format(video_params->format),
		olive::VideoParams::k_internal_channel_count,
		olive::Rational(video_params->pixel_aspect_num,
						video_params->pixel_aspect_den),
		static_cast<olive::VideoParams::Interlacing>(video_params->interlacing),
		video_params->divider > 0 ? video_params->divider : 1);
	shape->set_rect(QRectF(x, y, w, h), params,
					static_cast<olive::MultiUndoCommand *>(command));
	return OAKENGINE_OK;
}

const char *oakengine_subtitle_text_input_id(void)
{
	static const char *s = "text_in";
	Q_UNUSED(olive::SubtitleBlock::k_text_in);
	return s;
}

int oakengine_subtitle_get_text(const OakEngineNode *node, char *buf,
								int buf_size)
{
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	const olive::SubtitleBlock *sub =
		dynamic_cast<const olive::SubtitleBlock *>(impl(node));
	if (!sub) {
		return OAKENGINE_E_INVALID;
	}
	const QByteArray utf8 = sub->get_text().toUtf8();
	const int len = int(utf8.size());
	if (buf && buf_size > 0) {
		const int n = qMin(len, buf_size - 1);
		if (n > 0) {
			std::memcpy(buf, utf8.constData(), size_t(n));
		}
		buf[n] = '\0';
	}
	return len;
}

int oakengine_subtitle_set_text(OakEngineNode *node, const char *text)
{
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	olive::SubtitleBlock *sub = dynamic_cast<olive::SubtitleBlock *>(impl(node));
	if (!sub) {
		return OAKENGINE_E_INVALID;
	}
	sub->set_text(QString::fromUtf8(text ? text : ""));
	return OAKENGINE_OK;
}

/* ---- Bulk graph deletion -------------------------------------------------- */

int oakengine_nodes_delete_many(
	OakEngineNode *const *nodes, OakEngineNode *const *contexts,
	int node_count, OakEngineNode *const *edge_outputs,
	OakEngineNode *const *edge_input_nodes,
	const char *const *edge_input_ids,
	const int *edge_input_elements, int edge_count)
{
	return oakengine_nodes_delete_many_ex(
		nodes, contexts, node_count, edge_outputs, edge_input_nodes,
		edge_input_ids, edge_input_elements, edge_count, nullptr, nullptr,
		nullptr, nullptr, 0);
}

int oakengine_nodes_delete_many_ex(
	OakEngineNode *const *nodes, OakEngineNode *const *contexts,
	int node_count, OakEngineNode *const *edge_outputs,
	OakEngineNode *const *edge_input_nodes,
	const char *const *edge_input_ids,
	const int *edge_input_elements, int edge_count,
	OakEngineNode *const *reconnect_outputs,
	OakEngineNode *const *reconnect_input_nodes,
	const char *const *reconnect_input_ids,
	const int *reconnect_input_elements, int reconnect_count)
{
	set_error(QString());
	if (node_count <= 0 && edge_count <= 0) {
		set_error(QStringLiteral("nothing to delete"));
		return OAKENGINE_E_INVALID;
	}
	if (node_count > 0 && !nodes) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	auto *command = new olive::MultiUndoCommand();
	auto *dc = new olive::NodeViewDeleteCommand();
	command->add_child(dc);
	for (int i = 0; i < node_count; i++) {
		if (!nodes[i]) {
			set_error(QStringLiteral("null node at index %1").arg(i));
			delete command;
			return OAKENGINE_E_INVALID;
		}
		olive::Node *ctx = nullptr;
		if (contexts && contexts[i]) {
			ctx = impl(contexts[i]);
		}
		dc->add_node(impl(nodes[i]), ctx);
	}
	for (int i = 0; i < edge_count; i++) {
		if (!edge_outputs[i] || !edge_input_nodes[i] || !edge_input_ids[i]) {
			set_error(QStringLiteral("invalid edge at index %1").arg(i));
			delete command;
			return OAKENGINE_E_INVALID;
		}
		dc->add_edge(
			impl(edge_outputs[i]),
			olive::NodeInput(impl(edge_input_nodes[i]),
							 QString::fromUtf8(edge_input_ids[i]),
							 edge_input_elements ? edge_input_elements[i] : -1));
	}
	// Reconnect edges run AFTER the deletion inside the same command, so
	// they may target inputs that were occupied by the deleted nodes.
	for (int i = 0; i < reconnect_count; i++) {
		if (!reconnect_outputs[i] || !reconnect_input_nodes[i] ||
			!reconnect_input_ids[i]) {
			set_error(QStringLiteral("invalid reconnect edge at index %1").arg(i));
			delete command;
			return OAKENGINE_E_INVALID;
		}
		command->add_child(new olive::NodeEdgeAddCommand(
			impl(reconnect_outputs[i]),
			olive::NodeInput(impl(reconnect_input_nodes[i]),
							 QString::fromUtf8(reconnect_input_ids[i]),
							 reconnect_input_elements
								 ? reconnect_input_elements[i]
								 : -1)));
	}
	push_or_run(command, QStringLiteral("Delete Nodes"));
	return OAKENGINE_OK;
}

/* ---- Keyframe best type at time ------------------------------------------- */


int oakengine_node_keyframe_best_type_at_time(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, int default_type)
{
	set_error(QString());
	if (!self || !input_id) {
		return default_type;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	if (!node->inputs().contains(id)) {
		return default_type;
	}
	olive::NodeInputImmediate *imm =
		const_cast<olive::Node *>(node)->get_immediate(id, element);
	if (!imm) {
		return default_type;
	}
	const olive::Rational tb = project_time_base(node);
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	const olive::NodeKeyframe::Type best =
		imm->get_best_keyframe_type_for_time(time, track);
	return from_engine_easing(best);
}

/* ---- Handle-based keyframe API -------------------------------------------- */

// Keyframe handle wrappers.
namespace {

const olive::NodeKeyframe *impl_kf_const(const OakEngineKeyframe *h)
{
	return reinterpret_cast<const olive::NodeKeyframe *>(h);
}

olive::NodeKeyframe *impl_kf(OakEngineKeyframe *h)
{
	return reinterpret_cast<olive::NodeKeyframe *>(h);
}

OakEngineKeyframe *wrap_kf(olive::NodeKeyframe *k)
{
	return reinterpret_cast<OakEngineKeyframe *>(k);
}

} // namespace

// Map oak_node_value_type -> olive::NodeValue::Type (reverse of to_c_type).
namespace {
olive::NodeValue::Type from_c_type(int t)
{
	switch (t) {
	case OAK_NODE_VALUE_NONE: return olive::NodeValue::k_none;
	case OAK_NODE_VALUE_INT: return olive::NodeValue::k_int;
	case OAK_NODE_VALUE_FLOAT: return olive::NodeValue::k_float;
	case OAK_NODE_VALUE_BOOL: return olive::NodeValue::k_boolean;
	case OAK_NODE_VALUE_RATIONAL: return olive::NodeValue::k_rational;
	case OAK_NODE_VALUE_COLOR: return olive::NodeValue::k_color;
	case OAK_NODE_VALUE_VEC2: return olive::NodeValue::k_vec2;
	case OAK_NODE_VALUE_VEC3: return olive::NodeValue::k_vec3;
	case OAK_NODE_VALUE_VEC4: return olive::NodeValue::k_vec4;
	case OAK_NODE_VALUE_COMBO: return olive::NodeValue::k_combo;
	case OAK_NODE_VALUE_STRING: return olive::NodeValue::k_file;
	case OAK_NODE_VALUE_TEXT: return olive::NodeValue::k_text;
	case OAK_NODE_VALUE_FONT: return olive::NodeValue::k_font;
	case OAK_NODE_VALUE_STR_COMBO: return olive::NodeValue::k_str_combo;
	case OAK_NODE_VALUE_BINARY: return olive::NodeValue::k_binary;
	case OAK_NODE_VALUE_BEZIER: return olive::NodeValue::k_bezier;
	case OAK_NODE_VALUE_TEXTURE: return olive::NodeValue::k_texture;
	case OAK_NODE_VALUE_SAMPLES: return olive::NodeValue::k_samples;
	case OAK_NODE_VALUE_VIDEO_PARAMS: return olive::NodeValue::k_video_params;
	case OAK_NODE_VALUE_AUDIO_PARAMS: return olive::NodeValue::k_audio_params;
	default: return olive::NodeValue::k_none;
	}
}
} // namespace

int oakengine_node_keyframe_track_count(const OakEngineNode *self,
										const char *input_id, int element)
{
	if (!self || !input_id) {
		return 0;
	}
	return impl(self)->get_number_of_keyframe_tracks(
		QString::fromUtf8(input_id));
}

int oakengine_node_keyframe_count_on_track(const OakEngineNode *self,
										   const char *input_id, int element,
										   int track)
{
	if (!self || !input_id) {
		return 0;
	}
	const QVector<olive::NodeKeyframeTrack> &tracks =
		impl(self)->get_keyframe_tracks(QString::fromUtf8(input_id), element);
	if (track < 0 || track >= tracks.size()) {
		return 0;
	}
	return tracks.at(track).size();
}

int oakengine_node_keyframes_toggle_at_time(OakEngineNode *self,
											const char *input_id,
											int element, int64_t time_ts,
											int track, int on,
											const char *undo_name)
{
	set_error(QString());
	olive::Node *node = impl(self);
	const olive::NodeValue::Type declared =
		checked_keyframe_input(node, input_id);
	if (declared == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	const QString id = QString::fromUtf8(input_id);
	// Facade contract: time_ts is in SECONDS and track is 1-based
	// (track=1 addresses the first track).
	const olive::Rational time(time_ts);
	const int track_index = track - 1;
	const QString name = undo_name ?
		QString::fromUtf8(undo_name) : QStringLiteral("Toggle Keyframe");

	if (on) {
		// Check if a keyframe already exists; if so, no-op.
		olive::NodeKeyframe *existing =
			node->get_keyframe_at_time_on_track(id, time, track_index,
												element);
		if (existing) {
			return OAKENGINE_OK;
		}
		// Create keyframe with current value and best type.
		const QVariant cv = node->get_value_at_time(id, time, element);
		olive::NodeKeyframe *key = new olive::NodeKeyframe(
			time, cv, olive::NodeKeyframe::k_default_type, track_index,
			element, id);
		olive::MultiUndoCommand *cmd = new olive::MultiUndoCommand();
		if (!node->is_input_keyframing(id, element)) {
			cmd->add_child(new olive::NodeParamSetKeyframingCommand(
				olive::NodeInput(node, id, element), true));
		}
		cmd->add_child(new olive::NodeParamInsertKeyframeCommand(node, key));
		push_or_run(cmd, name);
	} else {
		// Remove keyframe if it exists.
		olive::NodeKeyframe *existing =
			node->get_keyframe_at_time_on_track(id, time, track_index,
												element);
		if (!existing) {
			return OAKENGINE_OK; // no-op
		}
		push_or_run(new olive::NodeParamRemoveKeyframeCommand(existing), name);
	}
	return OAKENGINE_OK;
}

int oakengine_node_has_keyframe_at_time(const OakEngineNode *self,
										const char *input_id, int element,
										int64_t time_ts, int track)
{
	if (!self || !input_id) {
		return 0;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	// Facade contract: time_ts is in SECONDS and track is 1-based.
	return node->get_keyframe_at_time_on_track(id, olive::Rational(time_ts),
											   track - 1, element) ?
		   1 : 0;
}

int oakengine_node_keyframe_earliest_time(const OakEngineNode *self,
										  const char *input_id, int element,
										  int64_t *num, int64_t *den)
{
	set_error(QString());
	if (!self || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return 0;
	}
	olive::NodeInputImmediate *imm =
		const_cast<olive::Node *>(impl(self))->get_immediate(
			QString::fromUtf8(input_id), element);
	if (!imm) {
		set_error(QStringLiteral("no keyframe tracks"));
		return 0;
	}
	const olive::NodeKeyframe *earliest = imm->get_earliest_keyframe();
	if (!earliest) {
		if (num) *num = 0;
		if (den) *den = 1;
		return 0;
	}
	const olive::Rational &time = earliest->time();
	if (num) *num = time.numerator();
	if (den) *den = time.denominator();
	return 1;
}

int oakengine_node_keyframe_latest_time(const OakEngineNode *self,
										const char *input_id, int element,
										int64_t *num, int64_t *den)
{
	set_error(QString());
	if (!self || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return 0;
	}
	olive::NodeInputImmediate *imm =
		const_cast<olive::Node *>(impl(self))->get_immediate(
			QString::fromUtf8(input_id), element);
	if (!imm) {
		set_error(QStringLiteral("no keyframe tracks"));
		return 0;
	}
	const olive::NodeKeyframe *latest = imm->get_latest_keyframe();
	if (!latest) {
		if (num) *num = 0;
		if (den) *den = 1;
		return 0;
	}
	const olive::Rational &time = latest->time();
	if (num) *num = time.numerator();
	if (den) *den = time.denominator();
	return 1;
}

int oakengine_node_keyframe_closest_time_before(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, int64_t *num, int64_t *den)
{
	set_error(QString());
	if (!self || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return 0;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	// Facade contract: time_ts is in SECONDS and track is 1-based.
	const olive::Rational time(time_ts);
	const int track_index = track - 1;
	olive::NodeInputImmediate *imm =
		const_cast<olive::Node *>(node)->get_immediate(id, element);
	if (!imm) {
		return 0;
	}
	// Walk the track to find the closest keyframe before the given time.
	const QVector<olive::NodeKeyframeTrack> &tracks =
		node->get_keyframe_tracks(id, element);
	if (track_index < 0 || track_index >= tracks.size()) {
		return 0;
	}
	const olive::NodeKeyframe *found = nullptr;
	for (const olive::NodeKeyframe *key : tracks.at(track_index)) {
		if (key->time() < time) {
			if (!found || key->time() > found->time()) {
				found = key;
			}
		}
	}
	if (!found) {
		return 0;
	}
	if (num) *num = found->time().numerator();
	if (den) *den = found->time().denominator();
	return 1;
}

int oakengine_node_keyframe_closest_time_after(
	const OakEngineNode *self, const char *input_id, int element,
	int64_t time_ts, int track, int64_t *num, int64_t *den)
{
	set_error(QString());
	if (!self || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return 0;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	// Facade contract: time_ts is in SECONDS and track is 1-based.
	const olive::Rational time(time_ts);
	const int track_index = track - 1;
	const QVector<olive::NodeKeyframeTrack> &tracks =
		node->get_keyframe_tracks(id, element);
	if (track_index < 0 || track_index >= tracks.size()) {
		return 0;
	}
	const olive::NodeKeyframe *found = nullptr;
	for (const olive::NodeKeyframe *key : tracks.at(track_index)) {
		if (key->time() > time) {
			if (!found || key->time() < found->time()) {
				found = key;
			}
		}
	}
	if (!found) {
		return 0;
	}
	if (num) *num = found->time().numerator();
	if (den) *den = found->time().denominator();
	return 1;
}

OakEngineKeyframe *oakengine_node_keyframe_handle_on_track(
	const OakEngineNode *self, const char *input_id, int element,
	int track, int index)
{
	if (!self || !input_id) {
		return nullptr;
	}
	const QVector<olive::NodeKeyframeTrack> &tracks =
		impl(self)->get_keyframe_tracks(QString::fromUtf8(input_id), element);
	if (track < 0 || track >= tracks.size()) {
		return nullptr;
	}
	const olive::NodeKeyframeTrack &tr = tracks.at(track);
	if (index < 0 || index >= tr.size()) {
		return nullptr;
	}
	return wrap_kf(tr.at(index));
}

OakEngineKeyframe *oakengine_node_keyframe_handle_at_time(
	const OakEngineNode *self, const char *input_id, int element,
	int track, int64_t time_num, int64_t time_den)
{
	if (!self || !input_id) {
		return nullptr;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	olive::NodeKeyframe *key = node->get_keyframe_at_time_on_track(
		id, olive::Rational(int(time_num), int(time_den)), track, element);
	return wrap_kf(key);
}

int oakengine_node_keyframes_at_time(const OakEngineNode *self,
									 const char *input_id, int element,
									 int64_t time_num, int64_t time_den,
									 OakEngineKeyframe **out_handles,
									 int max_handles)
{
	if (!self || !input_id || !out_handles || max_handles <= 0) {
		return 0;
	}
	const olive::Node *node = impl(self);
	const QString id = QString::fromUtf8(input_id);
	const olive::Rational t{int(time_num), int(time_den)};
	olive::NodeInputImmediate *imm =
		const_cast<olive::Node *>(node)->get_immediate(id, element);
	if (!imm) {
		return 0;
	}
	const QVector<olive::NodeKeyframe *> at_time =
		imm->get_keyframe_at_time(t);
	const int n = qMin(at_time.size(), max_handles);
	for (int i = 0; i < n; i++) {
		out_handles[i] = wrap_kf(at_time[i]);
	}
	return n;
}

int oakengine_node_set_input_keyframing(OakEngineNode *self,
										const char *input_id, int element,
										int keyframing, int track,
										int enable_all_tracks,
										const char *undo_name)
{
	set_error(QString());
	(void)track;
	(void)enable_all_tracks;
	olive::Node *node = impl(self);
	const olive::NodeValue::Type declared =
		checked_keyframe_input(node, input_id);
	if (declared == olive::NodeValue::k_none) {
		return self && input_id ? OAKENGINE_E_NOT_FOUND : OAKENGINE_E_INVALID;
	}
	const QString id = QString::fromUtf8(input_id);
	const bool already = node->is_input_keyframing(id, element);
	const QString name = undo_name ?
		QString::fromUtf8(undo_name) : QStringLiteral("Set Keyframing");

	if (keyframing && already) {
		// Already enabled: redundant enable is a no-op success.
		return OAKENGINE_OK;
	}

	// Facade contract (see oakengine/node.h): enabling keyframing also seeds
	// one default-type keyframe per track (at t=0 with the current split
	// standard value); disabling removes every keyframe on every track.
	// Both are ONE undoable command.
	auto *command = new olive::MultiUndoCommand();
	const olive::NodeInput input(node, id, element);
	if (keyframing) {
		command->add_child(
			new olive::NodeParamSetKeyframingCommand(input, true));
		const int tracks =
			olive::NodeValue::get_number_of_keyframe_tracks(declared);
		const olive::SplitValue values =
			node->get_split_standard_value(id, element);
		for (int t = 0; t < tracks; t++) {
			const QVariant v = t < values.size() ? values.at(t) : QVariant();
			command->add_child(new olive::NodeParamInsertKeyframeCommand(
				node, new olive::NodeKeyframe(
						  olive::Rational(0), v,
						  olive::NodeKeyframe::k_default_type, t, element,
						  id)));
		}
	} else {
		const QVector<olive::NodeKeyframeTrack> &tracks =
			node->get_keyframe_tracks(id, element);
		for (const olive::NodeKeyframeTrack &tr : tracks) {
			for (olive::NodeKeyframe *key : tr) {
				command->add_child(
					new olive::NodeParamRemoveKeyframeCommand(key));
			}
		}
		command->add_child(
			new olive::NodeParamSetKeyframingCommand(input, false));
	}
	push_or_run(command, name);
	return OAKENGINE_OK;
}

extern "C" void *oakengine_node_set_input_keyframing_command(
	OakEngineNode *self, const char *input_id, int element, int keyframing)
{
	if (!self || !input_id) {
		return nullptr;
	}
	olive::Node *node = impl(self);
	if (!node->inputs().contains(QString::fromUtf8(input_id))) {
		return nullptr;
	}
	return new olive::NodeParamSetKeyframingCommand(
		olive::NodeInput(node, QString::fromUtf8(input_id), element),
		keyframing != 0);
}

int oakengine_node_keyframes_paste(OakEngineNode *self,
								   OakEngineKeyframe *const *keyframes,
								   int count, const char *undo_name)
{
	set_error(QString());
	if (!self || !keyframes || count <= 0) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::Node *node = impl(self);
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	const QString name = undo_name ?
		QString::fromUtf8(undo_name) : QStringLiteral("Paste Keyframes");
	for (int i = 0; i < count; i++) {
		if (!keyframes[i]) {
			set_error(QStringLiteral("null keyframe at index %1").arg(i));
			delete command;
			return OAKENGINE_E_INVALID;
		}
		olive::NodeKeyframe *src = impl_kf(keyframes[i]);
		auto *clone = new olive::NodeKeyframe(src->time(), src->value(),
											  src->type(), src->track(),
											  src->element(), src->input());
		clone->set_bezier_control_in(src->bezier_control_in());
		clone->set_bezier_control_out(src->bezier_control_out());
		if (!node->is_input_keyframing(src->input(), src->element())) {
			command->add_child(new olive::NodeParamSetKeyframingCommand(
				olive::NodeInput(node, src->input(), src->element()), true));
		}
		command->add_child(
			new olive::NodeParamInsertKeyframeCommand(node, clone));
	}
	push_or_run(command, name);
	return OAKENGINE_OK;
}

/* ---- OakEngineKeyframe accessors ------------------------------------------ */

int oakengine_keyframe_get_time(const OakEngineKeyframe *self, int64_t *num,
								int64_t *den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Rational &time = impl_kf_const(self)->time();
	if (num) {
		*num = time.numerator();
	}
	if (den) {
		*den = time.denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_keyframe_get_input_id(const OakEngineKeyframe *self, char *buf,
									int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl_kf_const(self)->input(), buf, buf_size);
}

int oakengine_keyframe_get_track(const OakEngineKeyframe *self)
{
	if (!self) {
		return -1;
	}
	return impl_kf_const(self)->track();
}

int oakengine_keyframe_get_element(const OakEngineKeyframe *self)
{
	if (!self) {
		return -1;
	}
	return impl_kf_const(self)->element();
}

OakEngineNode *oakengine_keyframe_get_node(const OakEngineKeyframe *self)
{
	if (!self) {
		return nullptr;
	}
	return wrap(impl_kf_const(self)->parent());
}

int oakengine_keyframe_get_type(const OakEngineKeyframe *self)
{
	if (!self) {
		return -1;
	}
	return from_engine_easing(impl_kf_const(self)->type());
}

int oakengine_keyframe_default_type(void)
{
	return from_engine_easing(olive::NodeKeyframe::k_default_type);
}

int oakengine_keyframe_opposing_bezier_type(int type)
{
	return int(olive::NodeKeyframe::get_opposing_bezier_type(
		olive::NodeKeyframe::BezierType(type)));
}

int oakengine_keyframe_get_value(const OakEngineKeyframe *self,
								 oak_node_value *out)
{
	set_error(QString());
	if (!self || !out) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeKeyframe *key = impl_kf_const(self);
	// Determine the type from the key's input on the parent node.
	const olive::Node *parent = key->parent();
	const QString &input_id = key->input();
	olive::NodeValue::Type type = olive::NodeValue::k_float;
	if (parent && parent->inputs().contains(input_id)) {
		type = parent->get_input_data_type(input_id);
	}
	return kf_value_to_c(type, key->value(), out) ?
		OAKENGINE_OK : OAKENGINE_E_INVALID;
}

// Convert a single track's component QVariant into the POD, mirroring the
// application's NodeTrackComponentToOakNodeValue helper (per-track scalar in
// f[0]/num with the input's declared type).
static bool track_component_to_pod(olive::NodeValue::Type type,
								   const QVariant &v, oak_node_value *out)
{
	memset(out, 0, sizeof(*out));
	switch (type) {
	case olive::NodeValue::k_int:
		out->type = OAK_NODE_VALUE_INT;
		out->num = v.toLongLong();
		return true;
	case olive::NodeValue::k_combo:
		out->type = OAK_NODE_VALUE_COMBO;
		out->num = v.toLongLong();
		return true;
	case olive::NodeValue::k_float:
	case olive::NodeValue::k_bezier:
		out->type = OAK_NODE_VALUE_FLOAT;
		out->f[0] = v.toDouble();
		return true;
	case olive::NodeValue::k_boolean:
		out->type = OAK_NODE_VALUE_BOOL;
		out->num = v.toBool() ? 1 : 0;
		return true;
	case olive::NodeValue::k_rational: {
		const olive::Rational r = v.value<olive::Rational>();
		out->type = OAK_NODE_VALUE_RATIONAL;
		out->num = r.numerator();
		out->den = r.denominator();
		return true;
	}
	case olive::NodeValue::k_color:
		out->type = OAK_NODE_VALUE_COLOR;
		out->f[0] = v.toFloat();
		return true;
	case olive::NodeValue::k_vec2:
		out->type = OAK_NODE_VALUE_VEC2;
		out->f[0] = v.toFloat();
		return true;
	case olive::NodeValue::k_vec3:
		out->type = OAK_NODE_VALUE_VEC3;
		out->f[0] = v.toFloat();
		return true;
	case olive::NodeValue::k_vec4:
		out->type = OAK_NODE_VALUE_VEC4;
		out->f[0] = v.toFloat();
		return true;
	default:
		return false;
	}
}

int oakengine_keyframe_compute_paste_value(OakEngineNode *target_node,
										   OakEngineKeyframe *keyframe,
										   oak_node_value *out)
{
	set_error(QString());
	if (!target_node || !keyframe || !out) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::Node *node = impl(target_node);
	olive::NodeKeyframe *key = impl_kf(keyframe);
	const QString &input_id = key->input();
	if (!node->inputs().contains(input_id)) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(input_id));
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeValue::Type type = node->get_input_data_type(input_id);
	olive::SplitValue split = node->get_split_value_at_time(
		olive::NodeInput(node, input_id, key->element()), key->time());
	if (key->track() >= 0 && key->track() < split.size()) {
		split[key->track()] = key->value();
	}
	QVector<oak_node_value> tracks(split.size());
	for (int i = 0; i < split.size(); i++) {
		if (!track_component_to_pod(type, split.at(i), &tracks[i])) {
			set_error(QStringLiteral("unsupported value type"));
			return OAKENGINE_E_INVALID;
		}
	}
	return oakengine_node_value_combine_tracks(
		to_c_type(type), tracks.constData(), tracks.size(), out);
}

int oakengine_keyframe_has_sibling_at_time(const OakEngineKeyframe *self,
										   int64_t time_ts, int track)
{
	if (!self) {
		return 0;
	}
	const olive::NodeKeyframe *key = impl_kf_const(self);
	const olive::Rational time(track, 1); // fallback
	return key->has_sibling_at_time(olive::Rational(time_ts, 1)) ? 1 : 0;
}

int oakengine_keyframe_set_bezier_point_live(OakEngineKeyframe *self,
											 int point_index, double x,
											 double y)
{
	set_error(QString());
	if (!self || point_index < 0 || point_index > 1) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::NodeKeyframe *key = impl_kf(self);
	const olive::NodeKeyframe::BezierType mode =
		(point_index == 0) ? olive::NodeKeyframe::k_in_handle :
							 olive::NodeKeyframe::k_out_handle;
	key->set_bezier_control(mode, QPointF(x, y));
	return OAKENGINE_OK;
}

int oakengine_keyframe_get_bezier_point(const OakEngineKeyframe *self,
										int point_index, double *x,
										double *y)
{
	set_error(QString());
	if (!self || !x || !y || point_index < 0 || point_index > 1) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeKeyframe *key = impl_kf_const(self);
	const olive::NodeKeyframe::BezierType mode =
		(point_index == 0) ? olive::NodeKeyframe::k_in_handle :
							 olive::NodeKeyframe::k_out_handle;
	const QPointF p = key->bezier_control(mode);
	*x = p.x();
	*y = p.y();
	return OAKENGINE_OK;
}

int oakengine_keyframe_get_valid_bezier_point(const OakEngineKeyframe *self,
											  int point_index, double *x,
											  double *y)
{
	set_error(QString());
	if (!self || !x || !y || point_index < 0 || point_index > 1) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::NodeKeyframe *key = impl_kf_const(self);
	const QPointF p = (point_index == 0) ?
		key->valid_bezier_control_in() :
		key->valid_bezier_control_out();
	*x = p.x();
	*y = p.y();
	return OAKENGINE_OK;
}

int oakengine_keyframe_set_value_live(OakEngineKeyframe *self,
									  const oak_node_value *value)
{
	set_error(QString());
	if (!self || !value) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::NodeKeyframe *key = impl_kf(self);
	// Map the POD to a QVariant. Use key's parent node to determine type.
	const olive::Node *parent = key->parent();
	const QString &input_id = key->input();
	olive::NodeValue::Type type = olive::NodeValue::k_float;
	if (parent && parent->inputs().contains(input_id)) {
		type = parent->get_input_data_type(input_id);
	}
	QVariant qv;
	if (!component_from_c(value, type, 0, &qv)) {
		set_error(QStringLiteral("value type mismatch"));
		return OAKENGINE_E_INVALID;
	}
	key->set_value(qv);
	return OAKENGINE_OK;
}

int oakengine_keyframe_set_time_live(OakEngineKeyframe *self, int64_t num,
									 int64_t den)
{
	set_error(QString());
	if (!self) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	impl_kf(self)->set_time(olive::Rational(int(num), int(den)));
	return OAKENGINE_OK;
}

int oakengine_keyframes_remove_many(OakEngineKeyframe *const *keyframes,
									int count, const char *undo_name)
{
	set_error(QString());
	if (!keyframes || count <= 0) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	// Check for NULL entries first.
	for (int i = 0; i < count; i++) {
		if (!keyframes[i]) {
			set_error(QStringLiteral("null keyframe at index %1").arg(i));
			return OAKENGINE_E_INVALID;
		}
	}
	const QString name = undo_name ?
		QString::fromUtf8(undo_name) : QStringLiteral("Remove Keyframes");
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	for (int i = 0; i < count; i++) {
		command->add_child(
			new olive::NodeParamRemoveKeyframeCommand(impl_kf(keyframes[i])));
	}
	push_or_run(command, name);
	return OAKENGINE_OK;
}

OakEngineKeyframe *oakengine_keyframe_create(
	OakEngineNode *node, const char *input_id, int element, int track,
	int64_t time_ts, int type, const oak_node_value *value,
	int64_t duration_ts)
{
	set_error(QString());
	(void)duration_ts;
	if (!node || !input_id || !value) {
		set_error(QStringLiteral("invalid arguments"));
		return nullptr;
	}
	olive::Node *n = impl(node);
	const QString id = QString::fromUtf8(input_id);
	if (!n->inputs().contains(id)) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(id));
		return nullptr;
	}
	const olive::Rational tb = project_time_base(impl(node));
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	const olive::NodeValue::Type declared = n->get_input_data_type(id);
	QVariant engine_value;
	if (!component_from_c(value, declared, 0, &engine_value)) {
		set_error(QStringLiteral("value type mismatch for \"%1\"").arg(id));
		return nullptr;
	}
	auto *key = new olive::NodeKeyframe(time, engine_value,
										to_engine_easing(type),
										track, element, id);
	return wrap_kf(key);
}

void oakengine_keyframe_dispose(OakEngineKeyframe *keyframe)
{
	if (!keyframe) {
		return;
	}
	delete impl_kf(keyframe);
}

/* ---- Input dragger -------------------------------------------------------- */

struct OakEngineNodeDraggerImpl {
	olive::Node *node;
	QString input_id;
	int element;
	int track;
	int64_t time_ts;
	bool started;
	int keys_before;
};

OakEngineNodeDragger *oakengine_dragger_create(OakEngineNode *node,
											   const char *input_id,
											   int element, int track)
{
	set_error(QString());
	if (!node || !input_id) {
		set_error(QStringLiteral("invalid arguments"));
		return nullptr;
	}
	olive::Node *n = impl(node);
	const QString id = QString::fromUtf8(input_id);
	if (!n->inputs().contains(id)) {
		set_error(QStringLiteral("unknown input id \"%1\"").arg(id));
		return nullptr;
	}
	auto *d = new OakEngineNodeDraggerImpl();
	d->node = n;
	d->input_id = id;
	d->element = element;
	d->track = track;
	d->time_ts = 0;
	d->started = false;
	d->keys_before = 0;
	return reinterpret_cast<OakEngineNodeDragger *>(d);
}

int oakengine_dragger_start(OakEngineNodeDragger *self, int64_t time_ts,
							int track, int insert_on_all_tracks)
{
	set_error(QString());
	if (!self) {
		set_error(QStringLiteral("invalid dragger"));
		return OAKENGINE_E_INVALID;
	}
	auto *d = reinterpret_cast<OakEngineNodeDraggerImpl *>(self);
	if (d->started) {
		set_error(QStringLiteral("dragger already started"));
		return OAKENGINE_E_STATE;
	}
	d->time_ts = time_ts;
	d->track = track;
	d->keys_before = olive::NodeInput(d->node, d->input_id, d->element)
						 .get_array_size();
	// Create a keyframe at the drag time by calling set_value_at_time.
	// We need a temporary value; the dragger will live-set it.
	// Facade contract: time_ts is a frame timestamp and track is 1-based.
	const olive::Rational tb = project_time_base(d->node);
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(d->time_ts, tb);
	const QVariant cv = d->node->get_value_at_time(d->input_id, time,
												   d->element);
	olive::MultiUndoCommand *temp_cmd = new olive::MultiUndoCommand();
	olive::Node::set_value_at_time(
		olive::NodeInput(d->node, d->input_id, d->element),
		time, cv, track - 1, temp_cmd, insert_on_all_tracks != 0);
	// Execute the command immediately (we'll push the final command at end).
	temp_cmd->redo_now();
	delete temp_cmd;
	d->started = true;
	return OAKENGINE_OK;
}

int oakengine_dragger_drag(OakEngineNodeDragger *self,
						   const oak_node_value *value)
{
	set_error(QString());
	if (!self || !value) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	auto *d = reinterpret_cast<OakEngineNodeDraggerImpl *>(self);
	if (!d->started) {
		set_error(QStringLiteral("dragger not started"));
		return OAKENGINE_E_STATE;
	}
	// Facade contract: the stored time_ts is a frame timestamp and track is
	// 1-based.
	const olive::Rational tb = project_time_base(d->node);
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(d->time_ts, tb);
	const olive::NodeValue::Type declared =
		d->node->get_input_data_type(d->input_id);
	QVariant qv;
	if (!component_from_c(value, declared, 0, &qv)) {
		set_error(QStringLiteral("value type mismatch"));
		return OAKENGINE_E_INVALID;
	}
	// Live-set the keyframe value at the drag time.
	olive::NodeKeyframe *key = d->node->get_keyframe_at_time_on_track(
		d->input_id, time, d->track - 1, d->element);
	if (key) {
		key->set_value(qv);
	}
	return OAKENGINE_OK;
}

int oakengine_dragger_end(OakEngineNodeDragger *self, const char *undo_name)
{
	set_error(QString());
	if (!self) {
		set_error(QStringLiteral("invalid dragger"));
		return OAKENGINE_E_INVALID;
	}
	auto *d = reinterpret_cast<OakEngineNodeDraggerImpl *>(self);
	if (!d->started) {
		set_error(QStringLiteral("dragger not started"));
		return OAKENGINE_E_STATE;
	}
	// Push the single undo command that captures the entire drag.
	// Facade contract: the stored time_ts is a frame timestamp and track is
	// 1-based.
	const olive::Rational tb = project_time_base(d->node);
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(d->time_ts, tb);
	// Capture the dragged (final) value before touching the live key.
	const QVariant final_value = d->node->get_value_at_time(
		d->input_id, time, d->element);
	olive::NodeKeyframe *key = d->node->get_keyframe_at_time_on_track(
		d->input_id, time, d->track - 1, d->element);
	if (key) {
		// The start-created key lives on the node but NOT on the undo stack
		// (dragger_start executed it with redo_now). Remove it live and push
		// ONE undoable command that recreates it holding the final value, so
		// the whole drag is a single undo entry: undo removes the key
		// entirely (restoring the pre-drag keyframe count), redo re-creates
		// it with the final value.
		delete key;
		const QString name = undo_name ?
			QString::fromUtf8(undo_name) : QStringLiteral("Drag Input");
		olive::MultiUndoCommand *cmd = new olive::MultiUndoCommand();
		olive::Node::set_value_at_time(
			olive::NodeInput(d->node, d->input_id, d->element),
			time, final_value, d->track - 1, cmd, false);
		push_or_run(cmd, name);
	}
	d->started = false;
	return OAKENGINE_OK;
}

int oakengine_dragger_is_started(const OakEngineNodeDragger *self)
{
	if (!self) {
		return 0;
	}
	return reinterpret_cast<const OakEngineNodeDraggerImpl *>(self)->started ?
		   1 : 0;
}

void oakengine_dragger_free(OakEngineNodeDragger *self)
{
	if (!self) {
		return;
	}
	delete reinterpret_cast<OakEngineNodeDraggerImpl *>(self);
}

int oakengine_node_set_value_hint(OakEngineNode *self, const char *input_id,
								  int element, int type, int index,
								  const char *tag)
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
	// Validate type: must be a valid oak_node_value_type or -1.
	olive::NodeValue::Type nv_type = olive::NodeValue::k_none;
	if (type >= 0) {
		nv_type = from_c_type(type);
		if (nv_type == olive::NodeValue::k_none && type != 0) {
			set_error(QStringLiteral("invalid value type %1").arg(type));
			return OAKENGINE_E_INVALID;
		}
	}
	// An empty type list means "no preference" and falls back to the input's
	// declared type (NodeTraverser::generate_row_value_element_index);
	// OAK_NODE_VALUE_NONE (0) and -1 both leave the list empty.
	QVector<olive::NodeValue::Type> types;
	if (nv_type != olive::NodeValue::k_none) {
		types.append(nv_type);
	}
	olive::Node::ValueHint hint(types, index,
								QString::fromUtf8(tag ? tag : ""));
	node->set_value_hint_for_input(id, hint, element);
	return OAKENGINE_OK;
}

/* ---- Node static data and helpers ----------------------------------------- */

extern "C" const char *oakengine_node_enabled_input_id(void)
{
	return olive::Node::k_enabled_input.toUtf8().constData();
}

extern "C" const char *oakengine_volume_samples_input_id(void)
{
	return olive::VolumeNode::k_samples_input.toUtf8().constData();
}

extern "C" const char *oakengine_transform_texture_input_id(void)
{
	return olive::TransformDistortNode::k_texture_input.toUtf8().constData();
}

extern "C" const char *oakengine_transition_in_block_input_id(void)
{
	return olive::TransitionBlock::k_in_block_input.toUtf8().constData();
}

extern "C" const char *oakengine_transition_out_block_input_id(void)
{
	return olive::TransitionBlock::k_out_block_input.toUtf8().constData();
}

extern "C" double oakengine_audio_waveform_max_sample_rate(void)
{
	return olive::AudioVisualWaveform::k_maximum_sample_rate.to_double();
}

extern "C" int oakengine_node_category_name(int category_id, char *buf,
										   int buf_size)
{
	QString name = olive::Node::get_category_name(
		olive::Node::CategoryID(category_id));
	QByteArray utf8 = name.toUtf8();
	return string_to_buf(name, buf, buf_size);
}

extern "C" void *oakengine_node_link_command(OakEngineNode *a,
											OakEngineNode *b, int link)
{
	auto *na = reinterpret_cast<olive::Node *>(a);
	auto *nb = reinterpret_cast<olive::Node *>(b);
	if (!na || !nb) {
		return nullptr;
	}
	return new olive::NodeLinkCommand(na, nb, link != 0);
}

extern "C" OakEngineNode *oakengine_node_copy_in_graph(
	OakEngineNode *node, void *command)
{
	auto *n = reinterpret_cast<olive::Node *>(node);
	if (!n) {
		return nullptr;
	}
	olive::MultiUndoCommand *cmd = command
		? static_cast<olive::MultiUndoCommand *>(command) : nullptr;
	olive::Node *copy = olive::Node::copy_node_in_graph(n, cmd);
	if (!cmd && copy) {
		// If no parent command, the copy was made directly.
	}
	return reinterpret_cast<OakEngineNode *>(copy);
}

extern "C" int oakengine_node_copy_dependency_graph(
	OakEngineNode *const *nodes, OakEngineNode *const *copies, int count,
	void *command)
{
	if (!nodes || !copies || count <= 0) {
		return OAKENGINE_E_INVALID;
	}
	QList<olive::Node *> node_list;
	QList<olive::Node *> copy_list;
	for (int i = 0; i < count; i++) {
		node_list.append(reinterpret_cast<olive::Node *>(nodes[i]));
		copy_list.append(reinterpret_cast<olive::Node *>(copies[i]));
	}
	olive::MultiUndoCommand *cmd = command
		? static_cast<olive::MultiUndoCommand *>(command) : nullptr;
	olive::Node::copy_dependency_graph(node_list, copy_list, cmd);
	return OAKENGINE_OK;
}

extern "C" int oakengine_node_connect_command_string(
	OakEngineNode *output, OakEngineNode *input_node,
	const char *input_id, int element, char *buf, int buf_size)
{
	auto *out = reinterpret_cast<olive::Node *>(output);
	auto *in = reinterpret_cast<olive::Node *>(input_node);
	if (!out || !in || !input_id) {
		return OAKENGINE_E_INVALID;
	}
	olive::NodeInput ni(in, QString::fromUtf8(input_id), element);
	QString name = olive::Node::get_connect_command_string(out, ni);
	QByteArray utf8 = name.toUtf8();
	return string_to_buf(name, buf, buf_size);
}

extern "C" int oakengine_node_transform_time_to(
	OakEngineNode *from, OakEngineNode *to, int direction,
	int path_index, int64_t in_num, int64_t in_den,
	int64_t out_num, int64_t out_den,
	int64_t *result_in_num, int64_t *result_in_den,
	int64_t *result_out_num, int64_t *result_out_den)
{
	auto *f = reinterpret_cast<olive::Node *>(from);
	auto *t = reinterpret_cast<olive::Node *>(to);
	if (!f || !t) {
		return OAKENGINE_E_INVALID;
	}
	olive::TimeRange range(
		olive::Rational(in_num, in_den),
		olive::Rational(out_num, out_den));
	olive::TimeRange result = f->transform_time_to(
		range, t,
		static_cast<olive::Node::TransformTimeDirection>(direction),
		path_index);
	if (result_in_num) *result_in_num = result.in().numerator();
	if (result_in_den) *result_in_den = result.in().denominator();
	if (result_out_num) *result_out_num = result.out().numerator();
	if (result_out_den) *result_out_den = result.out().denominator();
	return OAKENGINE_OK;
}

/* ---- P1.1: NodeValue static methods (F class: 4 symbols) ---------------- */

int oakengine_node_value_keyframe_track_count(int c_type)
{
	olive::NodeValue::Type type = from_c_type(c_type);
	return olive::NodeValue::get_number_of_keyframe_tracks(type);
}

int oakengine_node_value_pretty_type_name(int c_type, char *buf, int buf_size)
{
	if (c_type <= OAK_NODE_VALUE_NONE ||
		c_type > OAK_NODE_VALUE_AUDIO_PARAMS) {
		return -1;
	}
	olive::NodeValue::Type type = from_c_type(c_type);
	QString name = olive::NodeValue::get_pretty_data_type_name(type);
	return string_to_buf(name, buf, buf_size);
}

int oakengine_node_value_split_to_tracks(int c_type,
	const oak_node_value *normal, oak_node_value *tracks_out, int track_count)
{
	if (!normal || !tracks_out || track_count <= 0) {
		return OAKENGINE_E_INVALID;
	}
	olive::NodeValue::Type type = from_c_type(c_type);
	QVariant v;
	switch (type) {
	case olive::NodeValue::k_int:
	case olive::NodeValue::k_combo:
		v = QVariant(static_cast<int>(normal->num));
		break;
	case olive::NodeValue::k_float:
		v = normal->f[0];
		break;
	case olive::NodeValue::k_boolean:
		v = normal->num != 0;
		break;
	case olive::NodeValue::k_rational:
		v = QVariant::fromValue(olive::Rational(normal->num, normal->den));
		break;
	case olive::NodeValue::k_color:
		v = QVariant::fromValue(olive::core::Color(
			normal->f[0], normal->f[1], normal->f[2], normal->f[3]));
		break;
	case olive::NodeValue::k_vec2:
		v = QVariant::fromValue(QVector2D(normal->f[0], normal->f[1]));
		break;
	case olive::NodeValue::k_vec3:
		v = QVariant::fromValue(QVector3D(normal->f[0], normal->f[1], normal->f[2]));
		break;
	case olive::NodeValue::k_vec4:
		v = QVariant::fromValue(QVector4D(normal->f[0], normal->f[1], normal->f[2], normal->f[3]));
		break;
	case olive::NodeValue::k_bezier:
		v = QVariant::fromValue(
			Bezier(normal->f[0], normal->f[1], normal->f[2], normal->f[3], normal->den, normal->num));
		break;
	default:
		return OAKENGINE_E_INVALID;
	}
	QVector<QVariant> split = olive::NodeValue::split_normal_value_into_track_values(type, v);
	int n = qMin(split.size(), track_count);
	for (int i = 0; i < n; ++i) {
		tracks_out[i].f[0] = 0;
		tracks_out[i].num = 0;
		tracks_out[i].den = 0;
		switch (type) {
		case olive::NodeValue::k_int:
			tracks_out[i].type = OAK_NODE_VALUE_INT;
			tracks_out[i].num = split[i].toLongLong();
			break;
		case olive::NodeValue::k_combo:
			tracks_out[i].type = OAK_NODE_VALUE_COMBO;
			tracks_out[i].num = split[i].toLongLong();
			break;
		case olive::NodeValue::k_boolean:
			tracks_out[i].type = OAK_NODE_VALUE_BOOL;
			tracks_out[i].num = split[i].toBool() ? 1 : 0;
			break;
		case olive::NodeValue::k_rational: {
			olive::Rational r = split[i].value<olive::Rational>();
			tracks_out[i].type = OAK_NODE_VALUE_RATIONAL;
			tracks_out[i].num = r.numerator();
			tracks_out[i].den = r.denominator();
			break;
		}
		default:
			tracks_out[i].type = OAK_NODE_VALUE_FLOAT;
			tracks_out[i].f[0] = split[i].toDouble();
			break;
		}
	}
	return OAKENGINE_OK;
}

int oakengine_node_value_combine_tracks(int c_type,
	const oak_node_value *tracks, int track_count, oak_node_value *normal_out)
{
	if (!tracks || !normal_out || track_count <= 0) {
		return OAKENGINE_E_INVALID;
	}
	olive::NodeValue::Type type = from_c_type(c_type);
	QVector<QVariant> split;
	split.reserve(track_count);
	for (int i = 0; i < track_count; ++i) {
		switch (type) {
		case olive::NodeValue::k_int:
		case olive::NodeValue::k_combo:
			split.append(QVariant(static_cast<int>(tracks[i].num)));
			break;
		case olive::NodeValue::k_boolean:
			split.append(tracks[i].num != 0);
			break;
		case olive::NodeValue::k_rational:
			split.append(QVariant::fromValue(
				olive::Rational(tracks[i].num, tracks[i].den)));
			break;
		default:
			split.append(tracks[i].f[0]);
			break;
		}
	}
	QVariant combined = olive::NodeValue::combine_track_values_into_normal_value(type, split);
	if (!normal_out) {
		return OAKENGINE_OK;
	}
	switch (type) {
	case olive::NodeValue::k_int:
	case olive::NodeValue::k_combo:
		normal_out->type = OAK_NODE_VALUE_INT;
		normal_out->num = combined.toLongLong();
		normal_out->f[0] = 0;
		break;
	case olive::NodeValue::k_float:
		normal_out->type = OAK_NODE_VALUE_FLOAT;
		normal_out->f[0] = combined.toFloat();
		normal_out->num = 0;
		break;
	case olive::NodeValue::k_boolean:
		normal_out->type = OAK_NODE_VALUE_BOOL;
		normal_out->num = combined.toBool() ? 1 : 0;
		normal_out->f[0] = 0;
		break;
	case olive::NodeValue::k_rational: {
		olive::Rational r = combined.value<olive::Rational>();
		normal_out->type = OAK_NODE_VALUE_RATIONAL;
		normal_out->num = r.numerator();
		normal_out->den = r.denominator();
		break;
	}
	case olive::NodeValue::k_color: {
		olive::core::Color c = combined.value<olive::core::Color>();
		normal_out->type = OAK_NODE_VALUE_COLOR;
		normal_out->f[0] = c.red();
		normal_out->f[1] = c.green();
		normal_out->f[2] = c.blue();
		normal_out->f[3] = c.alpha();
		break;
	}
	case olive::NodeValue::k_vec2: {
		QVector2D v = combined.value<QVector2D>();
		normal_out->type = OAK_NODE_VALUE_VEC2;
		normal_out->f[0] = v.x();
		normal_out->f[1] = v.y();
		break;
	}
	case olive::NodeValue::k_vec3: {
		QVector3D v = combined.value<QVector3D>();
		normal_out->type = OAK_NODE_VALUE_VEC3;
		normal_out->f[0] = v.x();
		normal_out->f[1] = v.y();
		normal_out->f[2] = v.z();
		break;
	}
	case olive::NodeValue::k_vec4: {
		QVector4D v = combined.value<QVector4D>();
		normal_out->type = OAK_NODE_VALUE_VEC4;
		normal_out->f[0] = v.x();
		normal_out->f[1] = v.y();
		normal_out->f[2] = v.z();
		normal_out->f[3] = v.w();
		break;
	}
	case olive::NodeValue::k_bezier: {
		Bezier b = combined.value<Bezier>();
		normal_out->type = OAK_NODE_VALUE_BEZIER;
		normal_out->f[0] = b.x();
		normal_out->f[1] = b.y();
		normal_out->f[2] = b.cp1_x();
		normal_out->f[3] = b.cp1_y();
		break;
	}
	default:
		normal_out->type = OAK_NODE_VALUE_NONE;
		normal_out->num = 0;
		normal_out->den = 0;
		normal_out->f[0] = 0;
		break;
	}
	return OAKENGINE_OK;
}

/* ---- Node type queries ---------------------------------------------------- */

int oakengine_node_is_clip(const OakEngineNode *self)
{
	return self && dynamic_cast<const olive::ClipBlock *>(impl(self)) ? 1 : 0;
}

int oakengine_node_is_track(const OakEngineNode *self)
{
	return self && dynamic_cast<const olive::Track *>(impl(self)) ? 1 : 0;
}

int oakengine_node_is_viewer_output(const OakEngineNode *self)
{
	return self && dynamic_cast<const olive::ViewerOutput *>(impl(self)) ? 1 : 0;
}

int oakengine_node_is_footage(const OakEngineNode *self)
{
	return self && dynamic_cast<const olive::Footage *>(impl(self)) ? 1 : 0;
}

int oakengine_node_is_sequence(const OakEngineNode *self)
{
	return self && dynamic_cast<const olive::Sequence *>(impl(self)) ? 1 : 0;
}

int oakengine_node_is_folder(const OakEngineNode *self)
{
	return self && dynamic_cast<const olive::Folder *>(impl(self)) ? 1 : 0;
}

/* ---- Node data (project tree columns) ------------------------------------- */

int oakengine_node_get_data(const OakEngineNode *self, int role,
							int *out_type, int64_t *out_int,
							char *out_str, int out_str_size)
{
	if (out_type) *out_type = 0;
	if (out_int) *out_int = 0;
	if (out_str && out_str_size > 0) out_str[0] = '\0';
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	olive::Node::DataType dt;
	switch (role) {
	case 0: dt = olive::Node::icon; break;
	case 1: dt = olive::Node::duration; break;
	case 2: dt = olive::Node::created_time; break;
	case 3: dt = olive::Node::modified_time; break;
	case 4: dt = olive::Node::frequency_rate; break;
	case 5: dt = olive::Node::tooltip; break;
	default: return OAKENGINE_E_INVALID;
	}
	const QVariant v = impl(self)->data(dt);
	if (!v.isValid()) {
		if (out_type) *out_type = 0;
		return OAKENGINE_OK;
	}
	switch (v.userType()) {
	case QMetaType::QString: {
		if (out_type) *out_type = 1;
		if (out_str) string_to_buf(v.toString(), out_str, out_str_size);
		break;
	}
	case QMetaType::LongLong:
	case QMetaType::Int:
	case QMetaType::UInt:
	case QMetaType::ULongLong: {
		if (out_type) *out_type = 2;
		if (out_int) *out_int = v.toLongLong();
		break;
	}
	default:
		// Unsupported variant kind: report as invalid.
		if (out_type) *out_type = 0;
		break;
	}
	return OAKENGINE_OK;
}

int oakengine_node_get_exclusive_dependency_count(const OakEngineNode *self)
{
	if (!self) {
		return 0;
	}
	return impl(self)->get_exclusive_dependencies().size();
}

OakEngineNode *oakengine_node_get_exclusive_dependency_at(
	const OakEngineNode *self, int index)
{
	if (!self) {
		return nullptr;
	}
	const QVector<olive::Node *> deps =
		impl(self)->get_exclusive_dependencies();
	if (index < 0 || index >= deps.size()) {
		return nullptr;
	}
	return wrap(deps.at(index));
}

/* ---- Clip / Track specific ------------------------------------------------ */

OakEngineNode *oakengine_clip_get_track(const OakEngineNode *clip)
{
	if (!clip) {
		return nullptr;
	}
	auto *c = dynamic_cast<olive::ClipBlock *>(impl(const_cast<OakEngineNode *>(clip)));
	if (!c) {
		return nullptr;
	}
	return wrap(c->track());
}

int oakengine_track_get_type(const OakEngineNode *track)
{
	if (!track) {
		return -1;
	}
	auto *t = dynamic_cast<olive::Track *>(impl(const_cast<OakEngineNode *>(track)));
	if (!t) {
		return -1;
	}
	return static_cast<int>(t->type());
}

int oakengine_track_get_index(const OakEngineNode *track)
{
	if (!track) {
		return -1;
	}
	auto *t = dynamic_cast<olive::Track *>(impl(const_cast<OakEngineNode *>(track)));
	if (!t) {
		return -1;
	}
	return t->index();
}

OakEngineNode *oakengine_track_get_sequence(const OakEngineNode *track)
{
	if (!track) {
		return nullptr;
	}
	auto *t = dynamic_cast<olive::Track *>(impl(const_cast<OakEngineNode *>(track)));
	if (!t) {
		return nullptr;
	}
	return wrap(t->sequence());
}

int oakengine_block_get_length_rational(
	const OakEngineNode *block, int *num, int *den)
{
	if (!block) {
		return OAKENGINE_E_INVALID;
	}
	auto *b = dynamic_cast<olive::Block *>(impl(const_cast<OakEngineNode *>(block)));
	if (!b) {
		return OAKENGINE_E_INVALID;
	}
	olive::Rational l = b->length();
	if (num) *num = l.numerator();
	if (den) *den = l.denominator();
	return OAKENGINE_OK;
}

int oakengine_block_get_in_rational(
	const OakEngineNode *block, int *num, int *den)
{
	if (!block) {
		return OAKENGINE_E_INVALID;
	}
	auto *b = dynamic_cast<olive::Block *>(impl(const_cast<OakEngineNode *>(block)));
	if (!b) {
		return OAKENGINE_E_INVALID;
	}
	olive::Rational v = b->in();
	if (num) *num = v.numerator();
	if (den) *den = v.denominator();
	return OAKENGINE_OK;
}

int oakengine_block_get_out_rational(
	const OakEngineNode *block, int *num, int *den)
{
	if (!block) {
		return OAKENGINE_E_INVALID;
	}
	auto *b = dynamic_cast<olive::Block *>(impl(const_cast<OakEngineNode *>(block)));
	if (!b) {
		return OAKENGINE_E_INVALID;
	}
	olive::Rational v = b->out();
	if (num) *num = v.numerator();
	if (den) *den = v.denominator();
	return OAKENGINE_OK;
}

/* ---- ViewerOutput specific ------------------------------------------------ */

OakEngineNode *oakengine_viewer_output_get_connected_texture(
	const OakEngineNode *self)
{
	if (!self) {
		return nullptr;
	}
	auto *v = dynamic_cast<olive::ViewerOutput *>(
		impl(const_cast<OakEngineNode *>(self)));
	if (!v) {
		return nullptr;
	}
	return wrap(v->get_connected_texture_output());
}

/* ---- Gizmo access --------------------------------------------------------- */

int oakengine_node_has_gizmos(const OakEngineNode *self)
{
	return self && impl(self)->has_gizmos() ? 1 : 0;
}

int oakengine_node_gizmo_count(const OakEngineNode *self)
{
	if (!self) {
		return 0;
	}
	return impl(self)->get_gizmos().size();
}

void *oakengine_node_gizmo_at(const OakEngineNode *self, int index)
{
	if (!self) {
		return nullptr;
	}
	const auto &gizmos = impl(self)->get_gizmos();
	if (index < 0 || index >= gizmos.size()) {
		return nullptr;
	}
	return gizmos.at(index);
}

int oakengine_node_update_gizmo_positions(
	OakEngineNode *self, void *node_value_row,
	int video_width, int video_height,
	int64_t time_num, int64_t time_den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	olive::Node *n = impl(self);
	if (!n->has_gizmos()) {
		return OAKENGINE_OK;
	}
	// Use the caller's NodeValueRow when provided, otherwise empty.
	const olive::NodeValueRow &row = node_value_row
		? *static_cast<const olive::NodeValueRow *>(node_value_row)
		: olive::NodeValueRow();
	olive::VideoParams vp;
	if (video_width > 0 && video_height > 0) {
		vp.set_width(video_width);
		vp.set_height(video_height);
	}
	olive::NodeGlobals globals(
		vp, olive::AudioParams(),
		olive::Rational(time_num, time_den),
		olive::LoopMode::k_loop_mode_off);
	n->update_gizmo_positions(row, globals);
	return OAKENGINE_OK;
}

/* ---- Graph topology ------------------------------------------------------- */

int oakengine_node_inputs_from(const OakEngineNode *self,
							   const OakEngineNode *other, int recursive)
{
	if (!self || !other) {
		return 0;
	}
	return impl(self)->inputs_from(
		impl(const_cast<OakEngineNode *>(other)), recursive != 0) ? 1 : 0;
}

int oakengine_node_output_connection_count(const OakEngineNode *self)
{
	if (!self) {
		return 0;
	}
	return int(impl(self)->output_connections().size());
}

int oakengine_node_output_connection_at(
	const OakEngineNode *self, int index, OakEngineNode **input_node,
	char *input_id_buf, int input_id_size, int *element)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const auto &conns = impl(self)->output_connections();
	if (index < 0 || index >= int(conns.size())) {
		return OAKENGINE_E_NOT_FOUND;
	}
	const auto &conn = conns[size_t(index)];
	// output_connections() stores {source (== self), NodeInput}; the
	// connection's destination is the NodeInput's node, NOT conn.first.
	if (input_node) {
		*input_node = wrap(conn.second.node());
	}
	if (input_id_buf && input_id_size > 0) {
		string_to_buf(conn.second.input(), input_id_buf, input_id_size);
	}
	if (element) {
		*element = conn.second.element();
	}
	return OAKENGINE_OK;
}

int oakengine_node_output_connection_at_ex(
	const OakEngineNode *self, int index, OakEngineNode **input_node,
	char *input_id_buf, int input_id_size, int *element, int *hidden)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const auto &conns = impl(self)->output_connections();
	if (index < 0 || index >= int(conns.size())) {
		return OAKENGINE_E_NOT_FOUND;
	}
	const auto &conn = conns[size_t(index)];
	// output_connections() stores {source (== self), NodeInput}; the
	// connection's destination is the NodeInput's node, NOT conn.first.
	if (input_node) {
		*input_node = wrap(conn.second.node());
	}
	if (input_id_buf && input_id_size > 0) {
		string_to_buf(conn.second.input(), input_id_buf, input_id_size);
	}
	if (element) {
		*element = conn.second.element();
	}
	if (hidden) {
		*hidden = conn.second.is_hidden() ? 1 : 0;
	}
	return OAKENGINE_OK;
}

int oakengine_node_input_connection_count_all(const OakEngineNode *self)
{
	if (!self) {
		return 0;
	}
	return int(impl(self)->input_connections().size());
}

int oakengine_node_input_connection_at_all(
	const OakEngineNode *self, int index, OakEngineNode **input_node,
	char *input_id_buf, int input_id_size, int *element,
	OakEngineNode **source_node, int *hidden)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const auto &conns = impl(self)->input_connections();
	if (index < 0 || index >= int(conns.size())) {
		return OAKENGINE_E_NOT_FOUND;
	}
	auto it = conns.cbegin();
	for (int i = 0; i < index; i++) {
		++it;
	}
	// it->first = NodeInput (the connected input on this node),
	// it->second = source Node* feeding it.
	if (input_node) {
		*input_node = wrap(it->first.node());
	}
	if (input_id_buf && input_id_size > 0) {
		string_to_buf(it->first.input(), input_id_buf, input_id_size);
	}
	if (element) {
		*element = it->first.element();
	}
	if (source_node) {
		*source_node = wrap(it->second);
	}
	if (hidden) {
		*hidden = it->first.is_hidden() ? 1 : 0;
	}
	return OAKENGINE_OK;
}

int oakengine_node_input_connection_count(
	const OakEngineNode *self, const char *input_id, int element)
{
	if (!self || !input_id) {
		return 0;
	}
	const auto &conns = impl(self)->input_connections();
	int count = 0;
	for (auto it = conns.cbegin(); it != conns.cend(); it++) {
		if (it->first.input() == QString::fromUtf8(input_id) &&
			it->first.element() == element) {
			count++;
		}
	}
	return count;
}

OakEngineNode *oakengine_node_input_connection_at(
	const OakEngineNode *self, const char *input_id, int element, int index)
{
	if (!self || !input_id || index < 0) {
		return nullptr;
	}
	const auto &conns = impl(self)->input_connections();
	int seen = 0;
	for (auto it = conns.cbegin(); it != conns.cend(); it++) {
		if (it->first.input() == QString::fromUtf8(input_id) &&
			it->first.element() == element) {
			if (seen == index) {
				return wrap(it->second);
			}
			seen++;
		}
	}
	return nullptr;
}

/* ---- Plugin messages ------------------------------------------------------ */

int oakengine_node_has_plugin(const OakEngineNode *self)
{
	if (!self) {
		return 0;
	}
	return impl(self)->getPluginInstance() != nullptr ? 1 : 0;
}

int oakengine_node_plugin_message_count(const OakEngineNode *self)
{
	if (!self) {
		return 0;
	}
	auto *instance = impl(self)->getPluginInstance();
	auto *olive_inst =
		dynamic_cast<olive::plugin::OlivePluginInstance *>(instance);
	if (!olive_inst) {
		return 0;
	}
	return olive_inst->persistent_message_count();
}

int oakengine_node_plugin_message_at(
	const OakEngineNode *self, int index, int *type, char *msg_buf,
	int msg_buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	auto *instance = impl(self)->getPluginInstance();
	auto *olive_inst =
		dynamic_cast<olive::plugin::OlivePluginInstance *>(instance);
	if (!olive_inst) {
		return OAKENGINE_E_NOT_FOUND;
	}
	const auto &msgs = olive_inst->persistent_messages();
	if (index < 0 || index >= msgs.size()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	if (type) {
		switch (msgs.at(index).type) {
		case olive::plugin::ErrorType::error:
			*type = 0;
			break;
		case olive::plugin::ErrorType::warning:
			*type = 1;
			break;
		default:
			*type = 2;
			break;
		}
	}
	if (msg_buf && msg_buf_size > 0) {
		string_to_buf(msgs.at(index).message, msg_buf, msg_buf_size);
	}
	return OAKENGINE_OK;
}

int oakengine_node_plugin_clear_messages(OakEngineNode *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	auto *instance = impl(self)->getPluginInstance();
	auto *olive_inst =
		dynamic_cast<olive::plugin::OlivePluginInstance *>(instance);
	if (!olive_inst) {
		return OAKENGINE_E_NOT_FOUND;
	}
	olive_inst->clearPersistentMessage();
	return OAKENGINE_OK;
}

/* ---- Node cache objects ------------------------------------------------------ */

OakEngineThumbnailCache *
oakengine_node_get_thumbnail_cache(const OakEngineNode *self)
{
	if (!self) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineThumbnailCache *>(
		impl(self)->thumbnail_cache());
}

OakEngineWaveformCache *
oakengine_node_get_waveform_cache(const OakEngineNode *self)
{
	if (!self) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineWaveformCache *>(
		impl(self)->waveform_cache());
}

OakEngineFrameCache *
oakengine_node_get_video_frame_cache(const OakEngineNode *self)
{
	if (!self) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineFrameCache *>(
		impl(self)->video_frame_cache());
}

} // extern "C"
