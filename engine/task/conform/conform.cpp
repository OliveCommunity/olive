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

#include "conform.h"

namespace olive
{

ConformTask::ConformTask(const QString &decoder_id,
						 const Decoder::CodecStream &stream,
						 const AudioParams &params,
						 const QVector<QString> &output_filenames)
	: decoder_id_(decoder_id)
	, stream_(stream)
	, params_(params)
	, output_filenames_(output_filenames)
{
	set_title(tr("Conforming Audio %1:%2")
				 .arg(stream.filename(), QString::number(stream.stream())));
}

bool ConformTask::run()
{
	DecoderPtr decoder = Decoder::create_from_id(decoder_id_);

	if (!decoder->open(stream_)) {
		set_error(tr("Failed to open decoder for audio conform"));
		return false;
	}

	connect(decoder.get(), &Decoder::index_progress, this,
			&ConformTask::progress_changed);

	qDebug() << "Starting conform of" << stream_.filename() << stream_.stream();

	bool ret =
		decoder->conform_audio(output_filenames_, params_, get_cancel_atom());

	decoder->close();

	return ret;
}

}
