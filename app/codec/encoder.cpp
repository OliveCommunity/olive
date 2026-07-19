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

#include "encoder.h"

#include <QFile>

#include "common/xmlutils.h"
#include "ffmpeg/ffmpegencoder.h"
#include "oiio/oiioencoder.h"

namespace olive
{

const QRegularExpression Encoder::k_image_sequence_contains_digits =
	QRegularExpression(QStringLiteral("\\[[#]+\\]"));
const QRegularExpression Encoder::k_image_sequence_remove_digits =
	QRegularExpression(QStringLiteral("[\\-\\.\\ \\_]?\\[[#]+\\]"));

Encoder::Encoder(const EncodingParams &params)
	: params_(params)
{
}

const EncodingParams &Encoder::params() const
{
	return params_;
}

QString Encoder::get_filename_for_frame(const Rational &frame)
{
	if (params().video_is_image_sequence()) {
		// Transform!
		int64_t frame_index = Timecode::time_to_timestamp(
			frame, params().video_params().frame_rate_as_time_base());
		int digits = get_image_sequence_placeholder_digit_count(params().filename());
		QString frame_index_str =
			QStringLiteral("%1").arg(frame_index, digits, 10, QChar('0'));

		QString f = params_.filename();
		f.replace(k_image_sequence_contains_digits, frame_index_str);
		return f;
	} else {
		// Keep filename
		return params_.filename();
	}
}

int Encoder::get_image_sequence_placeholder_digit_count(const QString &filename)
{
	int start = filename.indexOf(k_image_sequence_contains_digits);
	int digit_count = 0;
	for (int i = start + 1; i < filename.size(); i++) {
		if (filename.at(i) == '#') {
			digit_count++;
		} else {
			break;
		}
	}
	return digit_count;
}

bool Encoder::filename_contains_digit_placeholder(const QString &filename)
{
	return filename.contains(k_image_sequence_contains_digits);
}

QString Encoder::filename_remove_digit_placeholder(QString filename)
{
	return filename.remove(k_image_sequence_remove_digits);
}

EncodingParams::EncodingParams()
	: video_enabled_(false)
	, video_bit_rate_(0)
	, video_min_bit_rate_(0)
	, video_max_bit_rate_(0)
	, video_buffer_size_(0)
	, video_threads_(0)
	, video_is_image_sequence_(false)
	, audio_enabled_(false)
	, audio_bit_rate_(0)
	, subtitles_enabled_(false)
	, subtitles_are_sidecar_(false)
	, video_scaling_method_(k_stretch)
	, has_custom_range_(false)
{
}

QDir EncodingParams::get_preset_path()
{
	return QDir(FileFunctions::get_configuration_location())
		.filePath(QStringLiteral("exportpresets"));
}

QStringList EncodingParams::get_list_of_presets()
{
	QDir d = EncodingParams::get_preset_path();
	return d.entryList(QDir::Files);
}

void EncodingParams::enable_video(const VideoParams &video_params,
								 const ExportCodec::Codec &vcodec)
{
	video_enabled_ = true;
	video_params_ = video_params;
	video_codec_ = vcodec;
}

void EncodingParams::enable_audio(const AudioParams &audio_params,
								 const ExportCodec::Codec &acodec)
{
	audio_enabled_ = true;
	audio_params_ = audio_params;
	audio_codec_ = acodec;
}

void EncodingParams::enable_subtitles(const ExportCodec::Codec &scodec)
{
	subtitles_enabled_ = true;
	subtitles_codec_ = scodec;
}

void EncodingParams::enable_sidecar_subtitles(const ExportFormat::Format &sfmt,
											const ExportCodec::Codec &scodec)
{
	subtitles_enabled_ = true;
	subtitles_are_sidecar_ = true;
	subtitle_sidecar_fmt_ = sfmt;
	subtitles_codec_ = scodec;
}

void EncodingParams::disable_video()
{
	video_enabled_ = false;
}

void EncodingParams::disable_audio()
{
	audio_enabled_ = false;
}

void EncodingParams::disable_subtitles()
{
	subtitles_enabled_ = false;
}

bool EncodingParams::load(QXmlStreamReader *reader)
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("export")) {
			int version = 0;

			XMLAttributeLoop(reader, attr)
			{
				if (attr.name() == QStringLiteral("version")) {
					version = attr.value().toInt();
				}
			}

			switch (version) {
			case 1:
				return load_v1(reader);
			}
		} else {
			reader->skipCurrentElement();
		}
	}

	return false;
}

bool EncodingParams::load(QIODevice *device)
{
	QXmlStreamReader reader(device);
	return load(&reader);
}

