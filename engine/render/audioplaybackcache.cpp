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

#include "audioplaybackcache.h"

#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QUuid>

#include "common/filefunctions.h"
#include "node/output/viewer/viewer.h"

namespace olive
{

const qint64 AudioPlaybackCache::k_default_segment_size_per_channel =
	10 * 1024 * 1024;

AudioPlaybackCache::AudioPlaybackCache(QObject *parent)
	: PlaybackCache(parent)
{
}

AudioPlaybackCache::~AudioPlaybackCache()
{
}

void AudioPlaybackCache::set_parameters(const AudioParams &params)
{
	if (params_ == params) {
		return;
	}

	params_ = params;
}

void AudioPlaybackCache::write_pcm(const TimeRange &range,
								  const TimeRangeList &valid_ranges,
								  const SampleBuffer &samples)
{
	for (const TimeRange &r : valid_ranges) {
		if (write_part_of_sample_buffer(samples, r.in(), r.in() - range.in(),
									r.length())) {
			validate(r);
		}
	}
}

void AudioPlaybackCache::write_silence(const TimeRange &range)
{
	// WritePCM will automatically fill non-existent bytes with silence, so we just have to send
	// it an empty sample buffer
	write_pcm(range, { range }, SampleBuffer());
}

bool AudioPlaybackCache::write_part_of_sample_buffer(const SampleBuffer &samples,
												 const Rational &write_start,
												 const Rational &buffer_start,
												 const Rational &length)
{
	int64_t length_in_bytes = params_.time_to_bytes_per_channel(length);

	int64_t start_cache_offset = params_.time_to_bytes_per_channel(write_start);
	int64_t end_cache_offset = start_cache_offset + length_in_bytes;

	int64_t start_buffer_offset =
		params_.time_to_bytes_per_channel(buffer_start);
	int64_t end_buffer_offset =
		std::min(start_buffer_offset + length_in_bytes,
				 params_.samples_to_bytes_per_channel(samples.sample_count()));

	int64_t current_cache_offset = start_cache_offset;
	int64_t current_buffer_offset = start_buffer_offset;

	bool success = true;

	while (current_cache_offset != end_cache_offset) {
		int64_t segment = current_cache_offset / k_default_segment_size_per_channel;
		int64_t segment_start = segment * k_default_segment_size_per_channel;
		int64_t segment_end = segment_start + k_default_segment_size_per_channel;

		int64_t offset_in_segment = current_cache_offset - segment_start;
		// Never write past the end of the requested range
		int64_t write_len = std::min(segment_end - current_cache_offset,
									 end_cache_offset - current_cache_offset);
		int64_t max_buffer_len = end_buffer_offset - current_buffer_offset;
		int64_t zero_len = 0;

		if (write_len > max_buffer_len) {
			zero_len = write_len - max_buffer_len;
			write_len = max_buffer_len;
		}

		for (int channel = 0; channel < params_.channel_count(); channel++) {
			QString filename = get_segment_filename(segment, channel);

			if (!FileFunctions::directory_is_valid(QFileInfo(filename).dir())) {
				success = false;
				break;
			}

			QFile f(filename);
			if (f.open(QFile::ReadWrite)) {
				f.seek(offset_in_segment);
				if (write_len > 0) {
					f.write(reinterpret_cast<const char *>(samples.data(channel)) +
								current_buffer_offset,
							write_len);
				}

				if (zero_len > 0) {
					// NOTE: the length must be passed explicitly; write(const
					// char*) would treat the zeros as an empty C string
					QByteArray b(zero_len, 0);
					f.write(b.constData(), b.size());
				}

				f.close();
			} else {
				success = false;
			}
		}

		current_cache_offset += write_len + zero_len;
		current_buffer_offset += write_len;
	}

	return success;
}

QString AudioPlaybackCache::get_segment_filename(qint64 segment_index,
											   int channel)
{
	return get_this_cache_directory().filePath(QStringLiteral("%1.%2").arg(
		QString::number(segment_index), QString::number(channel)));
}

}
