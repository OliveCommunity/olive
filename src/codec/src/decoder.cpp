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

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "conformmanager.h"
#include "ffmpeg/ffmpegdecoder.h"
#include "oiio/oiiodecoder.h"
#include "planarfiledevice.h"

namespace olive
{

namespace
{

/**
 * @brief NULL/empty-handle-safe check of an oakrender cancel atom
 *        (borrowed pointer, used at several retrieval entry points)
 */
bool cancel_atom_is_cancelled(const OakCancelAtom *cancelled)
{
	if (!cancelled || !cancelled->ctx) {
		return false;
	}
	int c = 0;
	oakrender_cancelatom_is_cancelled(*cancelled, &c);
	return c != 0;
}

} // namespace

const Rational Decoder::k_any_timecode = RATIONAL_MIN;

Decoder::Decoder()
{
	update_last_accessed();
}

Decoder::~Decoder()
{
	oakrender_display_texture_free(&cached_texture_);
}

void Decoder::increment_access_time(int64_t t)
{
	last_accessed_ += t;
}

bool Decoder::open(const CodecStream &stream)
{
	std::lock_guard<std::mutex> locker(mutex_);

	update_last_accessed();

	if (stream_.is_valid()) {
		// Decoder is already open. Return TRUE if the stream is the stream we have, or FALSE if not.
		if (stream_ == stream) {
			return true;
		} else {
			fprintf(stderr, "Tried to open a decoder that was already open with another stream\n");
			return false;
		}
	} else {
		// Stream was not open, try opening it now
		if (!stream.is_valid()) {
			// Cannot open null stream
			fprintf(stderr, "Decoder attempted to open null stream\n");
			return false;
		}

		if (!stream.exists()) {
			// Cannot open file that doesn't exist
			fprintf(stderr, "Decoder attempted to open file that doesn't exist\n");
			return false;
		}

		// Set stream
		stream_ = stream;

		// Try open internal
		if (open_internal()) {
			return true;
		} else {
			// Unset stream
			fprintf(stderr, "Failed to open %s stream %d\n",
					stream_.filename().c_str(), stream_.stream());
			close_internal();
			stream_.reset();
			return false;
		}
	}
}

OakRenderTexture Decoder::retrieve_video(const RetrieveVideoParams &p)
{
	std::lock_guard<std::mutex> locker(mutex_);

	update_last_accessed();

	if (!stream_.is_valid()) {
		fprintf(stderr, "Can't retrieve video on a closed decoder\n");
		return OakRenderTexture{};
	}

	if (!supports_video()) {
		fprintf(stderr, "Decoder doesn't support video\n");
		return OakRenderTexture{};
	}

	if (cancel_atom_is_cancelled(p.cancelled)) {
		return OakRenderTexture{};
	}

	if (cached_texture_.ctx && cached_time_ == p.time &&
		cached_divider_ == p.divider) {
		// Hand the caller its own reference; the cache keeps its own
		return oakrender_display_texture_retain(cached_texture_);
	}

	OakRenderTexture texture = retrieve_video_internal(p);
	oakrender_display_texture_free(&cached_texture_);
	cached_texture_ = texture.ctx ?
						  oakrender_display_texture_retain(texture) :
						  OakRenderTexture{};
	cached_time_ = p.time;
	cached_divider_ = p.divider;

	return texture;
}

FramePtr Decoder::retrieve_video_frame(const RetrieveVideoParams &p)
{
	std::lock_guard<std::mutex> locker(mutex_);

	update_last_accessed();

	if (!stream_.is_valid()) {
		fprintf(stderr, "Can't retrieve video frame on a closed decoder\n");
		return nullptr;
	}

	if (!supports_video()) {
		fprintf(stderr, "Decoder doesn't support video\n");
		return nullptr;
	}

	if (cancel_atom_is_cancelled(p.cancelled)) {
		return nullptr;
	}

	return retrieve_video_frame_internal(p);
}

Decoder::RetrieveAudioStatus
Decoder::retrieve_audio(SampleBuffer &dest, const TimeRange &range,
						const AudioParams &params,
						const std::string &cache_path, OakLoopMode loop_mode,
						RenderMode::Mode mode)
{
	std::lock_guard<std::mutex> locker(mutex_);

	update_last_accessed();

	if (!stream_.is_valid()) {
		fprintf(stderr, "Can't retrieve audio on a closed decoder\n");
		return k_invalid;
	}

	if (!supports_audio()) {
		fprintf(stderr, "Decoder doesn't support audio\n");
		return k_invalid;
	}

	if (params.sample_rate() <= 0 || params.channel_count() <= 0) {
		fprintf(stderr, "Invalid audio parameters, skipping audio retrieve\n");
		return k_invalid;
	}

	// Get conform state from ConformManager
	ConformManager::Conform conform =
		ConformManager::instance()->get_conform_state(
			cache_path, stream_, params, (mode == RenderMode::k_online));
	if (conform.state == ConformManager::k_conform_generating) {
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

int64_t Decoder::get_last_accessed_time()
{
	return last_accessed_;
}

void Decoder::close()
{
	std::lock_guard<std::mutex> locker(mutex_);

	update_last_accessed();

	oakrender_display_texture_free(&cached_texture_);

	if (stream_.is_valid()) {
		close_internal();
		stream_.reset();
	} else {
		fprintf(stderr, "Tried to close a decoder that wasn't open\n");
	}
}

bool Decoder::conform_audio(const std::vector<std::string> &output_filenames,
							const AudioParams &params, OakCancelAtom *cancelled)
{
	return conform_audio_internal(output_filenames, params, cancelled);
}

/*
 * DECODER STATIC PUBLIC MEMBERS
 */

std::vector<DecoderPtr> Decoder::receive_list_of_all_decoders()
{
	std::vector<DecoderPtr> decoders;

	// The order in which these decoders are added is their priority when probing. Hence FFmpeg should usually be last,
	// since it supports so many formats and we presumably want to override those formats with a more specific decoder.
	decoders.push_back(std::make_shared<OIIODecoder>());
	decoders.push_back(std::make_shared<FFmpegDecoder>());

	return decoders;
}

DecoderPtr Decoder::create_from_id(const std::string &id)
{
	if (id.empty()) {
		return nullptr;
	}

	// Create list to iterate through
	std::vector<DecoderPtr> decoder_list = receive_list_of_all_decoders();

	for (DecoderPtr d : decoder_list) {
		if (d->id() == id) {
			return d;
		}
	}

	return nullptr;
}

void Decoder::signal_processing_progress(int64_t ts, int64_t duration)
{
	if (duration != FB_NOPTS_VALUE && duration != 0) {
		if (index_progress_callback_) {
			index_progress_callback_(static_cast<double>(ts) /
									 static_cast<double>(duration));
		}
	}
}

std::string
Decoder::transform_image_sequence_file_name(const std::string &filename,
											const int64_t &number)
{
	int digit_count = get_image_sequence_digit_count(filename);

	std::filesystem::path file_path(filename);

	// QFileInfo::completeBaseName(): filename up to the first '.'
	std::string original_basename = file_path.filename().string();
	std::string::size_type dot = original_basename.find('.');
	if (dot != std::string::npos) {
		original_basename.erase(dot);
	}

	std::string new_basename =
		original_basename.substr(0, original_basename.size() - digit_count);

	char number_buf[32];
	snprintf(number_buf, sizeof(number_buf), "%0*lld", digit_count,
			 static_cast<long long>(number));
	new_basename += number_buf;

	std::string new_filename = file_path.filename().string();
	std::string::size_type pos = 0;
	while ((pos = new_filename.find(original_basename, pos)) !=
		   std::string::npos) {
		new_filename.replace(pos, original_basename.size(), new_basename);
		pos += new_basename.size();
	}

	return (file_path.parent_path() / new_filename).string();
}

int Decoder::get_image_sequence_digit_count(const std::string &filename)
{
	// QFileInfo::completeBaseName(): filename up to the first '.'
	std::string basename =
		std::filesystem::path(filename).filename().string();
	std::string::size_type dot = basename.find('.');
	if (dot != std::string::npos) {
		basename.erase(dot);
	}

	// See if basename contains a number at the end
	int digit_count = 0;

	for (int i = int(basename.size()) - 1; i >= 0; i--) {
		if (basename[size_t(i)] >= '0' && basename[size_t(i)] <= '9') {
			digit_count++;
		} else {
			break;
		}
	}

	return digit_count;
}

int64_t Decoder::get_image_sequence_index(const std::string &filename)
{
	int digit_count = get_image_sequence_digit_count(filename);

	std::string original_basename =
		std::filesystem::path(filename).filename().string();
	std::string::size_type dot = original_basename.find('.');
	if (dot != std::string::npos) {
		original_basename.erase(dot);
	}

	std::string number_only =
		original_basename.substr(original_basename.size() - digit_count);

	return strtoll(number_only.c_str(), nullptr, 10);
}

OakRenderTexture Decoder::retrieve_video_internal(const RetrieveVideoParams &p)
{
	(void) p;
	return OakRenderTexture{};
}

FramePtr Decoder::retrieve_video_frame_internal(const RetrieveVideoParams &p)
{
	(void) p;
	return nullptr;
}

bool Decoder::conform_audio_internal(
	const std::vector<std::string> &filenames, const AudioParams &params,
	OakCancelAtom *cancelled)
{
	(void) filenames;
	(void) cancelled;
	(void) params;
	return false;
}

bool Decoder::retrieve_audio_from_conform(
	SampleBuffer &sample_buffer,
	const std::vector<std::string> &conform_filenames, TimeRange range,
	OakLoopMode loop_mode, const AudioParams &input_params)
{
	PlanarFileDevice input;
	if (input.open(conform_filenames, PlanarFileDevice::k_read_only)) {
		// Offset range by audio start offset
		range -= get_audio_start_offset();

		int64_t read_index = input_params.time_to_bytes(range.in()) /
							 input_params.channel_count();
		int64_t write_index = 0;

		const int64_t buffer_length_in_bytes =
			sample_buffer.sample_count() *
			input_params.bytes_per_sample_per_channel();

		while (write_index < buffer_length_in_bytes) {
			if (loop_mode == OAKCOMMON_LOOP_MODE_LOOP) {
				while (read_index >= input.size()) {
					read_index -= input.size();
				}

				while (read_index < 0) {
					read_index += input.size();
				}
			}

			int64_t write_count = 0;

			if (read_index < 0) {
				// Reading before 0, write silence here until audio data would actually start
				write_count = std::min(-read_index, buffer_length_in_bytes);
				sample_buffer.silence_bytes(write_index,
											write_index + write_count);
			} else if (read_index >= input.size()) {
				// Reading after data length, write silence until the end of the buffer
				write_count = buffer_length_in_bytes - write_index;
				sample_buffer.silence_bytes(write_index,
											write_index + write_count);
			} else {
				write_count = std::min(input.size() - read_index,
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
	last_accessed_ =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count();
}

}
