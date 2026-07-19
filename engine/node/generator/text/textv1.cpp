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

#include <QAbstractTextDocumentLayout>
#include <QTextDocument>

namespace olive
{

enum TextVerticalAlign {
	k_vertical_align_top,
	k_vertical_align_center,
	k_vertical_align_bottom,
};

const QString TextGeneratorV1::k_text_input = QStringLiteral("text_in");
const QString TextGeneratorV1::k_html_input = QStringLiteral("html_in");
const QString TextGeneratorV1::k_color_input = QStringLiteral("color_in");
const QString TextGeneratorV1::k_v_align_input = QStringLiteral("valign_in");
const QString TextGeneratorV1::k_font_input = QStringLiteral("font_in");
const QString TextGeneratorV1::k_font_size_input = QStringLiteral("font_size_in");

#define super Node

TextGeneratorV1::TextGeneratorV1()
{
	add_input(k_text_input, NodeValue::k_text, tr("Sample Text"));

	add_input(k_html_input, NodeValue::k_boolean, false);

	add_input(k_color_input, NodeValue::k_color,
			 QVariant::fromValue(Color(1.0f, 1.0f, 1.0)));

	add_input(k_v_align_input, NodeValue::k_combo, 1);

	add_input(k_font_input, NodeValue::k_font);

	add_input(k_font_size_input, NodeValue::k_float, 72.0f);

	set_flag(k_dont_show_in_create_menu);
}

QString TextGeneratorV1::name() const
{
	return tr("Text (Legacy)");
}

QString TextGeneratorV1::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.textgenerator");
}

QVector<Node::CategoryID> TextGeneratorV1::category() const
{
	return { k_category_generator };
}

QString TextGeneratorV1::description() const
{
	return tr("Generate rich text.");
}

void TextGeneratorV1::retranslate()
{
	super::retranslate();

	set_input_name(k_text_input, tr("Text"));
	set_input_name(k_html_input, tr("Enable HTML"));
	set_input_name(k_font_input, tr("Font"));
	set_input_name(k_font_size_input, tr("Font Size"));
	set_input_name(k_color_input, tr("Color"));
	set_input_name(k_v_align_input, tr("Vertical Align"));
	set_combo_box_strings(k_v_align_input, { tr("Top"), tr("Center"), tr("Bottom") });
}

void TextGeneratorV1::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	if (!value[k_text_input].to_string().isEmpty()) {
		table->push(NodeValue::k_texture,
					Texture::job(globals.vparams(), GenerateJob(value)), this);
	}
}

void TextGeneratorV1::generate_frame(FramePtr frame,
									const GenerateJob &job) const
{
	// This could probably be more optimized, but for now we use Qt to draw to a QImage.
	// QImages only support integer pixels and we use float pixels, so what we do here is draw onto
	// a single-channel QImage (alpha only) and then transplant that alpha channel to our float buffer
	// with correct float RGB.
	QImage img(frame->width(), frame->height(), QImage::Format_Grayscale8);
	img.fill(0);

	QTextDocument text_doc;

	// Set default font
	QFont default_font;
	default_font.setFamily(job.get(k_font_input).to_string());
	default_font.setPointSizeF(job.get(k_font_size_input).to_double());
	text_doc.setDefaultFont(default_font);

	// Center by default
	text_doc.setDefaultTextOption(QTextOption(Qt::AlignCenter));

	QString html = job.get(k_text_input).to_string();
	if (job.get(k_html_input).to_bool()) {
		html.replace('\n', QStringLiteral("<br>"));
		text_doc.setHtml(html);
	} else {
		text_doc.setPlainText(html);
	}

	// Align to 80% width because that's considered the "title safe" area
	int tenth_of_width = frame->video_params().width() / 10;
	text_doc.setTextWidth(tenth_of_width * 8);

	// Draw rich text onto image
	QPainter p(&img);
	p.scale(1.0 / frame->video_params().divider(),
			1.0 / frame->video_params().divider());

	// Push 10% inwards to compensate for title safe area
	p.translate(tenth_of_width, 0);

	TextVerticalAlign valign =
		static_cast<TextVerticalAlign>(job.get(k_v_align_input).to_int());
	int doc_height = text_doc.size().height();

	switch (valign) {
	case k_vertical_align_top:
		// Push 10% inwards for title safe area
		p.translate(0, frame->video_params().height() / 10);
		break;
	case k_vertical_align_center:
		// Center align
		p.translate(0, frame->video_params().height() / 2 - doc_height / 2);
		break;
	case k_vertical_align_bottom:
		// Push 10% inwards for title safe area
		p.translate(0, frame->video_params().height() - doc_height -
						   frame->video_params().height() / 10);
		break;
	}

	QAbstractTextDocumentLayout::PaintContext ctx;
	ctx.palette.setColor(QPalette::Text, Qt::white);
	text_doc.documentLayout()->draw(&p, ctx);

	// Transplant alpha channel to frame
	Color rgb = job.get(k_color_input).to_color();
	for (int x = 0; x < frame->width(); x++) {
		for (int y = 0; y < frame->height(); y++) {
			uchar src_alpha = img.bits()[img.bytesPerLine() * y + x];
			float alpha = float(src_alpha) / 255.0f;

			frame->set_pixel(x, y,
							 Color(rgb.red() * alpha, rgb.green() * alpha,
								   rgb.blue() * alpha, alpha));
		}
	}
}

}
