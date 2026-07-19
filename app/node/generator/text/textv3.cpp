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

#include "textv3.h"

#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QTextDocument>

#include "common/html.h"
#include "coreengine.h"
#include "node/project.h"
#include "node/nodeundo.h"

namespace olive
{

#define super ShapeNodeBase

enum TextVerticalAlign {
	k_vertical_align_top,
	k_vertical_align_center,
	k_vertical_align_bottom,
};

const QString TextGeneratorV3::k_text_input = QStringLiteral("text_in");
const QString TextGeneratorV3::k_vertical_alignment_input =
	QStringLiteral("valign_in");
const QString TextGeneratorV3::k_use_args_input = QStringLiteral("use_args_in");
const QString TextGeneratorV3::k_args_input = QStringLiteral("args_in");

TextGeneratorV3::TextGeneratorV3()
	: ShapeNodeBase(false)
	, dont_emit_valign_(false)
{
	add_input(k_text_input, NodeValue::k_text,
			 QStringLiteral("<p style='font-size: 72pt; color: white;'>%1</p>")
				 .arg(tr("Sample Text")));
	set_input_property(k_text_input, QStringLiteral("vieweronly"), true);

	set_standard_value(k_size_input, QVector2D(400, 300));

	add_input(k_vertical_alignment_input, NodeValue::k_combo,
			 InputFlags(k_input_flag_hidden | k_input_flag_static));

	add_input(k_use_args_input, NodeValue::k_boolean, true,
			 InputFlags(k_input_flag_hidden | k_input_flag_static));

	add_input(k_args_input, NodeValue::k_text, InputFlags(k_input_flag_array));
	set_input_property(k_args_input, QStringLiteral("arraystart"), 1);

	text_gizmo_ = new TextGizmo(this);
	text_gizmo_->set_input(NodeInput(this, k_text_input));
	connect(text_gizmo_, &TextGizmo::activated, this,
			&TextGeneratorV3::gizmo_activated);
	connect(text_gizmo_, &TextGizmo::deactivated, this,
			&TextGeneratorV3::gizmo_deactivated);
}

QString TextGeneratorV3::name() const
{
	return tr("Text");
}

QString TextGeneratorV3::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.text3");
}

QVector<Node::CategoryID> TextGeneratorV3::category() const
{
	return { k_category_generator };
}

QString TextGeneratorV3::description() const
{
	return tr("Generate rich text.");
}

void TextGeneratorV3::retranslate()
{
	super::retranslate();

	set_input_name(k_text_input, tr("Text"));
	set_input_name(k_vertical_alignment_input, tr("Vertical Alignment"));
	set_combo_box_strings(k_vertical_alignment_input,
					   { tr("Top"), tr("Middle"), tr("Bottom") });
	set_input_name(k_args_input, tr("Arguments"));
}

void TextGeneratorV3::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	QString text = value[k_text_input].to_string();

	if (value[k_use_args_input].to_bool()) {
		auto args = value[k_args_input].to_array();
		if (!args.empty()) {
			QStringList list;
			list.reserve(args.size());
			for (size_t i = 0; i < args.size(); i++) {
				list.append(args[i].to_string());
			}

			text = format_string(text, list);
		}
	}

	if (!text.isEmpty()) {
		TexturePtr base = value[k_text_input].to_texture();

		VideoParams text_params = base ? base->params() : globals.vparams();
		text_params.set_format(PixelFormat::u8);
		text_params.set_colorspace(
			project()->color_manager()->get_default_input_color_space());

		GenerateJob job(value);
		job.insert(k_text_input, NodeValue(NodeValue::k_text, text));

		push_mergable_job(value, Texture::job(text_params, job), table);
	} else if (value[k_base_input].to_texture()) {
		table->push(value[k_base_input]);
	}
}