void EncodingParams::save(QIODevice *device) const
{
	QXmlStreamWriter writer(device);
	save(&writer);
}

void EncodingParams::save(QXmlStreamWriter *writer) const
{
	writer->writeStartDocument();

	writer->writeStartElement(QStringLiteral("export"));

	writer->writeAttribute(QStringLiteral("version"),
						   QString::number(k_encoder_params_version));

	writer->writeTextElement(QStringLiteral("filename"), filename_);
	writer->writeTextElement(QStringLiteral("format"),
							 QString::number(format_));

	writer->writeTextElement(QStringLiteral("range"),
							 QString::number(has_custom_range_));
	writer->writeTextElement(
		QStringLiteral("customrangein"),
		QString::fromStdString(custom_range_.in().to_string()));
	writer->writeTextElement(
		QStringLiteral("customrangeout"),
		QString::fromStdString(custom_range_.out().to_string()));

	writer->writeStartElement(QStringLiteral("video"));

	writer->writeAttribute(QStringLiteral("enabled"),
						   QString::number(video_enabled_));

	if (video_enabled_) {
		writer->writeTextElement(QStringLiteral("codec"),
								 QString::number(video_codec_));
		writer->writeTextElement(QStringLiteral("width"),
								 QString::number(video_params_.width()));
		writer->writeTextElement(QStringLiteral("height"),
								 QString::number(video_params_.height()));
		writer->writeTextElement(QStringLiteral("format"),
								 QString::number(video_params_.format()));
		writer->writeTextElement(
			QStringLiteral("pixelaspect"),
			QString::fromStdString(
				video_params_.pixel_aspect_ratio().to_string()));
		writer->writeTextElement(
			QStringLiteral("timebase"),
			QString::fromStdString(video_params_.time_base().to_string()));
		writer->writeTextElement(QStringLiteral("divider"),
								 QString::number(video_params_.divider()));
		writer->writeTextElement(QStringLiteral("bitrate"),
								 QString::number(video_bit_rate_));
		writer->writeTextElement(QStringLiteral("minbitrate"),
								 QString::number(video_min_bit_rate_));
		writer->writeTextElement(QStringLiteral("maxbitrate"),
								 QString::number(video_max_bit_rate_));
		writer->writeTextElement(QStringLiteral("bufsize"),
								 QString::number(video_buffer_size_));
		writer->writeTextElement(QStringLiteral("threads"),
								 QString::number(video_threads_));
		writer->writeTextElement(QStringLiteral("pixfmt"), video_pix_fmt_);
		writer->writeTextElement(QStringLiteral("imgseq"),
								 QString::number(video_is_image_sequence_));

		writer->writeStartElement(QStringLiteral("color"));
		writer->writeTextElement(QStringLiteral("output"),
								 color_transform_.output());
		writer->writeEndElement(); // colortransform

		writer->writeTextElement(QStringLiteral("vscale"),
								 QString::number(video_scaling_method_));

		if (!video_opts_.isEmpty()) {
			writer->writeStartElement(QStringLiteral("opts"));

			QHash<QString, QString>::const_iterator i;
			for (i = video_opts_.constBegin(); i != video_opts_.constEnd();
				 i++) {
				writer->writeStartElement(QStringLiteral("entry"));

				writer->writeTextElement(QStringLiteral("key"), i.key());
				writer->writeTextElement(QStringLiteral("value"), i.value());

				writer->writeEndElement(); // entry
			}

			writer->writeEndElement(); // opts
		}
	}

	writer->writeEndElement(); // video

	writer->writeStartElement(QStringLiteral("audio"));

	writer->writeAttribute(QStringLiteral("enabled"),
						   QString::number(audio_enabled_));

	if (audio_enabled_) {
		writer->writeTextElement(QStringLiteral("codec"),
								 QString::number(audio_codec_));
		writer->writeTextElement(QStringLiteral("samplerate"),
								 QString::number(audio_params_.sample_rate()));

		writer->writeTextElement(
			QStringLiteral("channellayout"),
			QString::number(audio_params().channel_layout()));
		writer->writeTextElement(
			QStringLiteral("format"),
			QString::fromStdString(audio_params_.format().to_string()));
		writer->writeTextElement(QStringLiteral("bitrate"),
								 QString::number(audio_bit_rate_));
	}

	writer->writeStartElement(QStringLiteral("subtitles"));

	writer->writeAttribute(QStringLiteral("enabled"),
						   QString::number(subtitles_enabled_));

	if (subtitles_enabled_) {
		writer->writeTextElement(QStringLiteral("sidecar"),
								 QString::number(subtitles_are_sidecar_));
		writer->writeTextElement(QStringLiteral("sidecarformat"),
								 QString::number(subtitle_sidecar_fmt_));

		writer->writeTextElement(QStringLiteral("codec"),
								 QString::number(subtitles_codec_));
	}

	writer->writeEndElement(); // subtitles

	writer->writeEndElement(); // audio

	writer->writeEndElement(); // export

	writer->writeEndDocument();
}

