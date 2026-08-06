/***

  Oak Video Editor - Non-Linear Video Editor
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

#include "codec/conform.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "conformmanager.h"
#include "decoder.h"

namespace
{

int string_out(const std::string &s, char *buf, int buf_size)
{
	int need = static_cast<int>(s.size()) + 1;
	if (buf && buf_size > 0) {
		int n = std::min(static_cast<int>(s.size()), buf_size - 1);
		memcpy(buf, s.data(), n);
		buf[n] = '\0';
	}
	return need;
}

olive::core::AudioParams to_native_params(int sample_rate,
									  uint64_t channel_layout,
									  int sample_format)
{
	return olive::core::AudioParams(
		sample_rate, channel_layout,
		static_cast<olive::core::SampleFormat::Format>(sample_format));
}

olive::Decoder::CodecStream to_native_stream(const char *source_filename,
										 int stream_index)
{
	return olive::Decoder::CodecStream(
		source_filename ? source_filename : "", stream_index, nullptr);
}

bool conform_args_valid(const char *cache_path, const char *source_filename)
{
	return cache_path && *cache_path && source_filename && *source_filename;
}

} // namespace

int oakcodec_conform_create_instance(void)
{
	olive::ConformManager::create_instance();
	return OAKCODEC_OK;
}

int oakcodec_conform_destroy_instance(void)
{
	olive::ConformManager::destroy_instance();
	return OAKCODEC_OK;
}

int oakcodec_conform_get_state(const char *cache_path,
							   const char *source_filename, int stream_index,
							   int sample_rate, uint64_t channel_layout,
							   int sample_format, int wait)
{
	if (!conform_args_valid(cache_path, source_filename))
		return OAKCODEC_E_INVALID;
	if (!olive::ConformManager::instance())
		return OAKCODEC_E_STATE;

	olive::ConformManager::Conform c =
		olive::ConformManager::instance()->get_conform_state(
			cache_path, to_native_stream(source_filename, stream_index),
			to_native_params(sample_rate, channel_layout, sample_format),
			wait != 0);

	switch (c.state) {
	case olive::ConformManager::k_conform_exists:
		return OAKCODEC_CONFORM_EXISTS;
	case olive::ConformManager::k_conform_generating:
		return OAKCODEC_CONFORM_GENERATING;
	case olive::ConformManager::k_conform_unavailable:
	default:
		return OAKCODEC_CONFORM_UNAVAILABLE;
	}
}

int oakcodec_conform_filename_count(const char *cache_path,
								const char *source_filename,
								int stream_index, int sample_rate,
								uint64_t channel_layout, int sample_format)
{
	if (!conform_args_valid(cache_path, source_filename))
		return 0;

	// Pure path computation: never submits work.
	return static_cast<int>(olive::ConformManager::get_conformed_filename(
								cache_path,
								to_native_stream(source_filename, stream_index),
								to_native_params(sample_rate, channel_layout,
												 sample_format))
								.size());
}

int oakcodec_conform_filename_at(const char *cache_path,
								 const char *source_filename,
								 int stream_index, int sample_rate,
								 uint64_t channel_layout, int sample_format,
								 int index, char *buf, int buf_size)
{
	if (!conform_args_valid(cache_path, source_filename))
		return OAKCODEC_E_INVALID;

	std::vector<std::string> filenames =
		olive::ConformManager::get_conformed_filename(
			cache_path, to_native_stream(source_filename, stream_index),
			to_native_params(sample_rate, channel_layout, sample_format));

	if (index < 0 || index >= static_cast<int>(filenames.size()))
		return OAKCODEC_E_NOT_FOUND;
	return string_out(filenames[index], buf, buf_size);
}
