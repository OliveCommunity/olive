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

#include "nodeparamviewwidgetbridge.h"

#include <QCheckBox>
#include <QFontComboBox>
#include <QUrl>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include "oakutil/qtutils.h"
#include "core.h"
#include "nodeparambutton.h"
#include "nodeparamviewarraywidget.h"
#include "nodeparamviewtextedit.h"
#include "oakengine/color.h"
#include "oakengine/events.h"
#include "common/nodevaluehandle.h"
#include "common/oakvaluehelper.h"
#include "oakengine/node.h"
#include "oakengine/plugin.h"
#include "oakengine/viewer.h"
#include "oakengine/lut.h"
#include "oakengine/undo.h"
#include "widget/bezier/bezierwidget.h"
#include "widget/colorbutton/colorbutton.h"
#include "widget/filefield/filefield.h"
#include "widget/filefield/lutfilefield.h"
#include "widget/manageddisplay/colorprocessorhandle.h"
#include "widget/slider/floatslider.h"
#include "widget/slider/integerslider.h"
#include "widget/slider/rationalslider.h"

//#include <OpenImageIO/detail/fmt/base.h>

namespace olive
{

static int64_t rational_to_node_ts(OakEngineNode *node, const Rational &time)
{
	int num = 0, den = 1;
	oakengine_node_frame_time_base(node, &num, &den);
	return core::Timecode::time_to_timestamp(time, Rational(num, den));
}

static QVariant GetInputValueAtTime(const oak::Input &input,
									const Rational &node_time)
{
	OakEngineNode *node = input.node_handle();
	if (!node) {
		return QVariant();
	}
	const NodeValueType::Type type =
		static_cast<NodeValueType::Type>(input.data_type());
	const QByteArray input_utf8 = input.input_id().toUtf8();
	const char *input_id = input_utf8.constData();
	const int element = input.element();
	const int64_t time_ts = rational_to_node_ts(node, node_time);
	const OakEngineNode *enode = node;

	switch (type) {
	case NodeValueType::k_int:
	case NodeValueType::k_float:
	case NodeValueType::k_boolean:
	case NodeValueType::k_rational:
	case NodeValueType::k_color:
	case NodeValueType::k_vec2:
	case NodeValueType::k_vec3:
	case NodeValueType::k_vec4:
	case NodeValueType::k_combo: {
		oak_node_value v;
		if (oakengine_node_get_input_at_time(enode, input_id, element, -1,
										 time_ts, 1, &v) ==
			OAKENGINE_OK) {
			return OakNodeValueToQVariant(v);
		}
		break;
	}
	case NodeValueType::k_file:
	case NodeValueType::k_text:
	case NodeValueType::k_font:
	case NodeValueType::k_str_combo: {
		char buf[4096];
		const int len = oakengine_node_get_input_string_at_time(
			enode, input_id, element, time_ts, 0, buf, sizeof(buf));
		if (len >= 0) {
			return QString::fromUtf8(buf, len);
		}
		break;
	}
	case NodeValueType::k_binary: {
		const int len = oakengine_node_get_input_binary_at_time(
			enode, input_id, element, time_ts, 0, nullptr, 0);
		if (len > 0) {
			QByteArray bytes(len, '\0');
			oakengine_node_get_input_binary_at_time(
				enode, input_id, element, time_ts, 0, bytes.data(), len);
			return bytes;
		} else if (len == 0) {
			return QByteArray();
		}
		break;
	}
	case NodeValueType::k_bezier: {
		double out[6];
		if (oakengine_node_get_input_bezier_at_time(
				enode, input_id, element, time_ts, 0, out) == OAKENGINE_OK) {
			return QVariant::fromValue(
				Bezier(out[0], out[1], out[2], out[3], out[4], out[5]));
		}
		break;
	}
	default:
		break;
	}
	return QVariant();
}

static bool ResolveGroupInput(oak::Input *input)
{
	OakEngineNode *node = input->node_handle();
	char input_id[256];
	int element = input->element();
	const QByteArray utf = input->input_id().toUtf8();
	memcpy(input_id, utf.constData(),
		   qMin<int>(sizeof(input_id) - 1, utf.size()));
	input_id[sizeof(input_id) - 1] = '\0';
	if (!oakengine_node_group_get_inner(&node, input_id, sizeof(input_id),
									&element)) {
		return false;
	}
	*input = oak::Input(node, QString::fromUtf8(input_id), element);
	return true;
}

NodeParamViewWidgetBridge::NodeParamViewWidgetBridge(const oak::Input &input,
													 QObject *parent)
	: QObject(parent)
	, bridge_(new EngineEventBridge(this))
{
	connect(bridge_, &EngineEventBridge::node_input_value_changed, this,
			&NodeParamViewWidgetBridge::input_value_changed);
	connect(bridge_, &EngineEventBridge::node_input_property_changed, this,
			[this](OakEngineNode *, const QString &input) {
				property_changed(input);
			});
	connect(bridge_, &EngineEventBridge::node_input_data_type_changed, this,
			&NodeParamViewWidgetBridge::input_data_type_changed);

	oak::Input resolved = input;
	do {
		input_hierarchy_.append(resolved);

		bridge_->subscribe(reinterpret_cast<void *>(resolved.node_handle()),
						   OAKENGINE_EVENT_NODE_INPUT_VALUE_CHANGED);
		bridge_->subscribe(reinterpret_cast<void *>(resolved.node_handle()),
						   OAKENGINE_EVENT_NODE_INPUT_PROPERTY_CHANGED);
		bridge_->subscribe(reinterpret_cast<void *>(resolved.node_handle()),
						   OAKENGINE_EVENT_NODE_INPUT_DATA_TYPE_CHANGED);
	} while (ResolveGroupInput(&resolved));

	dragger_ = oakengine_dragger_create(
		get_inner_input().node_handle(),
		get_inner_input().input_id().toUtf8().constData(),
		get_inner_input().element(),
		-1); // track set in process_slider at start time

	create_widgets();
}

NodeParamViewWidgetBridge::~NodeParamViewWidgetBridge()
{
	// oakengine_dragger_create() ownership is ours (no-op on NULL)
	oakengine_dragger_free(dragger_);
	// Raw viewer subscription carries `this` as userdata
	if (viewer_sub_ > 0) {
		oakengine_event_unsubscribe(viewer_sub_);
	}
}

int get_slider_count(NodeValueType::Type type)
{
	return oakengine_node_value_keyframe_track_count(node_value_type_to_c(type));
}

namespace
{

// Map a panel widget's per-track scalar QVariant into the facade POD.
// Returns false for types that have no facade mapping (the caller keeps
// the legacy path for those).
bool variant_to_c_value(NodeValueType::Type type, const QVariant &value,
						oak_node_value *out)
{
	memset(out, 0, sizeof(*out));
	switch (type) {
	case NodeValueType::k_int:
		out->type = OAK_NODE_VALUE_INT;
		out->num = value.toLongLong();
		return true;
	case NodeValueType::k_combo:
		out->type = OAK_NODE_VALUE_COMBO;
		out->num = value.toLongLong();
		return true;
	case NodeValueType::k_float:
	case NodeValueType::k_bezier:
		out->type = OAK_NODE_VALUE_FLOAT;
		out->f[0] = value.toDouble();
		return true;
	case NodeValueType::k_boolean:
		out->type = OAK_NODE_VALUE_BOOL;
		out->num = value.toBool() ? 1 : 0;
		return true;
	case NodeValueType::k_rational: {
		const Rational r = value.value<Rational>();
		out->type = OAK_NODE_VALUE_RATIONAL;
		out->num = r.numerator();
		out->den = r.denominator();
		return true;
	}
	case NodeValueType::k_color:
		out->type = OAK_NODE_VALUE_COLOR;
		out->f[0] = value.toDouble();
		return true;
	case NodeValueType::k_vec2:
		out->type = OAK_NODE_VALUE_VEC2;
		out->f[0] = value.toDouble();
		return true;
	case NodeValueType::k_vec3:
		out->type = OAK_NODE_VALUE_VEC3;
		out->f[0] = value.toDouble();
		return true;
	case NodeValueType::k_vec4:
		out->type = OAK_NODE_VALUE_VEC4;
		out->f[0] = value.toDouble();
		return true;
	default:
		return false;
	}
}

// Convert rational node time to the facade's frame timestamps using the
// facade's own timebase (so the round trip is exact).
int64_t node_time_to_ts(OakEngineNode *node, const Rational &time)
{
	int tbn = 0, tbd = 0;
	oakengine_node_frame_time_base(node, &tbn, &tbd);
	return Timecode::time_to_timestamp(time, Rational(tbn, tbd),
									   Timecode::k_round);
}

// Mirrors of engine NodeValue::type_is_numeric()/type_is_vector()
// (engine/node/value.h) for the NodeValueType mirror ordinals.
bool type_is_numeric(NodeValueType::Type type)
{
	return type == NodeValueType::k_float || type == NodeValueType::k_int ||
		   type == NodeValueType::k_rational;
}

bool type_is_vector(NodeValueType::Type type)
{
	return type == NodeValueType::k_vec2 || type == NodeValueType::k_vec3 ||
		   type == NodeValueType::k_vec4;
}

// NodeInput property access through the typed facade getters (replaces the
// engine's QVariant-returning NodeInput::get_property()).
QString input_property_string(const oak::Input &input, const char *key)
{
	char buf[4096];
	buf[0] = '\0';
	oakengine_node_input_get_property_string(
		input.node_handle(), input.input_id().toUtf8().constData(), key, buf,
		sizeof(buf));
	return QString::fromUtf8(buf);
}

QStringList input_property_string_list(const oak::Input &input,
									   const char *key)
{
	QStringList out;
	OakEngineNode *node = input.node_handle();
	const QByteArray id = input.input_id().toUtf8();
	const int count =
		oakengine_node_input_get_property_string_list_count(node, id.constData(), key);
	for (int i = 0; i < count; i++) {
		char buf[1024];
		if (oakengine_node_input_get_property_string_list(
				node, id.constData(), key, i, buf, sizeof(buf)) >= 0) {
			out.append(QString::fromUtf8(buf));
		}
	}
	return out;
}

bool input_property_bool(const oak::Input &input, const char *key)
{
	int64_t v = 0;
	return oakengine_node_input_get_property_int(
			   input.node_handle(), input.input_id().toUtf8().constData(), key,
			   &v) == OAKENGINE_OK &&
		   v != 0;
}

// NodeInput::get_split_default_value_for_track() through the facade: the
// getter returns the per-track component as a POD typed with the input's
// declared type; rebuild the per-track QVariant the sliders expect.
QVariant split_default_value_for_track(const oak::Input &input, int track)
{
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	if (oakengine_node_input_get_default_value(
			input.node_handle(), input.input_id().toUtf8().constData(), track,
			&v) != OAKENGINE_OK) {
		return QVariant();
	}
	switch (v.type) {
	case OAK_NODE_VALUE_INT:
	case OAK_NODE_VALUE_COMBO:
		return QVariant::fromValue<int64_t>(v.num);
	case OAK_NODE_VALUE_RATIONAL:
		return QVariant::fromValue(Rational(int(v.num), int(v.den)));
	default:
		return v.f[0];
	}
}

// Rebuild one input property as a QVariant through the typed facade getters,
// replacing Node::get_input_properties() (a QVariantHash) in
// update_properties(). WRAPPER-GAP: the facade has no generic QVariant
// property getter (NodeInput::get_property()), only typed string/number/
// int/rational/string-list getters, so the value is reconstructed per key.
// Only the keys consumed by set_property() are mapped; unknown keys yield an
// invalid QVariant, which set_property() ignores.
// `data_type` is the property-owning input's declared type (needed to
// rebuild the min/max/offset values for the vector types).
QVariant read_input_property(const oak::Input &input, const QString &key,
							 NodeValueType::Type data_type)
{
	OakEngineNode *node = input.node_handle();
	const QByteArray id = input.input_id().toUtf8();
	const QByteArray key_utf8 = key.toUtf8();
	const char *k = key_utf8.constData();

	if (key == QStringLiteral("min") || key == QStringLiteral("max") ||
		key == QStringLiteral("offset")) {
		switch (data_type) {
		case NodeValueType::k_int:
		case NodeValueType::k_combo: {
			int64_t v = 0;
			if (oakengine_node_input_get_property_int(node, id.constData(), k,
													&v) == OAKENGINE_OK) {
				return QVariant::fromValue<int64_t>(v);
			}
			break;
		}
		case NodeValueType::k_float: {
			double v = 0;
			if (oakengine_node_input_get_property_number(node, id.constData(), k,
													   -1, &v) == OAKENGINE_OK) {
				return v;
			}
			break;
		}
		case NodeValueType::k_rational: {
			int num = 0, den = 1;
			if (oakengine_node_input_get_property_rational(
					node, id.constData(), k, &num, &den) == OAKENGINE_OK) {
				return QVariant::fromValue(Rational(num, den));
			}
			break;
		}
		case NodeValueType::k_vec2:
		case NodeValueType::k_vec3:
		case NodeValueType::k_vec4: {
			const int tracks = get_slider_count(data_type);
			double c[4] = { 0, 0, 0, 0 };
			for (int i = 0; i < tracks; i++) {
				if (oakengine_node_input_get_property_track_number(
						node, id.constData(), k, i, &c[i]) != OAKENGINE_OK) {
					return QVariant();
				}
			}
			if (data_type == NodeValueType::k_vec2) {
				return QVariant::fromValue(QVector2D(float(c[0]), float(c[1])));
			}
			if (data_type == NodeValueType::k_vec3) {
				return QVariant::fromValue(
					QVector3D(float(c[0]), float(c[1]), float(c[2])));
			}
			return QVariant::fromValue(
				QVector4D(float(c[0]), float(c[1]), float(c[2]), float(c[3])));
		}
		default:
			break;
		}
		return QVariant();
	}

	if (key == QStringLiteral("combo_str") ||
		key == QStringLiteral("combo_value_str")) {
		return input_property_string_list(input, k);
	}

	if (key == QStringLiteral("tooltip") ||
		key == QStringLiteral("placeholder") ||
		key == QStringLiteral("filter") || key.startsWith(QStringLiteral("color"))) {
		return input_property_string(input, k);
	}

	if (key == QStringLiteral("view") ||
		key == QStringLiteral("decimalplaces")) {
		int64_t v = 0;
		if (oakengine_node_input_get_property_int(node, id.constData(), k, &v) ==
			OAKENGINE_OK) {
			return int(v);
		}
		return QVariant();
	}

	if (key == QStringLiteral("base")) {
		double v = 0;
		if (oakengine_node_input_get_property_number(node, id.constData(), k,
												   -1, &v) == OAKENGINE_OK) {
			return v;
		}
		return QVariant();
	}

	if (key.startsWith(QStringLiteral("disable")) ||
		key.startsWith(QStringLiteral("enabled")) ||
		key == QStringLiteral("autotrim") ||
		key == QStringLiteral("viewlock") ||
		key == QStringLiteral("directory") ||
		key == QStringLiteral("lut_library") ||
		key == QStringLiteral("vieweronly")) {
		return input_property_bool(input, k);
	}

	return QVariant();
}

} // namespace

void NodeParamViewWidgetBridge::create_widgets()
{
	QWidget *parent = dynamic_cast<QWidget *>(this->parent());

	if (get_inner_input().is_array() && get_inner_input().element() == -1) {
		NodeParamViewArrayWidget *w = new NodeParamViewArrayWidget(
			get_inner_input().node(), get_inner_input().input_id(), parent);
		connect(w, &NodeParamViewArrayWidget::double_clicked, this,
				&NodeParamViewWidgetBridge::array_widget_double_clicked);
		widgets_.append(w);

	} else {
		// We assume the first data type is the "primary" type
		NodeValueType::Type t = get_data_type();
		switch (t) {
		// None of these inputs have applicable UI widgets
		case NodeValueType::k_none:
		case NodeValueType::k_texture:
		case NodeValueType::k_matrix:
		case NodeValueType::k_samples:
		case NodeValueType::k_video_params:
		case NodeValueType::k_audio_params:
		case NodeValueType::k_subtitle_params:
		case NodeValueType::k_binary:
		case NodeValueType::k_data_type_count:
			break;
		case NodeValueType::k_int: {
			create_sliders<IntegerSlider>(1, parent);
			break;
		}
		case NodeValueType::k_rational: {
			create_sliders<RationalSlider>(1, parent);
			break;
		}
		case NodeValueType::k_float:
		case NodeValueType::k_vec2:
		case NodeValueType::k_vec3:
		case NodeValueType::k_vec4: {
			create_sliders<FloatSlider>(get_slider_count(t), parent);
			break;
		}
		case NodeValueType::k_combo:
		case NodeValueType::k_str_combo: {
			QComboBox *combobox = new QComboBox(parent);

			QStringList items =
				input_property_string_list(get_inner_input(), "combo_str");
			QStringList values =
				input_property_string_list(get_inner_input(), "combo_value_str");
			const bool use_value_data = (t == NodeValueType::k_str_combo) &&
										!values.isEmpty();
			for (int i = 0; i < items.size(); ++i) {
				const QString &label = items.at(i);
				if (use_value_data && i < values.size()) {
					combobox->addItem(label, values.at(i));
				} else {
					combobox->addItem(label);
				}
			}

			widgets_.append(combobox);
			connect(combobox,
					static_cast<void (QComboBox::*)(int)>(
						&QComboBox::currentIndexChanged),
					this, &NodeParamViewWidgetBridge::widget_callback);
			break;
		}
		case NodeValueType::k_file: {
			FileField *file_field;
			if (input_property_bool(get_inner_input(), "lut_library")) {
				// File inputs that accept LUTs get a combo box for picking
				// from the global LUT library
				file_field = new LutFileField(parent);
			} else {
				file_field = new FileField(parent);
			}
			widgets_.append(file_field);
			connect(file_field, &FileField::filename_changed, this,
					&NodeParamViewWidgetBridge::widget_callback);
			break;
		}
		case NodeValueType::k_color: {
			if (input_property_string(get_inner_input(), "color_semantic") ==
				QStringLiteral("scalar")) {
				create_sliders<FloatSlider>(4, parent);
			} else {
				ColorButton *color_button = new ColorButton(
					oakengine_color_manager_from_project(
						oakengine_node_get_project(
							get_inner_input().node_handle())),
					parent);
				widgets_.append(color_button);
				connect(color_button, &ColorButton::color_changed, this,
						&NodeParamViewWidgetBridge::widget_callback);
			}
			break;
		}
		case NodeValueType::k_text: {
			NodeParamViewTextEdit *line_edit =
				new NodeParamViewTextEdit(parent);
			widgets_.append(line_edit);
			connect(line_edit, &NodeParamViewTextEdit::text_edited, this,
					&NodeParamViewWidgetBridge::widget_callback);
			connect(line_edit, &NodeParamViewTextEdit::request_edit_in_viewer,
					this, &NodeParamViewWidgetBridge::request_edit_text_in_viewer);
			break;
		}
		case NodeValueType::k_boolean: {
			QCheckBox *check_box = new QCheckBox(parent);
			widgets_.append(check_box);
			connect(check_box, &QCheckBox::clicked, this,
					&NodeParamViewWidgetBridge::widget_callback);
			break;
		}
		case NodeValueType::k_font: {
			QFontComboBox *font_combobox = new QFontComboBox(parent);
			widgets_.append(font_combobox);
			connect(font_combobox, &QFontComboBox::currentFontChanged, this,
					&NodeParamViewWidgetBridge::widget_callback);
			break;
		}
		case NodeValueType::k_bezier: {
			BezierWidget *bezier = new BezierWidget(parent);
			widgets_.append(bezier);

			connect(bezier->x_slider(), &FloatSlider::value_changed, this,
					&NodeParamViewWidgetBridge::widget_callback);
			connect(bezier->y_slider(), &FloatSlider::value_changed, this,
					&NodeParamViewWidgetBridge::widget_callback);
			connect(bezier->cp1_x_slider(), &FloatSlider::value_changed, this,
					&NodeParamViewWidgetBridge::widget_callback);
			connect(bezier->cp1_y_slider(), &FloatSlider::value_changed, this,
					&NodeParamViewWidgetBridge::widget_callback);
			connect(bezier->cp2_x_slider(), &FloatSlider::value_changed, this,
					&NodeParamViewWidgetBridge::widget_callback);
			connect(bezier->cp2_y_slider(), &FloatSlider::value_changed, this,
					&NodeParamViewWidgetBridge::widget_callback);
			break;
		}
		case NodeValueType::k_push_button: {
			const oak::Input input = get_inner_input();
			NodeParamButton *button = new NodeParamButton(input.name(), parent);
			widgets_.append(button);
			// PluginNode predicate goes through the C ABI (replaces the old
			// dynamic_cast<plugin::PluginNode*>); the handle itself is passed
			// opaquely to the push-button facade.
			OakEngineNode *plugin_node = input.node_handle();
			if (!oakengine_node_has_plugin(plugin_node)) {
				plugin_node = nullptr;
			}
			connect(button, &NodeParamButton::on_pressed, this,
				[plugin_node](const QString &name) {
					oakengine_plugin_node_push_button_clicked(
						plugin_node,
						name.toUtf8().constData());
				});
		}
		}

		// Check all properties
		update_properties();

		update_widget_values();

		// Install event filter to disable widgets picking up scroll events
		foreach (QWidget *w, widgets_) {
			w->installEventFilter(&scroll_filter_);
		}
	}
}

void NodeParamViewWidgetBridge::set_input_value(const QVariant &value, int track)
{
	// POD values go through the liboakengine C ABI facade (one undoable
	// command with the same set-value-at-time semantics as the old
	// app-side assembly); types without a facade mapping keep the legacy
	// undo assembly below.
	const oak::Input &input = get_inner_input();
	oak_node_value c_value;
	if (!variant_to_c_value(get_data_type(), value, &c_value)) {
		void *command = oakengine_undo_command_create_multi();
		set_input_value_internal(value, track, command, true);
		oakengine_undo_push(command, get_command_name().toUtf8().constData());
		return;
	}

	oakengine_node_set_input_at_time(
		input.node_handle(), input.input_id().toUtf8().constData(),
		input.element(),
		node_time_to_ts(input.node_handle(), get_current_time_as_node_time()),
		track, &c_value, 1);
}

void NodeParamViewWidgetBridge::set_string_value(const QString &value)
{
	// String-family inputs (file/text/font/str_combo) through the facade.
	const oak::Input &input = get_inner_input();
	oakengine_node_set_input_string_at_time(
		input.node_handle(), input.input_id().toUtf8().constData(),
		input.element(),
		node_time_to_ts(input.node_handle(), get_current_time_as_node_time()),
		value.toUtf8().constData());
}

void NodeParamViewWidgetBridge::set_input_value_internal(
	const QVariant &value, int track, void *command,
	bool insert_on_all_tracks_if_no_key)
{
		const oak::Input &input = get_inner_input();
	olive::Rational t = get_current_time_as_node_time();
	oak_node_value c_value;
	if (variant_to_c_value(get_data_type(), value, &c_value)) {
		oakengine_undo_command_multi_add_child(
			command,
			oakengine_node_set_value_at_time_command(
				reinterpret_cast<void *>(input.node_handle()),
				input.input_id().toUtf8().constData(), input.element(),
				t.numerator(), t.denominator(), &c_value, track,
				insert_on_all_tracks_if_no_key ? 1 : 0));
	}
}

void NodeParamViewWidgetBridge::process_slider(NumericSliderBase *slider,
											  int slider_track,
											  const QVariant &value)
{
	if (slider->is_dragging()) {
		// While we're dragging, we block the input's normal signalling and create our own
		if (!oakengine_dragger_is_started(dragger_)) {
			OakEngineNode *node = get_inner_input().node_handle();
			Rational node_time = get_current_time_as_node_time();
			int64_t ts = node_time_to_ts(node, node_time);

			oakengine_dragger_start(dragger_, ts, slider_track, 0);
		}

		oak_node_value c_value;
		if (variant_to_c_value(get_data_type(), value, &c_value)) {
			oakengine_dragger_drag(dragger_, &c_value);
		}

	} else if (oakengine_dragger_is_started(dragger_)) {
		// We were dragging and just stopped
		oak_node_value c_value;
		if (variant_to_c_value(get_data_type(), value, &c_value)) {
			oakengine_dragger_drag(dragger_, &c_value);
		}

		oakengine_dragger_end(dragger_, get_command_name().toUtf8().constData());

	} else {
		// No drag was involved, we can just push the value
		set_input_value(value, slider_track);
	}
}

void NodeParamViewWidgetBridge::widget_callback()
{
	switch (get_data_type()) {
	// None of these inputs have applicable UI widgets
	case NodeValueType::k_none:
	case NodeValueType::k_texture:
	case NodeValueType::k_matrix:
	case NodeValueType::k_samples:
	case NodeValueType::k_video_params:
	case NodeValueType::k_audio_params:
	case NodeValueType::k_subtitle_params:
	case NodeValueType::k_data_type_count:
		break;
	case NodeValueType::k_int: {
		// Widget is a IntegerSlider
		IntegerSlider *slider = static_cast<IntegerSlider *>(sender());

		process_slider(slider, QVariant::fromValue(slider->get_value()));
		break;
	}
	case NodeValueType::k_float: {
		// Widget is a FloatSlider
		FloatSlider *slider = static_cast<FloatSlider *>(sender());

		process_slider(slider, slider->get_value());
		break;
	}
	case NodeValueType::k_rational: {
		// Widget is a RationalSlider
		RationalSlider *slider = static_cast<RationalSlider *>(sender());
		process_slider(slider, QVariant::fromValue(slider->get_value()));
		break;
	}
	case NodeValueType::k_vec2: {
		// Widget is a FloatSlider
		FloatSlider *slider = static_cast<FloatSlider *>(sender());

		process_slider(slider, slider->get_value());
		break;
	}
	case NodeValueType::k_vec3: {
		// Widget is a FloatSlider
		FloatSlider *slider = static_cast<FloatSlider *>(sender());

		process_slider(slider, slider->get_value());
		break;
	}
	case NodeValueType::k_vec4: {
		// Widget is a FloatSlider
		FloatSlider *slider = static_cast<FloatSlider *>(sender());

		process_slider(slider, slider->get_value());
		break;
	}
	case NodeValueType::k_file: {
		set_string_value(static_cast<FileField *>(sender())->get_filename());
		break;
	}
	case NodeValueType::k_color: {
		if (input_property_string(get_inner_input(), "color_semantic") ==
			QStringLiteral("scalar")) {
			FloatSlider *slider = static_cast<FloatSlider *>(sender());
			process_slider(slider, slider->get_value());
		} else {
			// Sender is a ColorButton: all four components go through the
			// facade in one undoable command (track -1). The
			// color-management input properties are not undoable in the
			// engine and stay direct (same as the old code).
			ManagedColor c = static_cast<ColorButton *>(sender())->get_color();

			const oak::Input &input = get_inner_input();
			oak_node_value c_value;
			memset(&c_value, 0, sizeof(c_value));
			c_value.type = OAK_NODE_VALUE_COLOR;
			c_value.f[0] = c.red();
			c_value.f[1] = c.green();
			c_value.f[2] = c.blue();
			c_value.f[3] = c.alpha();
			oakengine_node_set_input_at_time(
				input.node_handle(), input.input_id().toUtf8().constData(),
				input.element(),
				node_time_to_ts(input.node_handle(),
								get_current_time_as_node_time()),
				-1, &c_value, 0);

			// The old code bracketed these with Node::blockSignals(true/false);
			// the facade's notify=0 already applies a QSignalBlocker inside
			// oakengine_node_set_input_property_string, so the bracket is
			// subsumed.
			OakEngineNode *n = get_inner_input().node_handle();
			const QByteArray input_id = get_inner_input().input_id().toUtf8();
			oakengine_node_set_input_property_string(
				n, input_id.constData(),
				"col_input", c.color_input().toUtf8().constData(), 0);
			oakengine_node_set_input_property_string(
				n, input_id.constData(),
				"col_display", c.color_output().display().toUtf8().constData(), 0);
			oakengine_node_set_input_property_string(
				n, input_id.constData(),
				"col_view", c.color_output().view().toUtf8().constData(), 0);
			oakengine_node_set_input_property_string(
				n, input_id.constData(),
				"col_look", c.color_output().look().toUtf8().constData(), 0);
		}
		break;
	}
	case NodeValueType::k_text: {
		// Sender is a NodeParamViewRichText
		set_string_value(static_cast<NodeParamViewTextEdit *>(sender())->text());
		break;
	}
	case NodeValueType::k_binary: {
		QString text = static_cast<NodeParamViewTextEdit *>(sender())->text();
		QByteArray raw = text.toUtf8();
		QByteArray decoded = QByteArray::fromBase64(raw);
		if (decoded.isEmpty() && !raw.isEmpty()) {
			decoded = raw;
		}
		set_input_value(decoded, 0);
		break;
	}
	case NodeValueType::k_boolean: {
		// Widget is a QCheckBox
		set_input_value(static_cast<QCheckBox *>(sender())->isChecked(), 0);
		break;
	}
	case NodeValueType::k_font: {
		// Widget is a QFontComboBox
		set_string_value(
			static_cast<QFontComboBox *>(sender())->currentFont().family());
		break;
	}
	case NodeValueType::k_combo: {
		// Widget is a QComboBox
		QComboBox *cb = static_cast<QComboBox *>(widgets_.first());
		int index = cb->currentIndex();

		// Subtract any splitters up until this point
		for (int i = index - 1; i >= 0; i--) {
			if (cb->itemData(i, Qt::AccessibleDescriptionRole).toString() ==
				QStringLiteral("separator")) {
				index--;
			}
		}

		set_input_value(index, 0);
		break;
	}
	case NodeValueType::k_str_combo: {
		QComboBox *cb = static_cast<QComboBox *>(widgets_.first());
		const QVariant data = cb->currentData();
		if (data.isValid()) {
			set_string_value(data.toString());
		} else {
			set_string_value(cb->currentText());
		}
		break;
	}
	case NodeValueType::k_bezier: {
		// Widget is a FloatSlider (child of BezierWidget)
		BezierWidget *bw = static_cast<BezierWidget *>(widgets_.first());
		FloatSlider *fs = static_cast<FloatSlider *>(sender());

		int index = -1;
		if (fs == bw->x_slider()) {
			index = 0;
		} else if (fs == bw->y_slider()) {
			index = 1;
		} else if (fs == bw->cp1_x_slider()) {
			index = 2;
		} else if (fs == bw->cp1_y_slider()) {
			index = 3;
		} else if (fs == bw->cp2_x_slider()) {
			index = 4;
		} else if (fs == bw->cp2_y_slider()) {
			index = 5;
		}

		if (index != -1) {
			process_slider(fs, index, fs->get_value());
		}
		break;
	}
	}
}

template <typename T>
void NodeParamViewWidgetBridge::create_sliders(int count, QWidget *parent)
{
	for (int i = 0; i < count; i++) {
		T *fs = new T(parent);
		fs->SliderBase::set_default_value(
			split_default_value_for_track(get_inner_input(), i));
		fs->set_ladder_element_count(2);

		// HACK: Force some spacing between sliders
		fs->setContentsMargins(
			0, 0,
			QtUtils::q_font_metrics_width(fs->fontMetrics(),
									   QStringLiteral("        ")),
			0);

		widgets_.append(fs);
		connect(fs, &T::value_changed, this,
				&NodeParamViewWidgetBridge::widget_callback);
	}
}

void NodeParamViewWidgetBridge::update_widget_values()
{
	if (get_inner_input().is_array() && get_inner_input().element() == -1) {
		return;
	}

	Rational node_time;
	if (get_inner_input().is_keyframing()) {
		node_time = get_current_time_as_node_time();
	}

	// We assume the first data type is the "primary" type
	switch (get_data_type()) {
	// None of these inputs have applicable UI widgets
	case NodeValueType::k_none:
	case NodeValueType::k_texture:
	case NodeValueType::k_matrix:
	case NodeValueType::k_samples:
	case NodeValueType::k_video_params:
	case NodeValueType::k_audio_params:
	case NodeValueType::k_subtitle_params:
	case NodeValueType::k_data_type_count:
		break;
	case NodeValueType::k_binary: {
		NodeParamViewTextEdit *e =
			static_cast<NodeParamViewTextEdit *>(widgets_.first());
		QByteArray bytes =
			GetInputValueAtTime(get_inner_input(), node_time).toByteArray();
		e->setTextPreservingCursor(QString::fromUtf8(bytes.toBase64()));
		break;
	}
	case NodeValueType::k_int: {
		static_cast<IntegerSlider *>(widgets_.first())
			->set_value(GetInputValueAtTime(get_inner_input(), node_time).toLongLong());
		break;
	}
	case NodeValueType::k_float: {
		static_cast<FloatSlider *>(widgets_.first())
			->set_value(GetInputValueAtTime(get_inner_input(), node_time).toDouble());
		break;
	}
	case NodeValueType::k_rational: {
		static_cast<RationalSlider *>(widgets_.first())
			->set_value(
				GetInputValueAtTime(get_inner_input(), node_time).value<Rational>());
		break;
	}
	case NodeValueType::k_vec2: {
		QVector2D vec2 =
			GetInputValueAtTime(get_inner_input(), node_time).value<QVector2D>();

		static_cast<FloatSlider *>(widgets_.at(0))
			->set_value(static_cast<double>(vec2.x()));
		static_cast<FloatSlider *>(widgets_.at(1))
			->set_value(static_cast<double>(vec2.y()));
		break;
	}
	case NodeValueType::k_vec3: {
		QVector3D vec3 =
			GetInputValueAtTime(get_inner_input(), node_time).value<QVector3D>();

		static_cast<FloatSlider *>(widgets_.at(0))
			->set_value(static_cast<double>(vec3.x()));
		static_cast<FloatSlider *>(widgets_.at(1))
			->set_value(static_cast<double>(vec3.y()));
		static_cast<FloatSlider *>(widgets_.at(2))
			->set_value(static_cast<double>(vec3.z()));
		break;
	}
	case NodeValueType::k_vec4: {
		QVector4D vec4 =
			GetInputValueAtTime(get_inner_input(), node_time).value<QVector4D>();

		static_cast<FloatSlider *>(widgets_.at(0))
			->set_value(static_cast<double>(vec4.x()));
		static_cast<FloatSlider *>(widgets_.at(1))
			->set_value(static_cast<double>(vec4.y()));
		static_cast<FloatSlider *>(widgets_.at(2))
			->set_value(static_cast<double>(vec4.z()));
		static_cast<FloatSlider *>(widgets_.at(3))
			->set_value(static_cast<double>(vec4.w()));
		break;
	}
	case NodeValueType::k_file: {
		FileField *ff = static_cast<FileField *>(widgets_.first());
		ff->set_filename(GetInputValueAtTime(get_inner_input(), node_time).toString());
		break;
	}
	case NodeValueType::k_color: {
		if (input_property_string(get_inner_input(), "color_semantic") ==
			QStringLiteral("scalar")) {
			Color c = GetInputValueAtTime(get_inner_input(), node_time).value<Color>();
			static_cast<FloatSlider *>(widgets_.at(0))
				->set_value(static_cast<double>(c.red()));
			static_cast<FloatSlider *>(widgets_.at(1))
				->set_value(static_cast<double>(c.green()));
			static_cast<FloatSlider *>(widgets_.at(2))
				->set_value(static_cast<double>(c.blue()));
			static_cast<FloatSlider *>(widgets_.at(3))
				->set_value(static_cast<double>(c.alpha()));
		} else {
			ManagedColor mc =
				GetInputValueAtTime(get_inner_input(), node_time).value<Color>();

			mc.set_color_input(
				input_property_string(get_inner_input(), "col_input"));

			QString d = input_property_string(get_inner_input(), "col_display");
			QString v = input_property_string(get_inner_input(), "col_view");
			QString l = input_property_string(get_inner_input(), "col_look");

			mc.set_color_output(oak::ColorTransform(d, v, l));

			static_cast<ColorButton *>(widgets_.first())->set_color(mc);
		}
		break;
	}
	case NodeValueType::k_text: {
		NodeParamViewTextEdit *e =
			static_cast<NodeParamViewTextEdit *>(widgets_.first());
		e->setTextPreservingCursor(
			GetInputValueAtTime(get_inner_input(), node_time).toString());
		break;
	}
	case NodeValueType::k_boolean:
		static_cast<QCheckBox *>(widgets_.first())
			->setChecked(GetInputValueAtTime(get_inner_input(), node_time).toBool());
		break;
	case NodeValueType::k_font: {
		QFontComboBox *fc = static_cast<QFontComboBox *>(widgets_.first());
		fc->blockSignals(true);
		fc->setCurrentFont(
			GetInputValueAtTime(get_inner_input(), node_time).toString());
		fc->blockSignals(false);
		break;
	}
	case NodeValueType::k_combo: {
		QComboBox *cb = static_cast<QComboBox *>(widgets_.first());
		cb->blockSignals(true);
		int index = GetInputValueAtTime(get_inner_input(), node_time).toInt();
		for (int i = 0; i < cb->count(); i++) {
			if (cb->itemData(i).toInt() == index) {
				cb->setCurrentIndex(i);
			}
		}
		cb->blockSignals(false);
		break;
	}
	case NodeValueType::k_str_combo: {
		QComboBox *cb = static_cast<QComboBox *>(widgets_.first());
		cb->blockSignals(true);
		const QString current =
			GetInputValueAtTime(get_inner_input(), node_time).toString();
		for (int i = 0; i < cb->count(); ++i) {
			const QVariant data = cb->itemData(i);
			if ((data.isValid() && data.toString() == current) ||
				(!data.isValid() && cb->itemText(i) == current)) {
				cb->setCurrentIndex(i);
				break;
			}
		}
		cb->blockSignals(false);
		break;
	}
	case NodeValueType::k_bezier: {
		BezierWidget *bw = static_cast<BezierWidget *>(widgets_.first());
		bw->set_value(GetInputValueAtTime(get_inner_input(), node_time).value<Bezier>());
		break;
	}
	}
}

Rational NodeParamViewWidgetBridge::get_current_time_as_node_time() const
{
	if (get_time_target()) {
		// ViewerOutput::get_playhead() via the C ABI
		int64_t pn = 0, pd = 1;
		oakengine_viewer_get_playhead(get_time_target(), &pn, &pd);
		return get_adjusted_time(
			get_time_target(),
			get_inner_input().node_handle(),
			Rational(pn, pd), k_transform_towards_input);
	} else {
		return 0;
	}
}

QString NodeParamViewWidgetBridge::get_command_name() const
{
	const oak::Input i = get_inner_input();
	return tr("Edited Value Of %1 - %2")
		.arg(i.node().label_and_name(), i.name());
}

void NodeParamViewWidgetBridge::set_timebase(const Rational &timebase)
{
	if (get_data_type() == NodeValueType::k_rational) {
		static_cast<RationalSlider *>(widgets_.first())->set_timebase(timebase);
	}
}

void NodeParamViewWidgetBridge::TimeTargetDisconnectEvent(OakEngineNode *v)
{
	if (viewer_sub_ > 0) {
		oakengine_event_unsubscribe(viewer_sub_);
		viewer_sub_ = 0;
	}
}

void NodeParamViewWidgetBridge::TimeTargetConnectEvent(OakEngineNode *v)
{
	viewer_sub_ = oakengine_event_subscribe(
		v,
		OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED,
		[](const oakengine_event *, void *userdata) {
			static_cast<NodeParamViewWidgetBridge *>(userdata)
				->update_widget_values();
		},
		this);
}

void NodeParamViewWidgetBridge::input_value_changed(OakEngineNode *source,
												  const QString &input,
												  int element, qint64 in_ts,
												  qint64 out_ts)
{
	const oak::Input ni(source, input, element);
	if (get_time_target() && get_inner_input() == ni &&
		!oakengine_dragger_is_started(dragger_)) {
		int64_t pn = 0, pd = 1;
		oakengine_viewer_get_playhead(
			reinterpret_cast<OakEngineNode *>(get_time_target()), &pn, &pd);
		int64_t playhead_ts =
			node_time_to_ts(source, Rational(pn, pd));
		if (in_ts <= playhead_ts && out_ts >= playhead_ts) {
			update_widget_values();
		}
	}
}

void NodeParamViewWidgetBridge::set_property(const QString &key,
											const QVariant &value)
{
	NodeValueType::Type data_type = get_data_type();

	// Parameters for all types
	bool key_is_disable = key.startsWith(QStringLiteral("disable"));
	if (key_is_disable || key.startsWith(QStringLiteral("enabled"))) {
		bool e = value.toBool();
		if (key_is_disable) {
			e = !e;
		}

		if (key.size() == 7) { // just the word "disable" or "enabled"
			for (int i = 0; i < widgets_.size(); i++) {
				widgets_.at(i)->setEnabled(e);
			}
		} else { // set specific track/widget
			bool ok;
			int element = key.mid(7).toInt(&ok);
			int tracks = oakengine_node_value_keyframe_track_count(node_value_type_to_c(data_type));

			if (ok && element >= 0 && element < tracks) {
				widgets_.at(element)->setEnabled(e);
			}
		}
	}

	if (key == QStringLiteral("tooltip")) {
		for (int i = 0; i < widgets_.size(); i++) {
			widgets_.at(i)->setToolTip(value.toString());
		}
	}

	// Parameters for integers, floats, and vectors
	if (type_is_numeric(data_type) || type_is_vector(data_type)) {
		if (key == QStringLiteral("min")) {
			switch (data_type) {
			case NodeValueType::k_int:
				static_cast<IntegerSlider *>(widgets_.first())
					->set_minimum(value.value<int64_t>());
				break;
			case NodeValueType::k_float:
				static_cast<FloatSlider *>(widgets_.first())
					->set_minimum(value.toDouble());
				break;
			case NodeValueType::k_rational:
				static_cast<RationalSlider *>(widgets_.first())
					->set_minimum(value.value<Rational>());
				break;
			case NodeValueType::k_vec2: {
				QVector2D min = value.value<QVector2D>();
				static_cast<FloatSlider *>(widgets_.at(0))->set_minimum(min.x());
				static_cast<FloatSlider *>(widgets_.at(1))->set_minimum(min.y());
				break;
			}
			case NodeValueType::k_vec3: {
				QVector3D min = value.value<QVector3D>();
				static_cast<FloatSlider *>(widgets_.at(0))->set_minimum(min.x());
				static_cast<FloatSlider *>(widgets_.at(1))->set_minimum(min.y());
				static_cast<FloatSlider *>(widgets_.at(2))->set_minimum(min.z());
				break;
			}
			case NodeValueType::k_vec4: {
				QVector4D min = value.value<QVector4D>();
				static_cast<FloatSlider *>(widgets_.at(0))->set_minimum(min.x());
				static_cast<FloatSlider *>(widgets_.at(1))->set_minimum(min.y());
				static_cast<FloatSlider *>(widgets_.at(2))->set_minimum(min.z());
				static_cast<FloatSlider *>(widgets_.at(3))->set_minimum(min.w());
				break;
			}
			default:
				break;
			}
		} else if (key == QStringLiteral("max")) {
			switch (data_type) {
			case NodeValueType::k_int:
				static_cast<IntegerSlider *>(widgets_.first())
					->set_maximum(value.value<int64_t>());
				break;
			case NodeValueType::k_float:
				static_cast<FloatSlider *>(widgets_.first())
					->set_maximum(value.toDouble());
				break;
			case NodeValueType::k_rational:
				static_cast<RationalSlider *>(widgets_.first())
					->set_maximum(value.value<Rational>());
				break;
			case NodeValueType::k_vec2: {
				QVector2D max = value.value<QVector2D>();
				static_cast<FloatSlider *>(widgets_.at(0))->set_maximum(max.x());
				static_cast<FloatSlider *>(widgets_.at(1))->set_maximum(max.y());
				break;
			}
			case NodeValueType::k_vec3: {
				QVector3D max = value.value<QVector3D>();
				static_cast<FloatSlider *>(widgets_.at(0))->set_maximum(max.x());
				static_cast<FloatSlider *>(widgets_.at(1))->set_maximum(max.y());
				static_cast<FloatSlider *>(widgets_.at(2))->set_maximum(max.z());
				break;
			}
			case NodeValueType::k_vec4: {
				QVector4D max = value.value<QVector4D>();
				static_cast<FloatSlider *>(widgets_.at(0))->set_maximum(max.x());
				static_cast<FloatSlider *>(widgets_.at(1))->set_maximum(max.y());
				static_cast<FloatSlider *>(widgets_.at(2))->set_maximum(max.z());
				static_cast<FloatSlider *>(widgets_.at(3))->set_maximum(max.w());
				break;
			}
			default:
				break;
			}
		} else if (key == QStringLiteral("offset")) {
			const int c_type = node_value_type_to_c(data_type);
			int tracks = oakengine_node_value_keyframe_track_count(c_type);

			oak_node_value normal;
			QVector<oak_node_value> track_vals(tracks);
			if (QVariantToOakNodeValue(data_type, value, &normal) &&
				oakengine_node_value_split_to_tracks(
					c_type, &normal, track_vals.data(), tracks) ==
					OAKENGINE_OK) {
				for (int i = 0; i < tracks; i++) {
					static_cast<NumericSliderBase *>(widgets_.at(i))
						->set_offset(track_vals.at(i).f[0]);
				}
			}

			update_widget_values();

		} else if (key.startsWith(QStringLiteral("color"))) {
			QColor c(value.toString());

			int tracks = oakengine_node_value_keyframe_track_count(node_value_type_to_c(data_type));

			if (key.size() == 5) {
				// Set for all tracks
				for (int i = 0; i < tracks; i++) {
					static_cast<SliderBase *>(widgets_.at(i))->set_color(c);
				}
			} else {
				bool ok;
				int element = key.mid(5).toInt(&ok);
				if (ok && element >= 0 && element < tracks) {
					static_cast<SliderBase *>(widgets_.at(element))->set_color(c);
				}
			}

		} else if (key == QStringLiteral("base")) {
			double d = value.toDouble();
			for (int i = 0; i < widgets_.size(); i++) {
				static_cast<NumericSliderBase *>(widgets_.at(i))
					->set_drag_multiplier(d);
			}
		}
	}

	// ComboBox strings changing
	if (data_type == NodeValueType::k_combo || data_type == NodeValueType::k_str_combo) {
		if (key == QStringLiteral("combo_str")) {
			QComboBox *cb = static_cast<QComboBox *>(widgets_.first());

			int old_index = cb->currentIndex();

			// Block the combobox changed signals since we anticipate the index will be the same and not require a re-render
			cb->blockSignals(true);

			cb->clear();

			QStringList items = value.toStringList();
			QStringList values =
				input_property_string_list(get_inner_input(), "combo_value_str");
			const bool use_value_data = (data_type == NodeValueType::k_str_combo) &&
										!values.isEmpty();
			int index = 0;
			for (int i = 0; i < items.size(); ++i) {
				const QString &s = items.at(i);
				if (s.isEmpty()) {
					cb->insertSeparator(cb->count());
					cb->setItemData(cb->count() - 1, -1);
				} else {
					if (use_value_data && i < values.size()) {
						cb->addItem(s, values.at(i));
					} else {
						cb->addItem(s, index);
						index++;
					}
				}
			}

			cb->setCurrentIndex(old_index);

			cb->blockSignals(false);

			// In case the amount of items is LESS and the previous index cannot be set, NOW we trigger a re-cache since the
			// value has changed
			if (cb->currentIndex() != old_index) {
				widget_callback();
			}
		}
	}

	// Parameters for floats and vectors only
	if (data_type == NodeValueType::k_float || type_is_vector(data_type)) {
		if (key == QStringLiteral("view")) {
			FloatSlider::DisplayType display_type =
				static_cast<FloatSlider::DisplayType>(value.toInt());

			foreach (QWidget *w, widgets_) {
				static_cast<FloatSlider *>(w)->set_display_type(display_type);
			}
		} else if (key == QStringLiteral("decimalplaces")) {
			int dec_places = value.toInt();

			foreach (QWidget *w, widgets_) {
				static_cast<FloatSlider *>(w)->set_decimal_places(dec_places);
			}
		} else if (key == QStringLiteral("autotrim")) {
			bool autotrim = value.toBool();

			foreach (QWidget *w, widgets_) {
				static_cast<FloatSlider *>(w)->set_auto_trim_decimal_places(
					autotrim);
			}
		}
	}

	if (data_type == NodeValueType::k_rational) {
		if (key == QStringLiteral("view")) {
			RationalSlider::DisplayType display_type =
				static_cast<RationalSlider::DisplayType>(value.toInt());

			foreach (QWidget *w, widgets_) {
				static_cast<RationalSlider *>(w)->set_display_type(display_type);
			}
		} else if (key == QStringLiteral("viewlock")) {
			bool locked = value.toBool();

			foreach (QWidget *w, widgets_) {
				static_cast<RationalSlider *>(w)->set_lock_display_type(locked);
			}
		}
	}

	// Parameters for files
	if (data_type == NodeValueType::k_file) {
		FileField *ff = static_cast<FileField *>(widgets_.first());

		if (key == QStringLiteral("placeholder")) {
			ff->set_placeholder(value.toString());
		} else if (key == QStringLiteral("directory")) {
			ff->set_directory_mode(value.toBool());
		} else if (key == QStringLiteral("filter")) {
			ff->set_name_filter(value.toString());
		} else if (key == QStringLiteral("lut_library") && value.toBool()) {
			// Offer the global LUT library directories as sidebar shortcuts in
			// the browse dialog
			QList<QUrl> sidebar_urls;
			{
				int dir_count = oakengine_lut_directory_count();
				for (int i = 0; i < dir_count; i++) {
					char buf[4096];
					int len = oakengine_lut_directory_at(i, buf, sizeof(buf));
					if (len > 0) {
						sidebar_urls.append(
							QUrl::fromLocalFile(
								QString::fromUtf8(buf, len)));
					}
				}
			}
			if (!sidebar_urls.isEmpty()) {
				ff->set_sidebar_urls(sidebar_urls);
			}
		}
	}

	// Parameters for text
	if (data_type == NodeValueType::k_text) {
		NodeParamViewTextEdit *tex =
			static_cast<NodeParamViewTextEdit *>(widgets_.first());

		if (key == QStringLiteral("vieweronly")) {
			tex->set_edit_in_viewer_only_mode(value.toBool());
		}
	}
}

void NodeParamViewWidgetBridge::input_data_type_changed(OakEngineNode *source,
													 const QString &input)
{
	if (source == get_outer_input().node_handle() &&
		input == get_outer_input().input_id()) {
		qDeleteAll(widgets_);
		widgets_.clear();

		create_widgets();

		emit widgets_recreated(get_outer_input());
	}
}

void NodeParamViewWidgetBridge::property_changed(const QString &input)
{
	bool found = false;

	for (auto it = input_hierarchy_.cbegin(); it != input_hierarchy_.cend();
		 it++) {
		if (it->input_id() == input) {
			found = true;
			break;
		}
	}

	if (found) {
		update_properties();
	}
}

void NodeParamViewWidgetBridge::update_properties()
{
	// Set properties from the last entry (the innermost input) to the first (the outermost)
	for (auto it = input_hierarchy_.crbegin(); it != input_hierarchy_.crend();
		 it++) {
		OakEngineNode *node = it->node_handle();
		const QByteArray id = it->input_id().toUtf8();
		// Node::get_input_properties() (a QVariantHash) has no facade
		// equivalent (WRAPPER-GAP); enumerate the keys and rebuild each value
		// through the typed getters (read_input_property()).
		const int count =
			oakengine_node_input_get_property_count(node, id.constData());
		for (int i = 0; i < count; i++) {
			char key_buf[256];
			if (oakengine_node_input_get_property_key(node, id.constData(), i,
													key_buf,
													sizeof(key_buf)) < 0) {
				continue;
			}
			const QString key = QString::fromUtf8(key_buf);
			set_property(key, read_input_property(
								  *it, key,
								  static_cast<NodeValueType::Type>(it->data_type())));
		}
	}
}

bool NodeParamViewScrollBlocker::eventFilter(QObject *watched, QEvent *event)
{
	Q_UNUSED(watched)

	if (event->type() == QEvent::Wheel) {
		// Block this event
		return true;
	}

	return false;
}

}