void TextGeneratorV3::generate_frame(FramePtr frame,
									const GenerateJob &job) const
{
	QImage img(reinterpret_cast<uchar *>(frame->data()), frame->width(),
			   frame->height(), frame->linesize_bytes(),
			   QImage::Format_RGBA8888_Premultiplied);
	img.fill(Qt::transparent);

	// 96 DPI in DPM (96 / 2.54 * 100)
	const int dpm = 3780;
	img.setDotsPerMeterX(dpm);
	img.setDotsPerMeterY(dpm);

	QTextDocument text_doc;
	text_doc.documentLayout()->setPaintDevice(&img);

	QString html = job.get(k_text_input).to_string();
	Html::html_to_doc(&text_doc, html);

	QVector2D size = job.get(k_size_input).to_vec2();
	text_doc.setTextWidth(size.x());

	// Draw rich text onto image
	QPainter p(&img);
	p.scale(1.0 / frame->video_params().divider(),
			1.0 / frame->video_params().divider());

	QVector2D pos = job.get(k_position_input).to_vec2();
	p.translate(pos.x() - size.x() / 2, pos.y() - size.y() / 2);
	p.translate(frame->video_params().width() / 2,
				frame->video_params().height() / 2);
	p.setClipRect(0, 0, size.x(), size.y());

	switch (static_cast<VerticalAlignment>(
		job.get(k_vertical_alignment_input).to_int())) {
	case k_v_align_top:
		// Do nothing
		break;
	case k_v_align_middle:
		p.translate(0, size.y() / 2 - text_doc.size().height() / 2);
		break;
	case k_v_align_bottom:
		p.translate(0, size.y() - text_doc.size().height());
		break;
	}

	// Ensure default text color is white
	QAbstractTextDocumentLayout::PaintContext ctx;
	ctx.palette.setColor(QPalette::Text, Qt::white);

	text_doc.documentLayout()->draw(&p, ctx);
}

void TextGeneratorV3::update_gizmo_positions(const NodeValueRow &row,
										   const NodeGlobals &globals)
{
	super::update_gizmo_positions(row, globals);

	QRectF rect = poly_gizmo()->get_polygon().boundingRect();
	text_gizmo_->set_rect(rect);
	text_gizmo_->set_html(row[k_text_input].to_string());
}

Qt::Alignment TextGeneratorV3::get_qt_alignment_from_ours(VerticalAlignment v)
{
	switch (v) {
	case k_v_align_top:
		return Qt::AlignTop;
	case k_v_align_middle:
		return Qt::AlignVCenter;
	case k_v_align_bottom:
		return Qt::AlignBottom;
	}
	return Qt::Alignment();
}

TextGeneratorV3::VerticalAlignment
TextGeneratorV3::get_our_alignment_from_qts(Qt::Alignment v)
{
	switch (v) {
	case Qt::AlignTop:
		return k_v_align_top;
	case Qt::AlignVCenter:
		return k_v_align_middle;
	case Qt::AlignBottom:
		return k_v_align_bottom;
	}

	return k_v_align_top;
}

QString TextGeneratorV3::format_string(const QString &input,
									  const QStringList &args)
{
	QString output;
	output.reserve(input.size());

	for (int i = 0; i < input.size(); i++) {
		const QChar &this_char = input.at(i);

		if (i < input.size() - 1 && this_char == '%') {
			const QChar &next_char = input.at(i + 1);
			if (next_char == '%') {
				// Double percent, append a single percent
				output.append('%');
				i++;
			} else if (next_char.isDigit()) {
				// Find length of number
				QString num;
				i++;
				while (i < input.size() && input.at(i).isDigit()) {
					num.append(input.at(i));
					i++;
				}
				i--;
				int index = num.toInt() - 1;
				if (index >= 0 && index < args.size()) {
					output.append(args.at(index));
				}
			} else {
				output.append(this_char);
			}
		} else {
			output.append(this_char);
		}
	}

	return output;
}

void TextGeneratorV3::InputValueChangedEvent(const QString &input, int element)
{
	if (input == k_vertical_alignment_input && !dont_emit_valign_) {
		text_gizmo_->set_vertical_alignment(
			get_qt_alignment_from_ours(get_vertical_alignment()));
	}

	super::InputValueChangedEvent(input, element);
}

void TextGeneratorV3::gizmo_activated()
{
	set_standard_value(k_use_args_input, false);
	connect(text_gizmo_, &TextGizmo::vertical_alignment_changed, this,
			&TextGeneratorV3::set_vertical_alignment_undoable);
	dont_emit_valign_ = true;
}

void TextGeneratorV3::gizmo_deactivated()
{
	set_standard_value(k_use_args_input, true);
	disconnect(text_gizmo_, &TextGizmo::vertical_alignment_changed, this,
			   &TextGeneratorV3::set_vertical_alignment_undoable);
	dont_emit_valign_ = true;
}

void TextGeneratorV3::set_vertical_alignment_undoable(Qt::Alignment a)
{
	EngineCore::instance()->undo_stack()->push(
		new NodeParamSetStandardValueCommand(NodeInput(this,
													   k_vertical_alignment_input),
											 get_our_alignment_from_qts(a)),
		tr("Set Text Vertical Alignment"));
}

}
