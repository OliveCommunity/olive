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

#include "textv2.h"

#include <cstdio>
#include <vector>

#include <olive/core/util/cpuoptimize.h>

#include "textbackend.h"

namespace olive
{

#define super ShapeNodeBase

enum TextVerticalAlign {
	k_vertical_align_top,
	k_vertical_align_center,
	k_vertical_align_bottom,
};

const std::string TextGeneratorV2::k_text_input = "text_in";
const std::string TextGeneratorV2::k_html_input = "html_in";
const std::string TextGeneratorV2::k_v_align_input = "valign_in";
const std::string TextGeneratorV2::k_font_input = "font_in";
const std::string TextGeneratorV2::k_font_size_input = "font_size_in";

TextGeneratorV2::TextGeneratorV2()
{
	add_input(k_text_input, NodeValue::k_text, "Sample Text");

	add_input(k_html_input, NodeValue::k_boolean, false);

	add_input(k_v_align_input, NodeValue::k_combo, k_vertical_align_top);

	add_input(k_font_input, NodeValue::k_font);

	add_input(k_font_size_input, NodeValue::k_float, 72.0f);

	set_standard_value(k_color_input, Variant::from_value(Color(1.0f, 1.0f, 1.0)));
	set_standard_value(k_size_input, Vector2D(400, 300));

	set_flag(k_dont_show_in_create_menu);
}

std::string TextGeneratorV2::name() const
{
	return "Text (Legacy)";
}

std::string TextGeneratorV2::id() const
{
	return "org.olivevideoeditor.Olive.text2";
}

std::vector<Node::CategoryID> TextGeneratorV2::category() const
{
	return { k_category_generator };
}

std::string TextGeneratorV2::description() const
{
	return "Generate rich text.";
}

void TextGeneratorV2::retranslate()
{
	super::retranslate();

	set_input_name(k_text_input, "Text");
	set_input_name(k_html_input, "Enable HTML");
	set_input_name(k_font_input, "Font");
	set_input_name(k_font_size_input, "Font Size");
	set_input_name(k_v_align_input, "Vertical Align");
	set_combo_box_strings(k_v_align_input, { "Top", "Center", "Bottom" });
}

void TextGeneratorV2::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	if (!value.at(k_text_input).to_string().empty()) {
		GenerateJob job(value);
		auto text_params = globals.vparams();
		text_params.set_format(PixelFormat::f32);
		table->push(NodeValue::k_texture, Texture::job(text_params, job), this);
	}
}

void TextGeneratorV2::generate_frame(FramePtr frame,
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
	// 72 DPI in DPM (72 / 2.54 * 100)
	req.dots_per_meter = 2835;
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

	Vector2D size = job.get(k_size_input).to_vec2();
	req.wrap_width = size.x();

	TextLayoutSize doc;
	if (TextMeasureBackend measure = text_measure_backend()) {
		doc = measure(req);
	}

	double scale = 1.0 / frame->video_params().divider();

	Vector2D pos = job.get(k_position_input).to_vec2();
	double base_offset_x = pos.x() - size.x() / 2 +
						   frame->video_params().width() / 2;
	double base_offset_y = pos.y() - size.y() / 2 +
						   frame->video_params().height() / 2;

	TextVerticalAlign valign = static_cast<TextVerticalAlign>(
		job.get(k_v_align_input).to_int());
	int doc_height = int(doc.height);

	double draw_offset_x = base_offset_x;
	double draw_offset_y = base_offset_y;

	switch (valign) {
	case k_vertical_align_top:
		// Do nothing
		break;
	case k_vertical_align_center:
		// Center align
		draw_offset_y += size.y() / 2 - doc_height / 2;
		break;
	case k_vertical_align_bottom:
		draw_offset_y += size.y() - doc_height;
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
		TextRenderTarget target = { img.data(), width, height, linesize, 1 };
		render(req, t, target);
	} else {
		static bool warned = false;
		if (!warned) {
			fprintf(stderr,
					"TextGeneratorV2: no text backend installed, frame left empty\n");
			warned = true;
		}
	}

	// Transplant alpha channel to frame
	Color rgba = job.get(k_color_input).to_color();
#if defined(OLIVE_PROCESSOR_X86) || defined(OLIVE_PROCESSOR_ARM)
	__m128 sse_color = _mm_loadu_ps(rgba.data());
#endif

	float *frame_dst = reinterpret_cast<float *>(frame->data());
	for (int y = 0; y < height; y++) {
		const unsigned char *src_y = img.data() + size_t(linesize) * y;
		float *dst_y = frame_dst + y * frame->linesize_pixels() *
									   VideoParams::k_rgba_channel_count;

		for (int x = 0; x < width; x++) {
			float alpha = float(src_y[x]) / 255.0f;
			float *dst = dst_y + x * VideoParams::k_rgba_channel_count;

#if defined(OLIVE_PROCESSOR_X86) || defined(OLIVE_PROCESSOR_ARM)
			__m128 sse_alpha = _mm_load1_ps(&alpha);
			__m128 sse_res = _mm_mul_ps(sse_color, sse_alpha);

			_mm_store_ps(dst, sse_res);
#else
			for (int i = 0; i < VideoParams::k_rgba_channel_count; i++) {
				dst[i] = rgba.data()[i] * alpha;
			}
#endif
		}
	}
}

}
