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

#include "videoparams.h"

#include <cstdint>

#include <QCoreApplication>
#include <QtMath>

#include "core.h"
#include "ofxImageEffect.h"

namespace olive
{

const int VideoParams::k_internal_channel_count = k_rgba_channel_count;

const Rational VideoParams::k_pixel_aspect_square(1);
const Rational VideoParams::k_pixel_aspect_ntsc_standard(8, 9);
const Rational VideoParams::k_pixel_aspect_ntsc_widescreen(32, 27);
const Rational VideoParams::k_pixel_aspect_pal_standard(16, 15);
const Rational VideoParams::k_pixel_aspect_pal_widescreen(64, 45);
const Rational VideoParams::k_pixel_aspect1080_anamorphic(4, 3);

const QVector<Rational> VideoParams::k_supported_frame_rates = {
	Rational(10, 1), // 10 FPS
	Rational(15, 1), // 15 FPS
	Rational(24000, 1001), // 23.976 FPS
	Rational(24, 1), // 24 FPS
	Rational(25, 1), // 25 FPS
	Rational(30000, 1001), // 29.97 FPS
	Rational(30, 1), // 30 FPS
	Rational(48000, 1001), // 47.952 FPS
	Rational(48, 1), // 48 FPS
	Rational(50, 1), // 50 FPS
	Rational(60000, 1001), // 59.94 FPS
	Rational(60, 1) // 60 FPS
};

const QVector<int> VideoParams::k_supported_dividers = {
	1, 2, 3, 4, 6, 8, 12, 16
};

const QVector<Rational> VideoParams::k_standard_pixel_aspects = {
	VideoParams::k_pixel_aspect_square,
	VideoParams::k_pixel_aspect_ntsc_standard,
	VideoParams::k_pixel_aspect_ntsc_widescreen,
	VideoParams::k_pixel_aspect_pal_standard,
	VideoParams::k_pixel_aspect_pal_widescreen,
	VideoParams::k_pixel_aspect1080_anamorphic
};

VideoParams::VideoParams()
	: width_(0)
	, height_(0)
	, depth_(0)
	, time_base_(0)
	, format_(PixelFormat::invalid)
	, channel_count_(0)
	, pixel_aspect_ratio_(1)
	, interlacing_(Interlacing::k_interlace_none)
	, divider_(1)
{
	calculate_effective_size();
	validate_pixel_aspect_ratio();
	set_defaults_for_footage();
}

VideoParams::VideoParams(int width, int height, PixelFormat format,
						 int nb_channels, const Rational &pixel_aspect_ratio,
						 Interlacing interlacing, int divider)
	: width_(width)
	, height_(height)
	, depth_(1)
	, format_(format)
	, channel_count_(nb_channels)
	, pixel_aspect_ratio_(pixel_aspect_ratio)
	, interlacing_(interlacing)
	, divider_(divider)
{
	calculate_effective_size();
	validate_pixel_aspect_ratio();
	set_defaults_for_footage();
}

VideoParams::VideoParams(int width, int height, int depth, PixelFormat format,
						 int nb_channels, const Rational &pixel_aspect_ratio,
						 VideoParams::Interlacing interlacing, int divider)
	: width_(width)
	, height_(height)
	, depth_(depth)
	, format_(format)
	, channel_count_(nb_channels)
	, pixel_aspect_ratio_(pixel_aspect_ratio)
	, interlacing_(interlacing)
	, divider_(divider)
{
	calculate_effective_size();
	validate_pixel_aspect_ratio();
	set_defaults_for_footage();
}

void VideoParams::set_channel_count(const std::string &ofx_component)
{
	if (ofx_component == kOfxImageComponentAlpha) {
		channel_count_ = 1;
	} else if (ofx_component == kOfxImageComponentRGB) {
		channel_count_ = k_rgb_channel_count;
	} else if (ofx_component == kOfxImageComponentRGBA) {
		channel_count_ = k_rgba_channel_count;
	}
}

VideoParams::VideoParams(int width, int height, const Rational &time_base,
						 PixelFormat format, int nb_channels,
						 const Rational &pixel_aspect_ratio,
						 Interlacing interlacing, int divider)
	: width_(width)
	, height_(height)
	, depth_(1)
	, time_base_(time_base)
	, format_(format)
	, channel_count_(nb_channels)
	, pixel_aspect_ratio_(pixel_aspect_ratio)
	, interlacing_(interlacing)
	, divider_(divider)
	, frame_rate_(time_base.flipped())
{
	calculate_effective_size();
	validate_pixel_aspect_ratio();
	set_defaults_for_footage();
}

int VideoParams::generate_auto_divider(qint64 width, qint64 height)
{
	const int target_res = 1280 * 720;

	qint64 megapixels = width * height;

	double squared_divider = double(megapixels) / double(target_res);
	double divider = qSqrt(squared_divider);

	if (divider <= k_supported_dividers.first()) {
		return k_supported_dividers.first();
	} else if (divider >= k_supported_dividers.last()) {
		return k_supported_dividers.last();
	} else {
		for (int i = 1; i < k_supported_dividers.size(); i++) {
			int prev_divider = k_supported_dividers.at(i - 1);
			int next_divider = k_supported_dividers.at(i);

			if (divider >= prev_divider && divider <= next_divider) {
				double prev_diff = qAbs(prev_divider - divider);
				double next_diff = qAbs(next_divider - divider);

				if (prev_diff < next_diff) {
					return prev_divider;
				} else {
					return next_divider;
				}
			}
		}

		// Fallback
		return 1;
	}
}

bool VideoParams::operator==(const VideoParams &rhs) const
{
	return width() == rhs.width() && height() == rhs.height() &&
		   depth() == rhs.depth() && interlacing() == rhs.interlacing() &&
		   time_base() == rhs.time_base() && format() == rhs.format() &&
		   pixel_aspect_ratio() == rhs.pixel_aspect_ratio() &&
		   divider() == rhs.divider() && channel_count() == rhs.channel_count();
}

bool VideoParams::operator!=(const VideoParams &rhs) const
{
	return !(*this == rhs);
}

int VideoParams::get_bytes_per_channel(PixelFormat format)
{
	switch (format) {
	case PixelFormat::invalid:
	case PixelFormat::count:
		break;
	case PixelFormat::u8:
		return 1;
	case PixelFormat::u10:
		return 0; // packed format, use GetBytesPerPixel instead
	case PixelFormat::u16:
	case PixelFormat::f16:
		return 2;
	case PixelFormat::f32:
		return 4;
	}

	return 0;
}

int VideoParams::get_bytes_per_pixel(PixelFormat format, int channels)
{
	if (format == PixelFormat::u10) {
		// Packed 10-bit RGBA10A2: 4 bytes per RGBA pixel regardless of channel count
		return channels == VideoParams::k_rgba_channel_count ? 4 : 0;
	}
	return get_bytes_per_channel(format) * channels;
}

QString VideoParams::get_name_for_divider(int div)
{
	if (div == 1) {
		return QCoreApplication::translate("VideoParams", "Full");
	} else {
		return QCoreApplication::translate("VideoParams", "1/%1").arg(div);
	}
}

QString VideoParams::get_format_name(PixelFormat format)
{
	switch (format) {
	case PixelFormat::u8:
		return QCoreApplication::translate("VideoParams", "8-bit");
	case PixelFormat::u10:
		return QCoreApplication::translate("VideoParams", "10-bit Packed");
	case PixelFormat::u16:
		return QCoreApplication::translate("VideoParams", "16-bit Integer");
	case PixelFormat::f16:
		return QCoreApplication::translate("VideoParams",
										   "Half-Float (16-bit)");
	case PixelFormat::f32:
		return QCoreApplication::translate("VideoParams",
										   "Full-Float (32-bit)");
	case PixelFormat::invalid:
	case PixelFormat::count:
		break;
	}

	return QCoreApplication::translate("VideoParams", "Unknown (0x%1)")
		.arg(static_cast<int>(format), 0, 16);
}

int VideoParams::get_divider_for_target_resolution(int src_width, int src_height,
											   int dst_width, int dst_height)
{
	int divider = 0;
	int test_width, test_height;

	do {
		divider++;

		test_width = VideoParams::get_scaled_dimension(src_width, divider);
		test_height = VideoParams::get_scaled_dimension(src_height, divider);
	} while (test_width > dst_width || test_height > dst_height);

	return divider;
}

void VideoParams::calculate_effective_size()
{
	effective_width_ = get_scaled_dimension(width(), divider_);
	effective_height_ = get_scaled_dimension(height(), divider_);
	effective_depth_ = (depth() == 1) ? depth() :
										get_scaled_dimension(depth(), divider_);
	calculate_square_pixel_width();
}

void VideoParams::validate_pixel_aspect_ratio()
{
	if (pixel_aspect_ratio_.isNull()) {
		pixel_aspect_ratio_ = 1;
	}
	calculate_square_pixel_width();
}

void VideoParams::set_defaults_for_footage()
{
	enabled_ = true;
	stream_index_ = 0;
	video_type_ = k_video_type_video;
	start_time_ = 0;
	duration_ = 0;
	premultiplied_alpha_ = false;
	x_ = 0;
	y_ = 0;
	color_range_ = k_color_range_default;
}

void VideoParams::calculate_square_pixel_width()
{
	if (pixel_aspect_ratio_.denominator() != 0) {
		par_width_ = qRound(width_ * pixel_aspect_ratio_.to_double());
	} else {
		par_width_ = width_;
	}
}

bool VideoParams::is_valid() const
{
	return (width() > 0 && height() > 0 && !pixel_aspect_ratio_.isNull() &&
			format_ > PixelFormat::invalid && format_ < PixelFormat::count &&
			channel_count_ > 0);
}

QString VideoParams::frame_rate_to_string(const Rational &frame_rate)
{
	return QCoreApplication::translate("VideoParams", "%1 FPS")
		.arg(frame_rate.to_double());
}

QStringList VideoParams::get_standard_pixel_aspect_ratio_names()
{
	QStringList strings = {
		QCoreApplication::translate("VideoParams", "Square Pixels (%1)"),
		QCoreApplication::translate("VideoParams", "NTSC Standard (%1)"),
		QCoreApplication::translate("VideoParams", "NTSC Widescreen (%1)"),
		QCoreApplication::translate("VideoParams", "PAL Standard (%1)"),
		QCoreApplication::translate("VideoParams", "PAL Widescreen (%1)"),
		QCoreApplication::translate("VideoParams", "HD Anamorphic 1080 (%1)")
	};

	// Format each
	for (int i = 0; i < strings.size(); i++) {
		strings.replace(i, format_pixel_aspect_ratio_string(
							   strings.at(i), k_standard_pixel_aspects.at(i)));
	}

	return strings;
}

QString VideoParams::format_pixel_aspect_ratio_string(const QString &format,
												  const Rational &ratio)
{
	return format.arg(QString::number(ratio.to_double(), 'f', 4));
}

int VideoParams::get_scaled_dimension(int dim, int divider)
{
	return dim / divider;
}

int64_t VideoParams::get_time_in_timebase_units(const Rational &time) const
{
	if (time_base_.isNull()) {
		return INT64_MIN; // AV_NOPTS_VALUE
	}

	return Timecode::time_to_timestamp(time, time_base_) + start_time_;
}

void VideoParams::load(QXmlStreamReader *reader)
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("width")) {
			set_width(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("height")) {
			set_height(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("depth")) {
			set_depth(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("timebase")) {
			set_time_base(
				Rational::from_string(reader->readElementText().toStdString()));
		} else if (reader->name() == QStringLiteral("format")) {
			set_format(static_cast<PixelFormat::Format>(
				reader->readElementText().toInt()));
		} else if (reader->name() == QStringLiteral("channelcount")) {
			set_channel_count(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("pixelaspectratio")) {
			set_pixel_aspect_ratio(
				Rational::from_string(reader->readElementText().toStdString()));
		} else if (reader->name() == QStringLiteral("interlacing")) {
			set_interlacing(static_cast<VideoParams::Interlacing>(
				reader->readElementText().toInt()));
		} else if (reader->name() == QStringLiteral("divider")) {
			set_divider(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("enabled")) {
			set_enabled(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("x")) {
			set_x(reader->readElementText().toFloat());
		} else if (reader->name() == QStringLiteral("y")) {
			set_y(reader->readElementText().toFloat());
		} else if (reader->name() == QStringLiteral("streamindex")) {
			set_stream_index(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("videotype")) {
			set_video_type(static_cast<VideoParams::Type>(
				reader->readElementText().toInt()));
		} else if (reader->name() == QStringLiteral("framerate")) {
			set_frame_rate(
				Rational::from_string(reader->readElementText().toStdString()));
		} else if (reader->name() == QStringLiteral("starttime")) {
			set_start_time(reader->readElementText().toLongLong());
		} else if (reader->name() == QStringLiteral("duration")) {
			set_duration(reader->readElementText().toLongLong());
		} else if (reader->name() == QStringLiteral("premultipliedalpha")) {
			set_premultiplied_alpha(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("colorspace")) {
			set_colorspace(reader->readElementText());
		} else if (reader->name() == QStringLiteral("colorrange")) {
			set_color_range(
				static_cast<ColorRange>(reader->readElementText().toInt()));
		} else if (reader->name() == QStringLiteral("colorprimaries")) {
			set_color_primaries(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("colortransfer")) {
			set_color_transfer(reader->readElementText().toInt());
		} else {
			reader->skipCurrentElement();
		}
	}
}

void VideoParams::save(QXmlStreamWriter *writer) const
{
	writer->writeTextElement(QStringLiteral("width"), QString::number(width_));
	writer->writeTextElement(QStringLiteral("height"),
							 QString::number(height_));
	writer->writeTextElement(QStringLiteral("depth"), QString::number(depth_));
	writer->writeTextElement(QStringLiteral("timebase"),
							 QString::fromStdString(time_base_.to_string()));
	writer->writeTextElement(QStringLiteral("format"),
							 QString::number(format_));
	writer->writeTextElement(QStringLiteral("channelcount"),
							 QString::number(channel_count_));
	writer->writeTextElement(
		QStringLiteral("pixelaspectratio"),
		QString::fromStdString(pixel_aspect_ratio_.to_string()));
	writer->writeTextElement(QStringLiteral("interlacing"),
							 QString::number(interlacing_));
	writer->writeTextElement(QStringLiteral("divider"),
							 QString::number(divider_));
	writer->writeTextElement(QStringLiteral("enabled"),
							 QString::number(enabled_));
	writer->writeTextElement(QStringLiteral("x"), QString::number(x_));
	writer->writeTextElement(QStringLiteral("y"), QString::number(y_));
	writer->writeTextElement(QStringLiteral("streamindex"),
							 QString::number(stream_index_));
	writer->writeTextElement(QStringLiteral("videotype"),
							 QString::number(video_type_));
	writer->writeTextElement(QStringLiteral("framerate"),
							 QString::fromStdString(frame_rate_.to_string()));
	writer->writeTextElement(QStringLiteral("starttime"),
							 QString::number(start_time_));
	writer->writeTextElement(QStringLiteral("duration"),
							 QString::number(duration_));
	writer->writeTextElement(QStringLiteral("premultipliedalpha"),
							 QString::number(premultiplied_alpha_));
	writer->writeTextElement(QStringLiteral("colorspace"), colorspace_);
	writer->writeTextElement(QStringLiteral("colorrange"),
							 QString::number(color_range_));
	writer->writeTextElement(QStringLiteral("colorprimaries"),
							 QString::number(color_primaries_));
	writer->writeTextElement(QStringLiteral("colortransfer"),
							 QString::number(color_transfer_));
}

}
