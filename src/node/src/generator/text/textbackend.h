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

#ifndef OAK_TEXTBACKEND_H
#define OAK_TEXTBACKEND_H

#include <string>

namespace olive
{

/**
 * @brief POD description of a text layout/rasterization job.
 *
 * Replaces the QTextDocument/QTextOption/QAbstractTextDocumentLayout usage
 * in the text generator nodes. oaknode carries only the data; the actual
 * text engine (formerly Qt's rich text layout + QPainter rasterizer) is
 * installed by the facade/app layer through the backend hooks at the bottom
 * of this header.
 */
struct TextLayoutRequest {
	enum Mode {
		k_plain_text, // QTextDocument::setPlainText()
		k_html, // QTextDocument::setHtml()
		k_olive_html // Olive's Html::html_to_doc() rich text
	};

	std::string text; // plain text or markup, per mode
	Mode mode = k_plain_text;
	std::string font_family; // default font family (empty = backend default)
	double font_size_pt = 0.0; // default font size in points (0 = default)
	int dots_per_meter = 0; // paint device resolution (0 = backend default)
	double wrap_width = 0.0; // QTextDocument::setTextWidth()
	bool center_horizontally = false; // default QTextOption(Qt::AlignCenter)

	// The backends always paint with white as the default text color
	// (formerly PaintContext palette QPalette::Text = Qt::white); HTML
	// markup may override it per span.
};

/**
 * @brief Laid-out document size (formerly QTextDocument::size()).
 */
struct TextLayoutSize {
	double width = 0.0;
	double height = 0.0;
};

/**
 * @brief Target pixel buffer for text rasterization.
 *
 * channel_count 1 = 8-bit grayscale coverage (formerly
 * QImage::Format_Grayscale8, the node tints it afterwards);
 * channel_count 4 = 8-bit RGBA premultiplied drawn over the existing
 * (cleared) buffer content (formerly QImage::Format_RGBA8888_Premultiplied).
 */
struct TextRenderTarget {
	unsigned char *data;
	int width;
	int height;
	int linesize_bytes;
	int channel_count;
};

/**
 * @brief Draw transform, mirroring the original QPainter calls.
 *
 * The painter first applied `scale(scale, scale)`, then translated to the
 * base offset, set the clip rect (0, 0, clip_width, clip_height) at that
 * base offset, and finally translated by the remaining draw offset before
 * drawing. A point p of the laid-out document therefore lands at
 * ((p.x + draw_offset_x) * scale, (p.y + draw_offset_y) * scale), clipped
 * to the rectangle ((clip_offset_*) * scale, (clip_size) * scale) relative
 * to the same origin. clip_enabled=false means no clip.
 */
struct TextRenderTransform {
	double scale = 1.0;
	double draw_offset_x = 0.0;
	double draw_offset_y = 0.0;
	bool clip_enabled = false;
	double clip_offset_x = 0.0;
	double clip_offset_y = 0.0;
	double clip_width = 0.0;
	double clip_height = 0.0;
};

/**
 * @brief Backend hooks installed by the facade/app layer.
 *
 * measure lays out the request and returns the document size (formerly
 * QTextDocument::size()). render draws the request into the target buffer
 * (formerly QAbstractTextDocumentLayout::draw()). When a hook is null the
 * caller falls back to a zero size / leaves the (already cleared) buffer
 * untouched — a documented behavior gap until the facade installs a text
 * engine.
 */
using TextMeasureBackend = TextLayoutSize (*)(const TextLayoutRequest &request);
using TextRenderBackend = void (*)(const TextLayoutRequest &request,
								  const TextRenderTransform &transform,
								  const TextRenderTarget &target);

inline TextMeasureBackend g_text_measure_backend = nullptr;
inline TextRenderBackend g_text_render_backend = nullptr;

inline void set_text_backends(TextMeasureBackend measure, TextRenderBackend render)
{
	g_text_measure_backend = measure;
	g_text_render_backend = render;
}

inline TextMeasureBackend text_measure_backend()
{
	return g_text_measure_backend;
}

inline TextRenderBackend text_render_backend()
{
	return g_text_render_backend;
}

}

#endif // OAK_TEXTBACKEND_H
