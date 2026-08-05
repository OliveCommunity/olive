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

#include "textv1.h"

#include <cstdio>
#include <vector>

#include "textbackend.h"

namespace olive
{

enum TextVerticalAlign {
	k_vertical_align_top,
	k_vertical_align_center,
	k_vertical_align_bottom,
};

const std::string TextGeneratorV1::k_text_input = "text_in";
const std::string TextGeneratorV1::k_html_input = "html_in";
const std::string TextGeneratorV1::k_color_input = "color_in";
const std::string TextGeneratorV1::k_v_align_input = "valign_in";
const std::string TextGeneratorV1::k_font_input = "font_in";
const std::string TextGeneratorV1::k_font_size_input = "font_size_in";

#define super Node

TextGeneratorV1::TextGeneratorV1()
{
	add_input(k_text_input, NodeValue::k_text, "Sample Text");

	add_input(k_html_input, NodeValue::k_boolean, false);

	add_input(k_color_input, NodeValue::k_color,
			 Variant::from_value(Color(1.0f, 1.0f, 1.0)));

	add_input(k_v_align_input, NodeValue::k_combo, 1);

	add_input(k_font_input, NodeValue::k_font);

	add_input(k_font_size_input, NodeValue::k_float, 72.0f);

	set_flag(k_dont_show_in_create_menu);
}

std::string TextGeneratorV1::name() const
{
	return "Text (Legacy)";
}

std::string TextGeneratorV1::id() const
{
	return "org.olivevideoeditor.Olive.textgenerator";
}

std::vector<Node::CategoryID> TextGeneratorV1::category() const
{
	return { k_category_generator };
}

std::string TextGeneratorV1::description() const
{
	return "Generate rich text.";
}

void TextGeneratorV1::retranslate()
{
	super::retranslate();

	set_input_name(k_text_input, "Text");
	set_input_name(k_html_input, "Enable HTML");
	set_input_name(k_font_input, "Font");
	set_input_name(k_font_size_input, "Font Size");
	set_input_name(k_color_input, "Color");
	set_input_name(k_v_align_input, "Vertical Align");
	set_combo_box_strings(k_v_align_input, { "Top", "Center", "Bottom" });
}

void TextGeneratorV1::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	if (!value.at(k_text_input).to_string().empty()) {
		table->push(NodeValue::k_texture,
					Texture::job(globals.vparams(), GenerateJob(value)), this);
	}
}

void TextGeneratorV1::generate_frame(FramePtr frame,
									const GenerateJob &job) const
{
	// Formerly laid out and rasterized with QTextDocument + QPainter into a
	// grayscale QImage (the alpha channel was then transplanted to the float
	// buffer with the correct RGB). Layout/rasterization now runs behind the
	// facade-installed text backend (see textbackend.h); the coverage buffer
	// handling and the transplant loop are unchanged.
	const int width = frame->width();
	const int height = frame->height();

	// QImage::Format_Grayscale8 scanlines are 32-bit aligned
	const int linesize = (width + 3) & ~3;
	std::vector<unsigned char> img(size_t(linesize) * height, 0);

	TextLayoutRequest req;
	req.text = job.get(k_text_input).to_string();
	req.font_family = job.get(k_font_input).to_string();
	req.font_size_pt =
		job.get(k_font_size_input).to_double();
	// Center by default (formerly QTextOption(Qt::AlignCenter))
	req.center_horizontally = true;
	if (job.get(k_html_input).to_bool()) {
		req.mode = TextLayoutRequest::k_html;
		// QTextDocument::setHtml() doesn't translate newlines, so they were
		// replaced with <br> tags first
		std::string::size_type nl = 0;
		while ((nl = req.text.find('\n', nl)) != std::string::npos) {
			req.text.replace(nl, 1, "<br>");
			nl += 4;
		}
	}

	// Align to 80% width because that's considered the "title safe" area
	int tenth_of_width = frame->video_params().width() / 10;
	req.wrap_width = tenth_of_width * 8;

	TextLayoutSize doc;
	if (TextMeasureBackend measure = text_measure_backend()) {
		doc = measure(req);
	}

	double scale = 1.0 / frame->video_params().divider();

	// Push 10% inwards to compensate for title safe area
	double offset_x = tenth_of_width;
	double offset_y = 0;

	TextVerticalAlign valign = static_cast<TextVerticalAlign>(
		job.get(k_v_align_input).to_int());
	int doc_height = int(doc.height);

	switch (valign) {
	case k_vertical_align_top:
		// Push 10% inwards for title safe area
		offset_y = frame->video_params().height() / 10;
		break;
	case k_vertical_align_center:
		// Center align
		offset_y = frame->video_params().height() / 2 - doc_height / 2;
		break;
	case k_vertical_align_bottom:
		// Push 10% inwards for title safe area
		offset_y = frame->video_params().height() - doc_height -
				   frame->video_params().height() / 10;
		break;
	}

	if (TextRenderBackend render = text_render_backend()) {
		TextRenderTransform t;
		t.scale = scale;
		t.draw_offset_x = offset_x;
		t.draw_offset_y = offset_y;
		TextRenderTarget target = { img.data(), width, height, linesize, 1 };
		render(req, t, target);
	} else {
		static bool warned = false;
		if (!warned) {
			fprintf(stderr,
					"TextGeneratorV1: no text backend installed, frame left empty\n");
			warned = true;
		}
	}

	// Transplant alpha channel to frame
	Color rgb = job.get(k_color_input).to_color();
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			unsigned char src_alpha = img[size_t(linesize) * y + x];
			float alpha = float(src_alpha) / 255.0f;

			frame->set_pixel(x, y,
							 Color(rgb.red() * alpha, rgb.green() * alpha,
								   rgb.blue() * alpha, alpha));
		}
	}
}

}
