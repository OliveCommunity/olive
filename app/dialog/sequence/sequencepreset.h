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

#ifndef OAK_SEQUENCEPARAM_H
#define OAK_SEQUENCEPARAM_H

#include <olive/core/core.h>
#include <QXmlStreamWriter>

#include "oakutil/xmlutils.h"
#include "dialog/sequence/presetmanager.h"

namespace olive
{

// Same namespace bridge the engine's render/videoparams.h used to provide
// (unqualified Rational/PixelFormat inside namespace olive).
using namespace core;

class SequencePreset : public Preset {
public:
	SequencePreset() = default;

	SequencePreset(const QString &name, int width, int height,
				   const Rational &frame_rate, const Rational &pixel_aspect,
				   int interlacing, int sample_rate,
				   uint64_t channel_layout, int preview_divider,
				   PixelFormat preview_format, bool preview_autocache)
		: width_(width)
		, height_(height)
		, frame_rate_(frame_rate)
		, pixel_aspect_(pixel_aspect)
		, interlacing_(interlacing)
		, sample_rate_(sample_rate)
		, channel_layout_(channel_layout)
		, preview_divider_(preview_divider)
		, preview_format_(preview_format)
		, preview_autocache_(preview_autocache)
	{
		set_name(name);
	}

	virtual void load(QXmlStreamReader *reader) override
	{
		while (xml_read_next_start_element(reader)) {
			if (reader->name() == QStringLiteral("name")) {
				set_name(reader->readElementText());
			} else if (reader->name() == QStringLiteral("width")) {
				width_ = reader->readElementText().toInt();
			} else if (reader->name() == QStringLiteral("height")) {
				height_ = reader->readElementText().toInt();
			} else if (reader->name() == QStringLiteral("framerate")) {
				frame_rate_ = Rational::from_string(
					reader->readElementText().toStdString());
			} else if (reader->name() == QStringLiteral("pixelaspect")) {
				pixel_aspect_ = Rational::from_string(
					reader->readElementText().toStdString());
			} else if (reader->name() == QStringLiteral("interlacing") ||
					   reader->name() == QStringLiteral("interlacing_")) {
				// "interlacing_" is the element name mistakenly written by
				// older versions of Save(); accept it for backward compatibility
				interlacing_ = static_cast<int>(
					reader->readElementText().toInt());
			} else if (reader->name() == QStringLiteral("samplerate")) {
				sample_rate_ = reader->readElementText().toInt();
			} else if (reader->name() == QStringLiteral("chlayout")) {
				channel_layout_ = reader->readElementText().toULongLong();
			} else if (reader->name() == QStringLiteral("divider")) {
				preview_divider_ = reader->readElementText().toInt();
			} else if (reader->name() == QStringLiteral("format")) {
				preview_format_ = static_cast<PixelFormat::Format>(
					reader->readElementText().toInt());
			} else if (reader->name() == QStringLiteral("autocache")) {
				preview_autocache_ = reader->readElementText().toInt();
			} else {
				reader->skipCurrentElement();
			}
		}
	}

	virtual void save(QXmlStreamWriter *writer) const override
	{
		writer->writeTextElement(QStringLiteral("name"), get_name());
		writer->writeTextElement(QStringLiteral("width"),
								 QString::number(width_));
		writer->writeTextElement(QStringLiteral("height"),
								 QString::number(height_));
		writer->writeTextElement(
			QStringLiteral("framerate"),
			QString::fromStdString(frame_rate_.to_string()));
		writer->writeTextElement(
			QStringLiteral("pixelaspect"),
			QString::fromStdString(pixel_aspect_.to_string()));
		writer->writeTextElement(QStringLiteral("interlacing"),
								 QString::number(interlacing_));
		writer->writeTextElement(QStringLiteral("samplerate"),
								 QString::number(sample_rate_));
		writer->writeTextElement(QStringLiteral("chlayout"),
								 QString::number(channel_layout_));
		writer->writeTextElement(QStringLiteral("divider"),
								 QString::number(preview_divider_));
		writer->writeTextElement(QStringLiteral("format"),
								 QString::number(preview_format_));
		writer->writeTextElement(QStringLiteral("autocache"),
								 QString::number(preview_autocache_));
	}

	int width() const
	{
		return width_;
	}

	int height() const
	{
		return height_;
	}

	const Rational &frame_rate() const
	{
		return frame_rate_;
	}

	const Rational &pixel_aspect() const
	{
		return pixel_aspect_;
	}

	int interlacing() const
	{
		return interlacing_;
	}

	int sample_rate() const
	{
		return sample_rate_;
	}

	uint64_t channel_layout() const
	{
		return channel_layout_;
	}

	int preview_divider() const
	{
		return preview_divider_;
	}

	PixelFormat preview_format() const
	{
		return preview_format_;
	}

	bool preview_autocache() const
	{
		return preview_autocache_;
	}

private:
	int width_;
	int height_;
	Rational frame_rate_;
	Rational pixel_aspect_;
	int interlacing_;
	int sample_rate_;
	uint64_t channel_layout_;
	int preview_divider_;
	PixelFormat preview_format_;
	bool preview_autocache_;
};

}

#endif // OAK_SEQUENCEPARAM_H
