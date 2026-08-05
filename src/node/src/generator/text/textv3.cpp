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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "coreengine.h"
#include "project.h"
#include "nodeundo.h"
#include "textbackend.h"

namespace olive
{

#define super ShapeNodeBase

enum TextVerticalAlign {
	k_vertical_align_top,
	k_vertical_align_center,
	k_vertical_align_bottom,
};

const std::string TextGeneratorV3::k_text_input = "text_in";
const std::string TextGeneratorV3::k_vertical_alignment_input = "valign_in";
const std::string TextGeneratorV3::k_use_args_input = "use_args_in";
const std::string TextGeneratorV3::k_args_input = "args_in";

TextGeneratorV3::TextGeneratorV3()
	: ShapeNodeBase(false)
	, dont_emit_valign_(false)
{
	add_input(k_text_input, NodeValue::k_text,
			 std::string("<p style='font-size: 72pt; color: white;'>") +
				 "Sample Text" + "</p>");
	set_input_property(k_text_input, "vieweronly", true);

	set_standard_value(k_size_input, Vector2D(400, 300));

	add_input(k_vertical_alignment_input, NodeValue::k_combo,
			 InputFlags(k_input_flag_hidden | k_input_flag_static));

	add_input(k_use_args_input, NodeValue::k_boolean, true,
			 InputFlags(k_input_flag_hidden | k_input_flag_static));

	add_input(k_args_input, NodeValue::k_text, InputFlags(k_input_flag_array));
	set_input_property(k_args_input, "arraystart", 1);

	text_gizmo_ = new TextGizmo(this);
	add_gizmo(text_gizmo_);
	text_gizmo_->set_input(NodeInput(this, k_text_input));
	// The TextGizmo activated/deactivated signal connections were removed
	// with QObject; the gizmo wave / facade invokes gizmo_activated() and
	// gizmo_deactivated() directly.
}

std::string TextGeneratorV3::name() const
{
	return "Text";
}

std::string TextGeneratorV3::id() const
{
	return "org.olivevideoeditor.Olive.text3";
}

std::vector<Node::CategoryID> TextGeneratorV3::category() const
{
	return { k_category_generator };
}

std::string TextGeneratorV3::description() const
{
	return "Generate rich text.";
}

void TextGeneratorV3::retranslate()
{
	super::retranslate();

	set_input_name(k_text_input, "Text");
	set_input_name(k_vertical_alignment_input, "Vertical Alignment");
	set_combo_box_strings(k_vertical_alignment_input,
						 { "Top", "Middle", "Bottom" });
	set_input_name(k_args_input, "Arguments");
}

void TextGeneratorV3::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	std::string text = value.at(k_text_input).to_string();

	if (value.at(k_use_args_input).to_bool()) {
		auto args = value.at(k_args_input).to_array();
		if (!args.empty()) {
			StringList list;
			list.reserve(args.size());
			for (size_t i = 0; i < args.size(); i++) {
				list.push_back(args[int(i)].to_string());
			}

			text = format_string(text, list);
		}
	}

	if (!text.empty()) {
		TexturePtr base = value.at(k_text_input).to_texture();

		VideoParams text_params = base ? base->params() : globals.vparams();
		text_params.set_format(PixelFormat::u8);
		text_params.set_colorspace(
			project()->color_manager()->get_default_input_color_space());

		GenerateJob job(value);
		job.insert(k_text_input, NodeValue(NodeValue::k_text, text));

		push_mergable_job(value, Texture::job(text_params, job), table);
	} else if (value.at(k_base_input).to_texture()) {
		table->push(value.at(k_base_input));
	}
}

void TextGeneratorV3::generate_frame(FramePtr frame,
									const GenerateJob &job) const
{
	// Formerly laid out and rasterized with QTextDocument (fed through
	// Html::html_to_doc) + QPainter directly over the frame buffer
	// (QImage::Format_RGBA8888_Premultiplied, filled Qt::transparent).
	// Layout/rasterization now runs behind the facade-installed text backend
	// (see textbackend.h); the buffer handling and offset math are unchanged.
	unsigned char *img = reinterpret_cast<unsigned char *>(frame->data());
	const int width = frame->width();
	const int height = frame->height();
	const int linesize = frame->linesize_bytes();

	// Clear to transparent (formerly QImage::fill(Qt::transparent))
	for (int y = 0; y < height; y++) {
		std::memset(img + size_t(linesize) * y, 0, size_t(width) * 4);
	}

	if (TextMeasureBackend measure = text_measure_backend()) {
		TextLayoutRequest req;
		req.text = job.get(k_text_input).to_string();
		req.mode = TextLayoutRequest::k_olive_html;
		// 96 DPI in DPM (96 / 2.54 * 100)
		req.dots_per_meter = 3780;

		Vector2D size = job.get(k_size_input).to_vec2();
		req.wrap_width = size.x();

		TextLayoutSize doc = measure(req);

		double scale = 1.0 / frame->video_params().divider();

		Vector2D pos =
			job.get(k_position_input).to_vec2();
		double base_offset_x = pos.x() - size.x() / 2 +
							   frame->video_params().width() / 2;
		double base_offset_y = pos.y() - size.y() / 2 +
							   frame->video_params().height() / 2;

		double draw_offset_x = base_offset_x;
		double draw_offset_y = base_offset_y;

		switch (static_cast<VerticalAlignment>(
			job.get(k_vertical_alignment_input)
				.to_int())) {
		case k_v_align_top:
			// Do nothing
			break;
		case k_v_align_middle:
			draw_offset_y += size.y() / 2 - doc.height / 2;
			break;
		case k_v_align_bottom:
			draw_offset_y += size.y() - doc.height;
			break;
		}

		if (TextRenderBackend render = text_render_backend()) {
			TextRenderTransform t;
			t.scale = scale;
			t.draw_offset_x = draw_offset_x;
			t.draw_offset_y = draw_offset_y;
			// The clip rect was set before the vertical alignment translate
			t.clip_enabled = true;
			t.clip_offset_x = base_offset_x;
			t.clip_offset_y = base_offset_y;
			t.clip_width = size.x();
			t.clip_height = size.y();
			TextRenderTarget target = { img, width, height, linesize, 4 };
			render(req, t, target);
		}
	} else {
		static bool warned = false;
		if (!warned) {
			fprintf(stderr,
					"TextGeneratorV3: no text backend installed, frame left empty\n");
			warned = true;
		}
	}
}