Encoder *Encoder::create_from_id(Type id, const EncodingParams &params)
{
	switch (id) {
	case k_encoder_type_none:
		break;
	case k_encoder_type_f_fmpeg:
		return new FFmpegEncoder(params);
	case k_encoder_type_oiio:
		return new OIIOEncoder(params);
	}

	return nullptr;
}

Encoder::Type Encoder::get_type_from_format(ExportFormat::Format f)
{
	switch (f) {
	case ExportFormat::k_format_d_nx_hd:
	case ExportFormat::k_format_matroska:
	case ExportFormat::k_format_quick_time:
	case ExportFormat::k_format_mpe_g4_video:
	case ExportFormat::k_format_mpe_g4_audio:
	case ExportFormat::k_format_wav:
	case ExportFormat::k_format_aiff:
	case ExportFormat::k_format_m_p3:
	case ExportFormat::k_format_flac:
	case ExportFormat::k_format_ogg:
	case ExportFormat::k_format_web_m:
	case ExportFormat::k_format_srt:
		return k_encoder_type_f_fmpeg;
	case ExportFormat::k_format_open_exr:
	case ExportFormat::k_format_png:
	case ExportFormat::k_format_tiff:
		return k_encoder_type_oiio;
	case ExportFormat::k_format_count:
		break;
	}

	return k_encoder_type_none;
}

Encoder *Encoder::create_from_format(ExportFormat::Format f,
								   const EncodingParams &params)
{
	return create_from_id(get_type_from_format(f), params);
}

Encoder *Encoder::create_from_params(const EncodingParams &params)
{
	return create_from_format(params.format(), params);
}

QStringList Encoder::get_pixel_formats_for_codec(ExportCodec::Codec c) const
{
	return QStringList();
}

std::vector<SampleFormat>
Encoder::get_sample_formats_for_codec(ExportCodec::Codec c) const
{
	return std::vector<SampleFormat>();
}

QMatrix4x4
EncodingParams::generate_matrix(EncodingParams::VideoScalingMethod method,
							   int source_width, int source_height,
							   int dest_width, int dest_height)
{
	QMatrix4x4 preview_matrix;

	if (method == EncodingParams::k_stretch) {
		return preview_matrix;
	}

	float export_ar =
		static_cast<float>(dest_width) / static_cast<float>(dest_height);
	float source_ar =
		static_cast<float>(source_width) / static_cast<float>(source_height);

	if (qFuzzyCompare(export_ar, source_ar)) {
		return preview_matrix;
	}

	if ((export_ar > source_ar) == (method == EncodingParams::k_fit)) {
		preview_matrix.scale(source_ar / export_ar, 1.0F);
	} else {
		preview_matrix.scale(1.0F, export_ar / source_ar);
	}

	return preview_matrix;
}

