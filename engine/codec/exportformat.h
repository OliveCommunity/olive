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

#ifndef OAK_EXPORTFORMAT_H
#define OAK_EXPORTFORMAT_H

#include <QList>
#include <QString>

#include "common/define.h"
#include "exportcodec.h"

namespace olive
{

class ExportFormat : public QObject {
	Q_OBJECT
public:
	// Only append to this list (never insert) because indexes are used in serialized files
	enum Format {
		k_format_d_nx_hd,
		k_format_matroska,
		k_format_mpe_g4_video,
		k_format_open_exr,
		k_format_quick_time,
		k_format_png,
		k_format_tiff,
		k_format_wav,
		k_format_aiff,
		k_format_m_p3,
		k_format_flac,
		k_format_ogg,
		k_format_web_m,
		k_format_srt,
		k_format_mpe_g4_audio,

		k_format_count
	};

	static QString get_name(Format f);
	static QString get_extension(Format f);
	static QList<ExportCodec::Codec> get_video_codecs(ExportFormat::Format f);
	static QList<ExportCodec::Codec> get_audio_codecs(ExportFormat::Format f);
	static QList<ExportCodec::Codec> get_subtitle_codecs(ExportFormat::Format f);

	static QStringList get_pixel_formats_for_codec(Format f, ExportCodec::Codec c);
	static std::vector<SampleFormat>
	get_sample_formats_for_codec(Format f, ExportCodec::Codec c);
};

}

#endif // OAK_EXPORTFORMAT_H