void TextGeneratorV3::update_gizmo_positions(const NodeValueRow &row,
										   const NodeGlobals &globals)
{
	super::update_gizmo_positions(row, globals);

	// QPolygonF::boundingRect() over the gizmo polygon (QPolygonF is now
	// std::vector<PointF>)
	RectF rect;
	const std::vector<PointF> &poly = poly_gizmo()->get_polygon();
	if (!poly.empty()) {
		double min_x = poly.front().x();
		double min_y = poly.front().y();
		double max_x = min_x;
		double max_y = min_y;
		for (const PointF &p : poly) {
			min_x = p.x() < min_x ? p.x() : min_x;
			min_y = p.y() < min_y ? p.y() : min_y;
			max_x = p.x() > max_x ? p.x() : max_x;
			max_y = p.y() > max_y ? p.y() : max_y;
		}
		rect = RectF(min_x, min_y, max_x - min_x, max_y - min_y);
	}
	text_gizmo_->set_rect(rect);
	text_gizmo_->set_html(row.at(k_text_input).to_string());
}

int TextGeneratorV3::get_qt_alignment_from_ours(VerticalAlignment v)
{
	switch (v) {
	case k_v_align_top:
		return TextGizmo::k_align_top;
	case k_v_align_middle:
		return TextGizmo::k_align_vcenter;
	case k_v_align_bottom:
		return TextGizmo::k_align_bottom;
	}
	return 0;
}

TextGeneratorV3::VerticalAlignment
TextGeneratorV3::get_our_alignment_from_qts(int v)
{
	switch (v) {
	case TextGizmo::k_align_top:
		return k_v_align_top;
	case TextGizmo::k_align_vcenter:
		return k_v_align_middle;
	case TextGizmo::k_align_bottom:
		return k_v_align_bottom;
	}

	return k_v_align_top;
}

std::string TextGeneratorV3::format_string(const std::string &input,
										  const StringList &args)
{
	std::string output;
	output.reserve(input.size());

	// The original iterated UTF-16 code units; scanning bytes is equivalent
	// here because '%' and the ASCII digits are single-byte in UTF-8 and
	// multi-byte sequences are copied through verbatim
	for (size_t i = 0; i < input.size(); i++) {
		const char this_char = input.at(i);

		if (i < input.size() - 1 && this_char == '%') {
			const char next_char = input.at(i + 1);
			if (next_char == '%') {
				// Double percent, append a single percent
				output.push_back('%');
				i++;
			} else if (next_char >= '0' && next_char <= '9') {
				// Find length of number
				std::string num;
				i++;
				while (i < input.size() && input.at(i) >= '0' &&
					   input.at(i) <= '9') {
					num.push_back(input.at(i));
					i++;
				}
				i--;
				// QString::toInt() semantics: out-of-int-range parses fail
				// and yield 0, making the index -1
				long long n = strtoll(num.c_str(), nullptr, 10);
				int index = (n > INT32_MAX || n < INT32_MIN) ? -1 : int(n) - 1;
				if (index >= 0 && index < int(args.size())) {
					output.append(args.at(index));
				}
			} else {
				output.push_back(this_char);
			}
		} else {
			output.push_back(this_char);
		}
	}

	return output;
}

void TextGeneratorV3::InputValueChangedEvent(const std::string &input, int element)
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
	// The TextGizmo vertical_alignment_changed signal connection was removed
	// with QObject; the gizmo wave / facade invokes
	// set_vertical_alignment_undoable() directly.
	dont_emit_valign_ = true;
}

void TextGeneratorV3::gizmo_deactivated()
{
	set_standard_value(k_use_args_input, true);
	// See gizmo_activated()
	dont_emit_valign_ = true;
}

void TextGeneratorV3::set_vertical_alignment_undoable(int a)
{
	EngineCore::instance()->undo_stack()->push(
		new NodeParamSetStandardValueCommand(NodeInput(this,
													   k_vertical_alignment_input),
											 int(get_our_alignment_from_qts(a))),
		"Set Text Vertical Alignment");
}

}
