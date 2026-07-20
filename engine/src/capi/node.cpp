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
#include "node/node.h"
#include "node/nodeundo.h"
#include "node/project.h"
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
	set_error(QString());
	if (!self) {
		set_error(QStringLiteral("invalid node"));
		return OAKENGINE_E_INVALID;
	}
	push_or_run(new olive::NodeRenameCommand(
					impl(self), QString::fromUtf8(label ? label : "")),
				QStringLiteral("Rename Node"));
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
	olive::Node *connected =
		in_node->get_connected_output(olive::NodeInput(in_node, id));
	if (!connected) {
		set_error(QStringLiteral("input \"%1\" is not connected").arg(id));
		return OAKENGINE_E_NOT_FOUND;
	}
	push_or_run(new olive::NodeEdgeRemoveCommand(
					connected, olive::NodeInput(in_node, id)),
				QStringLiteral("Disconnect Nodes"));
	return OAKENGINE_OK;
}

} // extern "C"
