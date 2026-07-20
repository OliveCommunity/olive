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

#include "common/qtutils.h"
#include "core.h"
#include "nodeparambutton.h"
#include "node/group/group.h"
#include "node/node.h"
#include "node/nodeundo.h"
#include "node/project/sequence/sequence.h"
#include "nodeparamviewarraywidget.h"
#include "nodeparamviewtextedit.h"
#include "oakengine/node.h"
#include "render/lutlibrary.h"
#include "undo/undostack.h"
#include "widget/bezier/bezierwidget.h"
#include "widget/colorbutton/colorbutton.h"
#include "widget/filefield/filefield.h"
#include "widget/filefield/lutfilefield.h"
#include "widget/slider/floatslider.h"
#include "widget/slider/integerslider.h"
#include "widget/slider/rationalslider.h"

//#include <OpenImageIO/detail/fmt/base.h>

namespace olive
{

NodeParamViewWidgetBridge::NodeParamViewWidgetBridge(NodeInput input,
													 QObject *parent)
	: QObject(parent)
{
	do {
		input_hierarchy_.append(input);

		connect(input.node(), &Node::value_changed, this,
				&NodeParamViewWidgetBridge::input_value_changed);
		connect(input.node(), &Node::input_property_changed, this,
				&NodeParamViewWidgetBridge::property_changed);
		connect(input.node(), &Node::input_data_type_changed, this,
				&NodeParamViewWidgetBridge::input_data_type_changed);
	} while (NodeGroup::get_inner(&input));

	create_widgets();
}

int get_slider_count(NodeValue::Type type)
{
	return NodeValue::get_number_of_keyframe_tracks(type);
}

namespace
{

// Map a panel widget's per-track scalar QVariant into the facade POD.
// Returns false for types that have no facade mapping (the caller keeps
// the legacy path for those).
bool variant_to_c_value(NodeValue::Type type, const QVariant &value,
						oak_node_value *out)
{
	memset(out, 0, sizeof(*out));
	switch (type) {
	case NodeValue::k_int:
		out->type = OAK_NODE_VALUE_INT;
		out->num = value.toLongLong();
		return true;
	case NodeValue::k_combo:
		out->type = OAK_NODE_VALUE_COMBO;
		out->num = value.toLongLong();
		return true;
	case NodeValue::k_float:
	case NodeValue::k_bezier:
		out->type = OAK_NODE_VALUE_FLOAT;
		out->f[0] = value.toDouble();
		return true;
	case NodeValue::k_boolean:
		out->type = OAK_NODE_VALUE_BOOL;
		out->num = value.toBool() ? 1 : 0;
		return true;
	case NodeValue::k_rational: {
		const Rational r = value.value<Rational>();
		out->type = OAK_NODE_VALUE_RATIONAL;
		out->num = r.numerator();
		out->den = r.denominator();
		return true;
	}
	case NodeValue::k_color:
		out->type = OAK_NODE_VALUE_COLOR;
		out->f[0] = value.toDouble();
		return true;
	case NodeValue::k_vec2:
		out->type = OAK_NODE_VALUE_VEC2;
		out->f[0] = value.toDouble();
		return true;
	case NodeValue::k_vec3:
		out->type = OAK_NODE_VALUE_VEC3;
		out->f[0] = value.toDouble();
		return true;
	case NodeValue::k_vec4:
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

} // namespace

void NodeParamViewWidgetBridge::create_widgets()
{
	QWidget *parent = dynamic_cast<QWidget *>(this->parent());

	if (get_inner_input().is_array() && get_inner_input().element() == -1) {
		NodeParamViewArrayWidget *w = new NodeParamViewArrayWidget(
			get_inner_input().node(), get_inner_input().input(), parent);
		connect(w, &NodeParamViewArrayWidget::double_clicked, this,
				&NodeParamViewWidgetBridge::array_widget_double_clicked);
		widgets_.append(w);

	} else {
		// We assume the first data type is the "primary" type
		NodeValue::Type t = get_data_type();
		switch (t) {
		// None of these inputs have applicable UI widgets
		case NodeValue::k_none:
		case NodeValue::k_texture:
		case NodeValue::k_matrix:
		case NodeValue::k_samples:
		case NodeValue::k_video_params:
		case NodeValue::k_audio_params:
		case NodeValue::k_subtitle_params:
		case NodeValue::k_binary:
		case NodeValue::k_data_type_count:
			break;
		case NodeValue::k_int: {
			create_sliders<IntegerSlider>(1, parent);
			break;
		}
		case NodeValue::k_rational: {
			create_sliders<RationalSlider>(1, parent);
			break;
		}
		case NodeValue::k_float:
		case NodeValue::k_vec2:
		case NodeValue::k_vec3:
		case NodeValue::k_vec4: {
			create_sliders<FloatSlider>(get_slider_count(t), parent);
			break;
		}
		case NodeValue::k_combo:
		case NodeValue::k_str_combo: {
			QComboBox *combobox = new QComboBox(parent);

			QStringList items = get_inner_input().get_combo_box_strings();
			QStringList values =
				get_inner_input().get_property("combo_value_str").toStringList();
			const bool use_value_data = (t == NodeValue::k_str_combo) &&
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
		case NodeValue::k_file: {
			FileField *file_field;
			if (get_inner_input().get_property(QStringLiteral("lut_library"))
					.toBool()) {
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
		case NodeValue::k_color: {
			if (get_inner_input().get_property("color_semantic").toString() ==
				QStringLiteral("scalar")) {
				create_sliders<FloatSlider>(4, parent);
			} else {
				ColorButton *color_button = new ColorButton(
					get_inner_input().node()->project()->color_manager(), parent);
				widgets_.append(color_button);
				connect(color_button, &ColorButton::color_changed, this,
						&NodeParamViewWidgetBridge::widget_callback);
			}
			break;
		}
		case NodeValue::k_text: {
			NodeParamViewTextEdit *line_edit =
				new NodeParamViewTextEdit(parent);
			widgets_.append(line_edit);
			connect(line_edit, &NodeParamViewTextEdit::text_edited, this,
					&NodeParamViewWidgetBridge::widget_callback);
			connect(line_edit, &NodeParamViewTextEdit::request_edit_in_viewer,
					this, &NodeParamViewWidgetBridge::request_edit_text_in_viewer);
			break;
		}
		case NodeValue::k_boolean: {
			QCheckBox *check_box = new QCheckBox(parent);
			widgets_.append(check_box);
			connect(check_box, &QCheckBox::clicked, this,
					&NodeParamViewWidgetBridge::widget_callback);
			break;
		}
		case NodeValue::k_font: {
			QFontComboBox *font_combobox = new QFontComboBox(parent);
			widgets_.append(font_combobox);
			connect(font_combobox, &QFontComboBox::currentFontChanged, this,
					&NodeParamViewWidgetBridge::widget_callback);
			break;
		}
		case NodeValue::k_bezier: {
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
		case NodeValue::k_push_button: {
			NodeInput input = get_inner_input();
			NodeParamButton *button = new NodeParamButton(input.name(), parent);
			widgets_.append(button);
			plugin::PluginNode *plugin_node =
				dynamic_cast<plugin::PluginNode *>(input.node());
			connect(button, &NodeParamButton::on_pressed, plugin_node,
					&plugin::PluginNode::push_button_clicked);
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
	const NodeInput &input = get_inner_input();
	oak_node_value c_value;
	if (!variant_to_c_value(get_data_type(), value, &c_value)) {
		MultiUndoCommand *command = new MultiUndoCommand();
		set_input_value_internal(value, track, command, true);
		Core::instance()->undo_stack()->push(command, get_command_name());
		return;
	}

	oakengine_node_set_input_at_time(
		reinterpret_cast<OakEngineNode *>(input.node()),
		input.input().toUtf8().constData(), input.element(),
		node_time_to_ts(reinterpret_cast<OakEngineNode *>(input.node()),
						get_current_time_as_node_time()),
		track, &c_value, 1);
}

void NodeParamViewWidgetBridge::set_string_value(const QString &value)
{
	// String-family inputs (file/text/font/str_combo) through the facade.
	const NodeInput &input = get_inner_input();
	oakengine_node_set_input_string_at_time(
		reinterpret_cast<OakEngineNode *>(input.node()),
		input.input().toUtf8().constData(), input.element(),
		node_time_to_ts(reinterpret_cast<OakEngineNode *>(input.node()),
						get_current_time_as_node_time()),
		value.toUtf8().constData());
}

void NodeParamViewWidgetBridge::set_input_value_internal(
	const QVariant &value, int track, MultiUndoCommand *command,
	bool insert_on_all_tracks_if_no_key)
{
	Node::set_value_at_time(get_inner_input(), get_current_time_as_node_time(), value,
						 track, command, insert_on_all_tracks_if_no_key);
}

void NodeParamViewWidgetBridge::process_slider(NumericSliderBase *slider,
											  int slider_track,
											  const QVariant &value)
{
	if (slider->is_dragging()) {
		// While we're dragging, we block the input's normal signalling and create our own
		if (!dragger_.is_started()) {
			Rational node_time = get_current_time_as_node_time();

			dragger_.start(NodeKeyframeTrackReference(get_inner_input(),
													  slider_track),
						   node_time);
		}

		dragger_.drag(value);

	} else if (dragger_.is_started()) {
		// We were dragging and just stopped
		dragger_.drag(value);

		MultiUndoCommand *command = new MultiUndoCommand();
		dragger_.end(command);
		Core::instance()->undo_stack()->push(command, get_command_name());

	} else {
		// No drag was involved, we can just push the value
		set_input_value(value, slider_track);
	}
}

void NodeParamViewWidgetBridge::widget_callback()
{
	switch (get_data_type()) {
	// None of these inputs have applicable UI widgets
	case NodeValue::k_none:
	case NodeValue::k_texture:
	case NodeValue::k_matrix:
	case NodeValue::k_samples:
	case NodeValue::k_video_params:
	case NodeValue::k_audio_params:
	case NodeValue::k_subtitle_params:
	case NodeValue::k_data_type_count:
		break;
	case NodeValue::k_int: {
		// Widget is a IntegerSlider
		IntegerSlider *slider = static_cast<IntegerSlider *>(sender());

		process_slider(slider, QVariant::fromValue(slider->get_value()));
		break;
	}
	case NodeValue::k_float: {
		// Widget is a FloatSlider
		FloatSlider *slider = static_cast<FloatSlider *>(sender());

		process_slider(slider, slider->get_value());
		break;
	}
	case NodeValue::k_rational: {
		// Widget is a RationalSlider
		RationalSlider *slider = static_cast<RationalSlider *>(sender());
		process_slider(slider, QVariant::fromValue(slider->get_value()));
		break;
	}
	case NodeValue::k_vec2: {
		// Widget is a FloatSlider
		FloatSlider *slider = static_cast<FloatSlider *>(sender());

		process_slider(slider, slider->get_value());
		break;
	}
	case NodeValue::k_vec3: {
		// Widget is a FloatSlider
		FloatSlider *slider = static_cast<FloatSlider *>(sender());

		process_slider(slider, slider->get_value());
		break;
	}
	case NodeValue::k_vec4: {
		// Widget is a FloatSlider
		FloatSlider *slider = static_cast<FloatSlider *>(sender());

		process_slider(slider, slider->get_value());
		break;
	}
	case NodeValue::k_file: {
		set_string_value(static_cast<FileField *>(sender())->get_filename());
		break;
	}
	case NodeValue::k_color: {
		if (get_inner_input().get_property("color_semantic").toString() ==
			QStringLiteral("scalar")) {
			FloatSlider *slider = static_cast<FloatSlider *>(sender());
			process_slider(slider, slider->get_value());
		} else {
			// Sender is a ColorButton: all four components go through the
			// facade in one undoable command (track -1). The
			// color-management input properties are not undoable in the
			// engine and stay direct (same as the old code).
			ManagedColor c = static_cast<ColorButton *>(sender())->get_color();

			const NodeInput &input = get_inner_input();
			oak_node_value c_value;
			memset(&c_value, 0, sizeof(c_value));
			c_value.type = OAK_NODE_VALUE_COLOR;
			c_value.f[0] = c.red();
			c_value.f[1] = c.green();
			c_value.f[2] = c.blue();
			c_value.f[3] = c.alpha();
			oakengine_node_set_input_at_time(
				reinterpret_cast<OakEngineNode *>(input.node()),
				input.input().toUtf8().constData(), input.element(),
				node_time_to_ts(
					reinterpret_cast<OakEngineNode *>(input.node()),
					get_current_time_as_node_time()),
				-1, &c_value, 0);

			Node *n = get_inner_input().node();
			n->blockSignals(true);
			n->set_input_property(get_inner_input().input(),
								QStringLiteral("col_input"), c.color_input());
			n->set_input_property(get_inner_input().input(),
								QStringLiteral("col_display"),
								c.color_output().display());
			n->set_input_property(get_inner_input().input(),
								QStringLiteral("col_view"),
								c.color_output().view());
			n->set_input_property(get_inner_input().input(),
								QStringLiteral("col_look"),
								c.color_output().look());
			n->blockSignals(false);
		}
		break;
	}
	case NodeValue::k_text: {
		// Sender is a NodeParamViewRichText
		set_string_value(static_cast<NodeParamViewTextEdit *>(sender())->text());
		break;
	}
	case NodeValue::k_binary: {
		QString text = static_cast<NodeParamViewTextEdit *>(sender())->text();
		QByteArray raw = text.toUtf8();
		QByteArray decoded = QByteArray::fromBase64(raw);
		if (decoded.isEmpty() && !raw.isEmpty()) {
			decoded = raw;
		}
		set_input_value(decoded, 0);
		break;
	}
	case NodeValue::k_boolean: {
		// Widget is a QCheckBox
		set_input_value(static_cast<QCheckBox *>(sender())->isChecked(), 0);
		break;
	}
	case NodeValue::k_font: {
		// Widget is a QFontComboBox
		set_string_value(
			static_cast<QFontComboBox *>(sender())->currentFont().family());
		break;
	}
	case NodeValue::k_combo: {
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
	case NodeValue::k_str_combo: {
		QComboBox *cb = static_cast<QComboBox *>(widgets_.first());
		const QVariant data = cb->currentData();
		if (data.isValid()) {
			set_string_value(data.toString());
		} else {
			set_string_value(cb->currentText());
		}
		break;
	}
	case NodeValue::k_bezier: {
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
			get_inner_input().get_split_default_value_for_track(i));
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
	case NodeValue::k_none:
	case NodeValue::k_texture:
	case NodeValue::k_matrix:
	case NodeValue::k_samples:
	case NodeValue::k_video_params:
	case NodeValue::k_audio_params:
	case NodeValue::k_subtitle_params:
	case NodeValue::k_data_type_count:
		break;
	case NodeValue::k_binary: {
		NodeParamViewTextEdit *e =
			static_cast<NodeParamViewTextEdit *>(widgets_.first());
		QByteArray bytes =
			get_inner_input().get_value_at_time(node_time).toByteArray();
		e->setTextPreservingCursor(QString::fromUtf8(bytes.toBase64()));
		break;
	}
	case NodeValue::k_int: {
		static_cast<IntegerSlider *>(widgets_.first())
			->set_value(get_inner_input().get_value_at_time(node_time).toLongLong());
		break;
	}
	case NodeValue::k_float: {
		static_cast<FloatSlider *>(widgets_.first())
			->set_value(get_inner_input().get_value_at_time(node_time).toDouble());
		break;
	}
	case NodeValue::k_rational: {
		static_cast<RationalSlider *>(widgets_.first())
			->set_value(
				get_inner_input().get_value_at_time(node_time).value<Rational>());
		break;
	}
	case NodeValue::k_vec2: {
		QVector2D vec2 =
			get_inner_input().get_value_at_time(node_time).value<QVector2D>();

		static_cast<FloatSlider *>(widgets_.at(0))
			->set_value(static_cast<double>(vec2.x()));
		static_cast<FloatSlider *>(widgets_.at(1))
			->set_value(static_cast<double>(vec2.y()));
		break;
	}
	case NodeValue::k_vec3: {
		QVector3D vec3 =
			get_inner_input().get_value_at_time(node_time).value<QVector3D>();

		static_cast<FloatSlider *>(widgets_.at(0))
			->set_value(static_cast<double>(vec3.x()));
		static_cast<FloatSlider *>(widgets_.at(1))
			->set_value(static_cast<double>(vec3.y()));
		static_cast<FloatSlider *>(widgets_.at(2))
			->set_value(static_cast<double>(vec3.z()));
		break;
	}
	case NodeValue::k_vec4: {
		QVector4D vec4 =
			get_inner_input().get_value_at_time(node_time).value<QVector4D>();

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
	case NodeValue::k_file: {
		FileField *ff = static_cast<FileField *>(widgets_.first());
		ff->set_filename(get_inner_input().get_value_at_time(node_time).toString());
		break;
	}
	case NodeValue::k_color: {
		if (get_inner_input().get_property("color_semantic").toString() ==
			QStringLiteral("scalar")) {
			Color c = get_inner_input().get_value_at_time(node_time).value<Color>();
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
				get_inner_input().get_value_at_time(node_time).value<Color>();

			mc.set_color_input(
				get_inner_input().get_property("col_input").toString());

			QString d = get_inner_input().get_property("col_display").toString();
			QString v = get_inner_input().get_property("col_view").toString();
			QString l = get_inner_input().get_property("col_look").toString();

			mc.set_color_output(ColorTransform(d, v, l));

			static_cast<ColorButton *>(widgets_.first())->set_color(mc);
		}
		break;
	}
	case NodeValue::k_text: {
		NodeParamViewTextEdit *e =
			static_cast<NodeParamViewTextEdit *>(widgets_.first());
		e->setTextPreservingCursor(
			get_inner_input().get_value_at_time(node_time).toString());
		break;
	}
	case NodeValue::k_boolean:
		static_cast<QCheckBox *>(widgets_.first())
			->setChecked(get_inner_input().get_value_at_time(node_time).toBool());
		break;
	case NodeValue::k_font: {
		QFontComboBox *fc = static_cast<QFontComboBox *>(widgets_.first());
		fc->blockSignals(true);
		fc->setCurrentFont(
			get_inner_input().get_value_at_time(node_time).toString());
		fc->blockSignals(false);
		break;
	}
	case NodeValue::k_combo: {
		QComboBox *cb = static_cast<QComboBox *>(widgets_.first());
		cb->blockSignals(true);
		int index = get_inner_input().get_value_at_time(node_time).toInt();
		for (int i = 0; i < cb->count(); i++) {
			if (cb->itemData(i).toInt() == index) {
				cb->setCurrentIndex(i);
			}
		}
		cb->blockSignals(false);
		break;
	}
	case NodeValue::k_str_combo: {
		QComboBox *cb = static_cast<QComboBox *>(widgets_.first());
		cb->blockSignals(true);
		const QString current =
			get_inner_input().get_value_at_time(node_time).toString();
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
	case NodeValue::k_bezier: {
		BezierWidget *bw = static_cast<BezierWidget *>(widgets_.first());
		bw->set_value(get_inner_input().get_value_at_time(node_time).value<Bezier>());
		break;
	}
	}
}

Rational NodeParamViewWidgetBridge::get_current_time_as_node_time() const
{
	if (get_time_target()) {
		return get_adjusted_time(get_time_target(), get_inner_input().node(),
							   get_time_target()->get_playhead(),
							   Node::k_transform_towards_input);
	} else {
		return 0;
	}
}

QString NodeParamViewWidgetBridge::get_command_name() const
{
	NodeInput i = get_inner_input();
	return tr("Edited Value Of %1 - %2")
		.arg(i.node()->get_label_and_name(), i.node()->get_input_name(i.input()));
}

void NodeParamViewWidgetBridge::set_timebase(const Rational &timebase)
{
	if (get_data_type() == NodeValue::k_rational) {
		static_cast<RationalSlider *>(widgets_.first())->set_timebase(timebase);
	}
}

void NodeParamViewWidgetBridge::TimeTargetDisconnectEvent(ViewerOutput *v)
{
	disconnect(v, &ViewerOutput::playhead_changed, this,
			   &NodeParamViewWidgetBridge::update_widget_values);
}

void NodeParamViewWidgetBridge::TimeTargetConnectEvent(ViewerOutput *v)
{
	connect(v, &ViewerOutput::playhead_changed, this,
			&NodeParamViewWidgetBridge::update_widget_values);
}

void NodeParamViewWidgetBridge::input_value_changed(const NodeInput &input,
												  const TimeRange &range)
{
	if (get_time_target() && get_inner_input() == input && !dragger_.is_started() &&
		range.in() <= get_time_target()->get_playhead() &&
		range.out() >= get_time_target()->get_playhead()) {
		// We'll need to update the widgets because the values have changed on our current time
		update_widget_values();
	}
}

void NodeParamViewWidgetBridge::set_property(const QString &key,
											const QVariant &value)
{
	NodeValue::Type data_type = get_data_type();

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
			int tracks = NodeValue::get_number_of_keyframe_tracks(data_type);

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
	if (NodeValue::type_is_numeric(data_type) ||
		NodeValue::type_is_vector(data_type)) {
		if (key == QStringLiteral("min")) {
			switch (data_type) {
			case NodeValue::k_int:
				static_cast<IntegerSlider *>(widgets_.first())
					->set_minimum(value.value<int64_t>());
				break;
			case NodeValue::k_float:
				static_cast<FloatSlider *>(widgets_.first())
					->set_minimum(value.toDouble());
				break;
			case NodeValue::k_rational:
				static_cast<RationalSlider *>(widgets_.first())
					->set_minimum(value.value<Rational>());
				break;
			case NodeValue::k_vec2: {
				QVector2D min = value.value<QVector2D>();
				static_cast<FloatSlider *>(widgets_.at(0))->set_minimum(min.x());
				static_cast<FloatSlider *>(widgets_.at(1))->set_minimum(min.y());
				break;
			}
			case NodeValue::k_vec3: {
				QVector3D min = value.value<QVector3D>();
				static_cast<FloatSlider *>(widgets_.at(0))->set_minimum(min.x());
				static_cast<FloatSlider *>(widgets_.at(1))->set_minimum(min.y());
				static_cast<FloatSlider *>(widgets_.at(2))->set_minimum(min.z());
				break;
			}
			case NodeValue::k_vec4: {
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
			case NodeValue::k_int:
				static_cast<IntegerSlider *>(widgets_.first())
					->set_maximum(value.value<int64_t>());
				break;
			case NodeValue::k_float:
				static_cast<FloatSlider *>(widgets_.first())
					->set_maximum(value.toDouble());
				break;
			case NodeValue::k_rational:
				static_cast<RationalSlider *>(widgets_.first())
					->set_maximum(value.value<Rational>());
				break;
			case NodeValue::k_vec2: {
				QVector2D max = value.value<QVector2D>();
				static_cast<FloatSlider *>(widgets_.at(0))->set_maximum(max.x());
				static_cast<FloatSlider *>(widgets_.at(1))->set_maximum(max.y());
				break;
			}
			case NodeValue::k_vec3: {
				QVector3D max = value.value<QVector3D>();
				static_cast<FloatSlider *>(widgets_.at(0))->set_maximum(max.x());
				static_cast<FloatSlider *>(widgets_.at(1))->set_maximum(max.y());
				static_cast<FloatSlider *>(widgets_.at(2))->set_maximum(max.z());
				break;
			}
			case NodeValue::k_vec4: {
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
			int tracks = NodeValue::get_number_of_keyframe_tracks(data_type);

			QVector<QVariant> offsets =
				NodeValue::split_normal_value_into_track_values(data_type,
																value);

			for (int i = 0; i < tracks; i++) {
				static_cast<NumericSliderBase *>(widgets_.at(i))
					->set_offset(offsets.at(i));
			}

			update_widget_values();

		} else if (key.startsWith(QStringLiteral("color"))) {
			QColor c(value.toString());

			int tracks = NodeValue::get_number_of_keyframe_tracks(data_type);

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
	if (data_type == NodeValue::k_combo || data_type == NodeValue::k_str_combo) {
		if (key == QStringLiteral("combo_str")) {
			QComboBox *cb = static_cast<QComboBox *>(widgets_.first());

			int old_index = cb->currentIndex();

			// Block the combobox changed signals since we anticipate the index will be the same and not require a re-render
			cb->blockSignals(true);

			cb->clear();

			QStringList items = value.toStringList();
			QStringList values =
				get_inner_input().get_property("combo_value_str").toStringList();
			const bool use_value_data = (data_type == NodeValue::k_str_combo) &&
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
	if (data_type == NodeValue::k_float ||
		NodeValue::type_is_vector(data_type)) {
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

	if (data_type == NodeValue::k_rational) {
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
	if (data_type == NodeValue::k_file) {
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
			for (const QString &dir : LUTLibrary::get_directories()) {
				sidebar_urls.append(QUrl::fromLocalFile(dir));
			}
			if (!sidebar_urls.isEmpty()) {
				ff->set_sidebar_urls(sidebar_urls);
			}
		}
	}

	// Parameters for text
	if (data_type == NodeValue::k_text) {
		NodeParamViewTextEdit *tex =
			static_cast<NodeParamViewTextEdit *>(widgets_.first());

		if (key == QStringLiteral("vieweronly")) {
			tex->set_edit_in_viewer_only_mode(value.toBool());
		}
	}
}

void NodeParamViewWidgetBridge::input_data_type_changed(const QString &input,
													 NodeValue::Type type)
{
	if (sender() == get_outer_input().node() &&
		input == get_outer_input().input()) {
		// Delete all widgets
		qDeleteAll(widgets_);
		widgets_.clear();

		// Create new widgets
		create_widgets();

		// Signal that widgets are new
		emit widgets_recreated(get_outer_input());
	}
}

void NodeParamViewWidgetBridge::property_changed(const QString &input,
												const QString &key,
												const QVariant &value)
{
	bool found = false;

	for (auto it = input_hierarchy_.cbegin(); it != input_hierarchy_.cend();
		 it++) {
		if (it->input() == input) {
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
		auto input_properties = it->node()->get_input_properties(it->input());
		for (auto jt = input_properties.cbegin(); jt != input_properties.cend();
			 jt++) {
			set_property(jt.key(), jt.value());
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
