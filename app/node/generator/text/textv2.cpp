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

#include <olive/core/core.h>
#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QTextDocument>

namespace olive
{

#define super ShapeNodeBase

enum TextVerticalAlign {
	k_vertical_align_top,
	k_vertical_align_center,
	k_vertical_align_bottom,
};

const QString TextGeneratorV2::k_text_input = QStringLiteral("text_in");
const QString TextGeneratorV2::k_html_input = QStringLiteral("html_in");
const QString TextGeneratorV2::k_v_align_input = QStringLiteral("valign_in");
const QString TextGeneratorV2::k_font_input = QStringLiteral("font_in");
const QString TextGeneratorV2::k_font_size_input = QStringLiteral("font_size_in");

TextGeneratorV2::TextGeneratorV2()
{
	add_input(k_text_input, NodeValue::k_text, tr("Sample Text"));

	add_input(k_html_input, NodeValue::k_boolean, false);

	add_input(k_v_align_input, NodeValue::k_combo, k_vertical_align_top);

	add_input(k_font_input, NodeValue::k_font);

	add_input(k_font_size_input, NodeValue::k_float, 72.0f);

	set_standard_value(k_color_input, QVariant::fromValue(Color(1.0f, 1.0f, 1.0)));
	set_standard_value(k_size_input, QVector2D(400, 300));

	set_flag(k_dont_show_in_create_menu);
}

QString TextGeneratorV2::name() const
{
	return tr("Text (Legacy)");
}

QString TextGeneratorV2::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.text2");
}

QVector<Node::CategoryID> TextGeneratorV2::category() const
{
	return { k_category_generator };
}

QString TextGeneratorV2::description() const
{
	return tr("Generate rich text.");
}

void TextGeneratorV2::retranslate()
{
	super::retranslate();

	set_input_name(k_text_input, tr("Text"));
	set_input_name(k_html_input, tr("Enable HTML"));
	set_input_name(k_font_input, tr("Font"));
	set_input_name(k_font_size_input, tr("Font Size"));
	set_input_name(k_v_align_input, tr("Vertical Align"));
	set_combo_box_strings(k_v_align_input, { tr("Top"), tr("Center"), tr("Bottom") });
}

void TextGeneratorV2::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	if (!value[k_text_input].to_string().isEmpty()) {
		GenerateJob job(value);
		auto text_params = globals.vparams();
		text_params.set_format(PixelFormat::f32);
		table->push(NodeValue::k_texture, Texture::job(text_params, job), this);
	}
}

void TextGeneratorV2::generate_frame(FramePtr frame,
									const GenerateJob &job) const
{
	// This could probably be more optimized, but for now we use Qt to draw to a QImage.
	// QImages only support integer pixels and we use float pixels, so what we do here is draw onto
	// a single-channel QImage (alpha only) and then transplant that alpha channel to our float buffer
	// with correct float RGB.
	QImage img(frame->width(), frame->height(), QImage::Format_Grayscale8);
	img.fill(Qt::transparent);

	// 72 DPI in DPM (72 / 2.54 * 100)
	const int dpm = 2835;
	img.setDotsPerMeterX(dpm);
	img.setDotsPerMeterY(dpm);

	QTextDocument text_doc;
	text_doc.documentLayout()->setPaintDevice(&img);

	// Set default font
	QFont default_font;
	default_font.setFamily(job.get(k_font_input).to_string());
	default_font.setPointSizeF(job.get(k_font_size_input).to_double());
	text_doc.setDefaultFont(default_font);

	QString html = job.get(k_text_input).to_string();
	if (job.get(k_html_input).to_bool()) {
		html.replace('\n', QStringLiteral("<br>"));
		text_doc.setHtml(html);
	} else {
		text_doc.setPlainText(html);
	}

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

	TextVerticalAlign valign =
		static_cast<TextVerticalAlign>(job.get(k_v_align_input).to_int());
	int doc_height = text_doc.size().height();

	switch (valign) {
	case k_vertical_align_top:
		// Do nothing
		break;
	case k_vertical_align_center:
		// Center align
		p.translate(0, size.y() / 2 - doc_height / 2);
		break;
	case k_vertical_align_bottom:
		p.translate(0, size.y() - doc_height);
		break;
	}

	QAbstractTextDocumentLayout::PaintContext ctx;
	ctx.palette.setColor(QPalette::Text, Qt::white);

	text_doc.documentLayout()->draw(&p, ctx);

	// Transplant alpha channel to frame
	Color rgba = job.get(k_color_input).to_color();
#if defined(Q_PROCESSOR_X86) || defined(Q_PROCESSOR_ARM)
	__m128 sse_color = _mm_loadu_ps(rgba.data());
#endif

	float *frame_dst = reinterpret_cast<float *>(frame->data());
	for (int y = 0; y < frame->height(); y++) {
		uchar *src_y = img.bits() + img.bytesPerLine() * y;
		float *dst_y = frame_dst + y * frame->linesize_pixels() *
									   VideoParams::k_rgba_channel_count;

		for (int x = 0; x < frame->width(); x++) {
			float alpha = float(src_y[x]) / 255.0f;
			float *dst = dst_y + x * VideoParams::k_rgba_channel_count;

#if defined(Q_PROCESSOR_X86) || defined(Q_PROCESSOR_ARM)
			__m128 sse_alpha = _mm_load1_ps(&alpha);
			__m128 sse_res = _mm_mul_ps(sse_color, sse_alpha);

			_mm_store_ps(dst, sse_res);
#else
			for (int i = 0; i < VideoParams::kRGBAChannelCount; i++) {
				dst[i] = rgba.data()[i] * alpha;
			}
#endif
		}
	}
}

}