bool EncodingParams::load_v1(QXmlStreamReader *reader)
{
	Rational custom_range_in, custom_range_out;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("filename")) {
			filename_ = reader->readElementText();
		} else if (reader->name() == QStringLiteral("format")) {
			format_ = static_cast<ExportFormat::Format>(
				reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("range")) {
			has_custom_range_ = reader->readElementText().toInt();
		} else if (reader->name() == QStringLiteral("customrangein")) {
			custom_range_in =
				Rational::from_string(reader->readElementText().toStdString());
		} else if (reader->name() == QStringLiteral("customrangeout")) {
			custom_range_out =
				Rational::from_string(reader->readElementText().toStdString());
		} else if (reader->name() == QStringLiteral("video")) {
			XMLAttributeLoop(reader, attr)
			{
				if (attr.name() == QStringLiteral("enabled")) {
					video_enabled_ = attr.value().toInt();
				}
			}

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("codec")) {
					video_codec_ = static_cast<ExportCodec::Codec>(
						reader->readElementText().toInt());
				} else if (reader->name() == QStringLiteral("width")) {
					video_params_.set_width(reader->readElementText().toInt());
				} else if (reader->name() == QStringLiteral("height")) {
					video_params_.set_height(reader->readElementText().toInt());
				} else if (reader->name() == QStringLiteral("format")) {
					video_params_.set_format(static_cast<PixelFormat::Format>(
						reader->readElementText().toInt()));
				} else if (reader->name() == QStringLiteral("pixelaspect")) {
					video_params_.set_pixel_aspect_ratio(Rational::from_string(
						reader->readElementText().toStdString()));
				} else if (reader->name() == QStringLiteral("timebase")) {
					video_params_.set_time_base(Rational::from_string(
						reader->readElementText().toStdString()));
				} else if (reader->name() == QStringLiteral("divider")) {
					video_params_.set_divider(
						reader->readElementText().toInt());
				} else if (reader->name() == QStringLiteral("bitrate")) {
					video_bit_rate_ = reader->readElementText().toLongLong();
				} else if (reader->name() == QStringLiteral("minbitrate")) {
					video_min_bit_rate_ =
						reader->readElementText().toLongLong();
				} else if (reader->name() == QStringLiteral("maxbitrate")) {
					video_max_bit_rate_ =
						reader->readElementText().toLongLong();
				} else if (reader->name() == QStringLiteral("bufsize")) {
					video_buffer_size_ = reader->readElementText().toLongLong();
				} else if (reader->name() == QStringLiteral("threads")) {
					video_threads_ = reader->readElementText().toInt();
				} else if (reader->name() == QStringLiteral("pixfmt")) {
					video_pix_fmt_ = reader->readElementText();
				} else if (reader->name() == QStringLiteral("imgseq")) {
					video_is_image_sequence_ =
						reader->readElementText().toInt();
				} else if (reader->name() == QStringLiteral("color")) {
					while (xml_read_next_start_element(reader)) {
						if (reader->name() == QStringLiteral("output")) {
							color_transform_ = reader->readElementText();
						} else {
							reader->skipCurrentElement();
						}
					}
				} else if (reader->name() == QStringLiteral("vscale")) {
					video_scaling_method_ = static_cast<VideoScalingMethod>(
						reader->readElementText().toInt());
				} else if (reader->name() == QStringLiteral("opts")) {
					while (xml_read_next_start_element(reader)) {
						if (reader->name() == QStringLiteral("entry")) {
							QString key, value;
							while (xml_read_next_start_element(reader)) {
								if (reader->name() == QStringLiteral("key")) {
									key = reader->readElementText();
								} else if (reader->name() ==
										   QStringLiteral("value")) {
									value = reader->readElementText();
								} else {
									reader->skipCurrentElement();
								}
							}
							set_video_option(key, value);
						} else {
							reader->skipCurrentElement();
						}
					}
				} else {
					reader->skipCurrentElement();
				}
			}

			// HACK: Resolve bug where I forgot to serialize pixel aspect ratio
			if (video_params_.pixel_aspect_ratio().isNull()) {
				video_params_.set_pixel_aspect_ratio(1);
			}
		} else if (reader->name() == QStringLiteral("audio")) {
			XMLAttributeLoop(reader, attr)
			{
				if (attr.name() == QStringLiteral("enabled")) {
					audio_enabled_ = attr.value().toInt();
				}
			}

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("codec")) {
					audio_codec_ = static_cast<ExportCodec::Codec>(
						reader->readElementText().toInt());
				} else if (reader->name() == QStringLiteral("samplerate")) {
					audio_params_.set_sample_rate(
						reader->readElementText().toInt());
				} else if (reader->name() == QStringLiteral("channellayout")) {
					audio_params_.set_channel_layout(
						reader->readElementText().toLongLong());
				} else if (reader->name() == QStringLiteral("format")) {
					audio_params_.set_format(SampleFormat::from_string(
						reader->readElementText().toStdString()));
				} else if (reader->name() == QStringLiteral("bitrate")) {
					audio_bit_rate_ = reader->readElementText().toLongLong();
				} else {
					reader->skipCurrentElement();
				}
			}

			// HACK: Resolve bug where I forgot to serialize the audio bit rate
			if (!audio_bit_rate_) {
				audio_bit_rate_ = 320000;
			}
		} else if (reader->name() == QStringLiteral("subtitles")) {
			XMLAttributeLoop(reader, attr)
			{
				if (attr.name() == QStringLiteral("enabled")) {
					subtitles_enabled_ = attr.value().toInt();
				}
			}

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("sidecar")) {
					subtitles_are_sidecar_ = reader->readElementText().toInt();
				} else if (reader->name() == QStringLiteral("sidecarformat")) {
					subtitle_sidecar_fmt_ = static_cast<ExportFormat::Format>(
						reader->readElementText().toInt());
				} else if (reader->name() == QStringLiteral("codec")) {
					subtitles_codec_ = static_cast<ExportCodec::Codec>(
						reader->readElementText().toInt());
				} else {
					reader->skipCurrentElement();
				}
			}
		} else {
			reader->skipCurrentElement();
		}
	}

	return true;
}

}
