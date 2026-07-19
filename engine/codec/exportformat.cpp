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

#include "exportformat.h"

#include "encoder.h"

namespace olive
{

QString ExportFormat::get_name(olive::ExportFormat::Format f)
{
	switch (f) {
	case k_format_d_nx_hd:
		return tr("DNxHD");
	case k_format_matroska:
		return tr("Matroska Video");
	case k_format_mpe_g4_video:
		return tr("MPEG-4 Video");
	case k_format_mpe_g4_audio:
		return tr("MPEG-4 Audio");
	case k_format_open_exr:
		return tr("OpenEXR");
	case k_format_png:
		return tr("PNG");
	case k_format_tiff:
		return tr("TIFF");
	case k_format_quick_time:
		return tr("QuickTime");
	case k_format_wav:
		return tr("Wave Audio");
	case k_format_aiff:
		return tr("AIFF");
	case k_format_m_p3:
		return tr("MP3");
	case k_format_flac:
		return tr("FLAC");
	case k_format_ogg:
		return tr("Ogg");
	case k_format_web_m:
		return tr("WebM");
	case k_format_srt:
		return tr("SubRip SRT");

	case k_format_count:
		break;
	}

	return tr("Unknown");
}

QString ExportFormat::get_extension(ExportFormat::Format f)
{
	switch (f) {
	case k_format_d_nx_hd:
		return QStringLiteral("mxf");
	case k_format_matroska:
		return QStringLiteral("mkv");
	case k_format_mpe_g4_video:
		return QStringLiteral("mp4");
	case k_format_mpe_g4_audio:
		return QStringLiteral("m4a");
	case k_format_open_exr:
		return QStringLiteral("exr");
	case k_format_png:
		return QStringLiteral("png");
	case k_format_tiff:
		return QStringLiteral("tiff");
	case k_format_quick_time:
		return QStringLiteral("mov");
	case k_format_wav:
		return QStringLiteral("wav");
	case k_format_aiff:
		return QStringLiteral("aiff");
	case k_format_m_p3:
		return QStringLiteral("mp3");
	case k_format_flac:
		return QStringLiteral("flac");
	case k_format_ogg:
		return QStringLiteral("ogg");
	case k_format_web_m:
		return QStringLiteral("webm");
	case k_format_srt:
		return QStringLiteral("srt");
	case k_format_count:
		break;
	}

	return QString();
}

QList<ExportCodec::Codec> ExportFormat::get_video_codecs(ExportFormat::Format f)
{
	switch (f) {
	case k_format_d_nx_hd:
		return { ExportCodec::k_codec_d_nx_hd };
	case k_format_matroska:
		return { ExportCodec::k_codec_h264, ExportCodec::k_codec_h264rgb,
				 ExportCodec::k_codec_h265, ExportCodec::k_codec_v_p9 };
	case k_format_mpe_g4_video:
		return { ExportCodec::k_codec_h264, ExportCodec::k_codec_h264rgb,
				 ExportCodec::k_codec_h265 };
	case k_format_open_exr:
		return { ExportCodec::k_codec_open_exr };
	case k_format_png:
		return { ExportCodec::k_codec_png };
	case k_format_tiff:
		return { ExportCodec::k_codec_tiff };
	case k_format_quick_time:
		return { ExportCodec::k_codec_h264, ExportCodec::k_codec_h264rgb,
				 ExportCodec::k_codec_h265, ExportCodec::k_codec_pro_res,
				 ExportCodec::k_codec_cineform };
	case k_format_web_m:
		return { ExportCodec::k_codec_a_v1, ExportCodec::k_codec_v_p9 };
	case k_format_ogg:
	case k_format_wav:
	case k_format_mpe_g4_audio:
	case k_format_aiff:
	case k_format_m_p3:
	case k_format_flac:
	case k_format_srt:
	case k_format_count:
		break;
	}

	return {};
}

QList<ExportCodec::Codec> ExportFormat::get_audio_codecs(ExportFormat::Format f)
{
	switch (f) {
	// Video/audio formats
	case k_format_d_nx_hd:
		return { ExportCodec::k_codec_pcm };
	case k_format_matroska:
		return { ExportCodec::k_codec_aac,	ExportCodec::k_codec_m_p2,
				 ExportCodec::k_codec_m_p3,	ExportCodec::k_codec_pcm,
				 ExportCodec::k_codec_vorbis, ExportCodec::k_codec_opus,
				 ExportCodec::k_codec_flac };
	case k_format_mpe_g4_video:
	case k_format_mpe_g4_audio:
		return { ExportCodec::k_codec_aac, ExportCodec::k_codec_m_p2,
				 ExportCodec::k_codec_m_p3 };
	case k_format_quick_time:
		return { ExportCodec::k_codec_aac, ExportCodec::k_codec_m_p2,
				 ExportCodec::k_codec_m_p3, ExportCodec::k_codec_pcm };
	case k_format_web_m:
		return { ExportCodec::k_codec_opus, ExportCodec::k_codec_aac,
				 ExportCodec::k_codec_m_p2,  ExportCodec::k_codec_m_p3,
				 ExportCodec::k_codec_pcm,  ExportCodec::k_codec_vorbis };

	// Audio only formats
	case k_format_wav:
		return { ExportCodec::k_codec_pcm };
	case k_format_aiff:
		return { ExportCodec::k_codec_pcm };
	case k_format_m_p3:
		return { ExportCodec::k_codec_m_p3 };
	case k_format_flac:
		return { ExportCodec::k_codec_flac };
	case k_format_ogg:
		return { ExportCodec::k_codec_opus, ExportCodec::k_codec_vorbis,
				 ExportCodec::k_codec_pcm };

	// Video only formats
	case k_format_open_exr:
	case k_format_png:
	case k_format_tiff:
	case k_format_srt:
	case k_format_count:
		break;
	}

	return {};
}

QList<ExportCodec::Codec> ExportFormat::get_subtitle_codecs(Format f)
{
	switch (f) {
	case k_format_d_nx_hd:
	case k_format_mpe_g4_video:
	case k_format_mpe_g4_audio:
	case k_format_open_exr:
	case k_format_quick_time:
	case k_format_png:
	case k_format_tiff:
	case k_format_wav:
	case k_format_aiff:
	case k_format_m_p3:
	case k_format_flac:
	case k_format_ogg:
	case k_format_web_m:
	case k_format_count:
		break;
	case k_format_matroska:
	case k_format_srt:
		return { ExportCodec::k_codec_srt };
	}

	return {};
}

QStringList ExportFormat::get_pixel_formats_for_codec(ExportFormat::Format f,
												  ExportCodec::Codec c)
{
	Encoder *e = Encoder::create_from_format(f, EncodingParams());
	QStringList list;

	if (e) {
		list = e->get_pixel_formats_for_codec(c);
		delete e;
	}

	return list;
}

std::vector<SampleFormat>
ExportFormat::get_sample_formats_for_codec(Format format, ExportCodec::Codec c)
{
	std::vector<SampleFormat> f;
	Encoder *e = Encoder::create_from_format(format, EncodingParams());

	if (e) {
		f = e->get_sample_formats_for_codec(c);
		delete e;
	}

	return f;
}

}
