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

#include "decoder.h"

#include <QCoreApplication>
#include <QDebug>
#include <QHash>

#include "codec/ffmpeg/ffmpegdecoder.h"
#include "codec/planarfiledevice.h"
#include "codec/oiio/oiiodecoder.h"
#include "conformmanager.h"

namespace olive
{

const Rational Decoder::k_any_timecode = RATIONAL_MIN;

Decoder::Decoder()
	: cached_texture_(nullptr)
{
	update_last_accessed();
}

void Decoder::increment_access_time(qint64 t)
{
	last_accessed_ += t;
}

bool Decoder::open(const CodecStream &stream)
{
	QMutexLocker locker(&mutex_);

	update_last_accessed();

	if (stream_.is_valid()) {
		// Decoder is already open. Return TRUE if the stream is the stream we have, or FALSE if not.
		if (stream_ == stream) {
			return true;
		} else {
			qWarning()
				<< "Tried to open a decoder that was already open with another stream";
			return false;
		}
	} else {
		// Stream was not open, try opening it now
		if (!stream.is_valid()) {
			// Cannot open null stream
			qCritical() << "Decoder attempted to open null stream";
			return false;
		}

		if (!stream.exists()) {
			// Cannot open file that doesn't exist
			qCritical() << "Decoder attempted to open file that doesn't exist";
			return false;
		}

		// Set stream
		stream_ = stream;

		// Try open internal
		if (open_internal()) {
			return true;
		} else {
			// Unset stream
			qCritical() << "Failed to open" << stream_.filename() << "stream"
						<< stream_.stream();
			close_internal();
			stream_.reset();
			return false;
		}
	}
}

TexturePtr Decoder::retrieve_video(const RetrieveVideoParams &p)
{
	QMutexLocker locker(&mutex_);

	update_last_accessed();

	if (!stream_.is_valid()) {
		qCritical() << "Can't retrieve video on a closed decoder";
		return nullptr;
	}

	if (!supports_video()) {
		qCritical() << "Decoder doesn't support video";
		return nullptr;
	}

	if (p.cancelled && p.cancelled->is_cancelled()) {
		return nullptr;
	}

	if (cached_texture_ && cached_time_ == p.time &&
		cached_divider_ == p.divider) {
		return cached_texture_;
	}

	cached_texture_ = retrieve_video_internal(p);
	cached_time_ = p.time;
	cached_divider_ = p.divider;

	return cached_texture_;
}

FramePtr Decoder::retrieve_video_frame(const RetrieveVideoParams &p)
{
	QMutexLocker locker(&mutex_);

	update_last_accessed();

	if (!stream_.is_valid()) {
		qCritical() << "Can't retrieve video frame on a closed decoder";
		return nullptr;
	}

	if (!supports_video()) {
		qCritical() << "Decoder doesn't support video";
		return nullptr;
	}

	if (p.cancelled && p.cancelled->is_cancelled()) {
		return nullptr;
	}

	return retrieve_video_frame_internal(p);
}

Decoder::RetrieveAudioStatus
Decoder::retrieve_audio(SampleBuffer &dest, const TimeRange &range,
					   const AudioParams &params, const QString &cache_path,
					   LoopMode loop_mode, RenderMode::Mode mode)
{
	QMutexLocker locker(&mutex_);

	update_last_accessed();

	if (!stream_.is_valid()) {
		qCritical() << "Can't retrieve audio on a closed decoder";
		return k_invalid;
	}

	if (!supports_audio()) {
		qCritical() << "Decoder doesn't support audio";
		return k_invalid;
	}

	if (params.sample_rate() <= 0 || params.channel_count() <= 0) {
		qWarning() << "Invalid audio parameters, skipping audio retrieve";
		return k_invalid;
	}

	// Get conform state from ConformManager
	ConformManager::Conform conform =
		ConformManager::instance()->get_conform_state(
			id(), cache_path, stream_, params, (mode == RenderMode::k_online));
	if (conform.state == ConformManager::k_conform_generating) {
		// If we need the task, it's available in `conform.task`
		return k_waiting_for_conform;
	}

	// See if we got the conform
	if (retrieve_audio_from_conform(dest, conform.filenames, range, loop_mode,
								 params)) {
		return k_ok;
	} else {
		return k_unknown_error;
	}
}

qint64 Decoder::get_last_accessed_time()
{
	return last_accessed_;
}

void Decoder::close()
{
	QMutexLocker locker(&mutex_);

	update_last_accessed();

	cached_texture_ = nullptr;

	if (stream_.is_valid()) {
		close_internal();
		stream_.reset();
	} else {
		qWarning() << "Tried to close a decoder that wasn't open";
	}
}

bool Decoder::conform_audio(const QVector<QString> &output_filenames,
						   const AudioParams &params, CancelAtom *cancelled)
{
	return conform_audio_internal(output_filenames, params, cancelled);
}

/*
 * DECODER STATIC PUBLIC MEMBERS
 */

QVector<DecoderPtr> Decoder::receive_list_of_all_decoders()
{
	QVector<DecoderPtr> decoders;

	// The order in which these decoders are added is their priority when probing. Hence FFmpeg should usually be last,
	// since it supports so many formats and we presumably want to override those formats with a more specific decoder.
	decoders.append(std::make_shared<OIIODecoder>());
	decoders.append(std::make_shared<FFmpegDecoder>());

	return decoders;
}

DecoderPtr Decoder::create_from_id(const QString &id)
{
	if (id.isEmpty()) {
		return nullptr;
	}

	// Create list to iterate through
	QVector<DecoderPtr> decoder_list = receive_list_of_all_decoders();

	foreach (DecoderPtr d, decoder_list) {
		if (d->id() == id) {
			return d;
		}
	}

	return nullptr;
}

void Decoder::signal_processing_progress(int64_t ts, int64_t duration)
{
	if (duration != FB_NOPTS_VALUE && duration != 0) {
		emit index_progress(static_cast<double>(ts) /
						   static_cast<double>(duration));
	}
}

QString Decoder::transform_image_sequence_file_name(const QString &filename,
												const int64_t &number)
{
	int digit_count = get_image_sequence_digit_count(filename);

	QFileInfo file_info(filename);

	QString original_basename = file_info.completeBaseName();

	QString new_basename =
		original_basename.left(original_basename.size() - digit_count)
			.append(
				QStringLiteral("%1").arg(number, digit_count, 10, QChar('0')));

	return file_info.dir().filePath(
		file_info.fileName().replace(original_basename, new_basename));
}

int Decoder::get_image_sequence_digit_count(const QString &filename)
{
	QString basename = QFileInfo(filename).completeBaseName();

	// See if basename contains a number at the end
	int digit_count = 0;

	for (int i = basename.size() - 1; i >= 0; i--) {
		if (basename.at(i).isDigit()) {
			digit_count++;
		} else {
			break;
		}
	}

	return digit_count;
}

int64_t Decoder::get_image_sequence_index(const QString &filename)
{
	int digit_count = get_image_sequence_digit_count(filename);

	QFileInfo file_info(filename);

	QString original_basename = file_info.completeBaseName();

	QString number_only =
		original_basename.mid(original_basename.size() - digit_count);

	return number_only.toLongLong();
}

TexturePtr Decoder::retrieve_video_internal(const RetrieveVideoParams &p)
{
	Q_UNUSED(p)
	return nullptr;
}

FramePtr Decoder::retrieve_video_frame_internal(const RetrieveVideoParams &p)
{
	Q_UNUSED(p)
	return nullptr;
}

bool Decoder::conform_audio_internal(const QVector<QString> &filenames,
								   const AudioParams &params,
								   CancelAtom *cancelled)
{
	Q_UNUSED(filenames)
	Q_UNUSED(cancelled)
	Q_UNUSED(params)
	return false;
}

bool Decoder::retrieve_audio_from_conform(
	SampleBuffer &sample_buffer, const QVector<QString> &conform_filenames,
	TimeRange range, LoopMode loop_mode, const AudioParams &input_params)
{
	PlanarFileDevice input;
	if (input.open(conform_filenames, QFile::ReadOnly)) {
		// Offset range by audio start offset
		range -= get_audio_start_offset();

		qint64 read_index = input_params.time_to_bytes(range.in()) /
							input_params.channel_count();
		qint64 write_index = 0;

		const qint64 buffer_length_in_bytes =
			sample_buffer.sample_count() *
			input_params.bytes_per_sample_per_channel();

		while (write_index < buffer_length_in_bytes) {
			if (loop_mode == LoopMode::k_loop_mode_loop) {
				while (read_index >= input.size()) {
					read_index -= input.size();
				}

				while (read_index < 0) {
					read_index += input.size();
				}
			}

			qint64 write_count = 0;

			if (read_index < 0) {
				// Reading before 0, write silence here until audio data would actually start
				write_count = qMin(-read_index, buffer_length_in_bytes);
				sample_buffer.silence_bytes(write_index,
											write_index + write_count);
			} else if (read_index >= input.size()) {
				// Reading after data length, write silence until the end of the buffer
				write_count = buffer_length_in_bytes - write_index;
				sample_buffer.silence_bytes(write_index,
											write_index + write_count);
			} else {
				write_count = qMin(input.size() - read_index,
								   buffer_length_in_bytes - write_index);
				input.seek(read_index);
				input.read(reinterpret_cast<char **>(
							   sample_buffer.to_raw_ptrs().data()),
						   write_count, write_index);
			}

			read_index += write_count;
			write_index += write_count;
		}

		input.close();

		return true;
	}

	return false;
}

void Decoder::update_last_accessed()
{
	last_accessed_ = QDateTime::currentMSecsSinceEpoch();
}

uint qHash(Decoder::CodecStream stream, uint seed)
{
	return qHash(stream.filename(), seed) ^ ::qHash(stream.stream(), seed) ^
		   qHash(stream.block(), seed);
}

}
