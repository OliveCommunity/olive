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

#include <filesystem>

namespace olive
{

namespace
{

int channel_count_from_layout(uint64_t layout)
{
	int count = 0;
	while (layout) {
		count += int(layout & 1);
		layout >>= 1;
	}
	return count;
}

} // namespace

ConformTask::ConformTask(const OakCodecTaskRequest &request)
	: input_filename_(request.input_filename ? request.input_filename : "")
	, output_filename_(request.output_filename ? request.output_filename
											   : "")
	, stream_index_(request.stream_index)
	, sample_rate_(request.sample_rate)
	, channel_layout_(request.channel_layout)
	, sample_format_(request.sample_format)
{
	set_title("Conforming Audio " + input_filename_ + ":" +
			  std::to_string(stream_index_));
}

bool ConformTask::derive_filenames(const std::string &first_channel_final,
								   int channel_count,
								   std::vector<std::string> *final_names,
								   std::vector<std::string> *working_names)
{
	static const std::string k_suffix = ".0.pcm";

	if (channel_count < 1 || first_channel_final.size() <= k_suffix.size() ||
		first_channel_final.compare(first_channel_final.size() -
										k_suffix.size(),
									k_suffix.size(), k_suffix) != 0) {
		return false;
	}

	const std::string base =
		first_channel_final.substr(0, first_channel_final.size() -
										  k_suffix.size());

	final_names->clear();
	working_names->clear();
	final_names->reserve(size_t(channel_count));
	working_names->reserve(size_t(channel_count));
	for (int i = 0; i < channel_count; i++) {
		std::string final_name = base + "." + std::to_string(i) + ".pcm";
		final_names->push_back(final_name);
		working_names->push_back(final_name + ".working");
	}
	return true;
}

bool ConformTask::run()
{
	int channel_count = channel_count_from_layout(channel_layout_);

	std::vector<std::string> final_names, working_names;
	if (!derive_filenames(output_filename_, channel_count, &final_names,
						  &working_names)) {
		set_error("Invalid conform output filename");
		return false;
	}

	OakDecoder decoder = oakcodec_decoder_init();
	if (!decoder.ctx) {
		set_error("Failed to create decoder");
		return false;
	}

	bool ret = false;

	if (oakcodec_decoder_open(decoder, input_filename_.c_str(),
							  stream_index_) != OAKCODEC_OK) {
		char err[256];
		oakcodec_decoder_last_error(decoder, err, sizeof(err));
		set_error(std::string("Failed to open decoder for audio conform: ") +
				  err);
	} else {
		std::vector<const char *> working_ptrs;
		working_ptrs.reserve(working_names.size());
		for (const std::string &name : working_names) {
			working_ptrs.push_back(name.c_str());
		}

		int result = oakcodec_decoder_conform_audio(
			decoder, working_ptrs.data(), int(working_ptrs.size()),
			sample_rate_, channel_layout_, sample_format_,
			get_cancel_atom());

		oakcodec_decoder_close(decoder);

		if (result == OAKCODEC_OK) {
			ret = true;
			for (size_t i = 0; i < final_names.size() && ret; i++) {
				std::error_code ec;
				std::filesystem::rename(working_names[i], final_names[i], ec);
				if (ec) {
					set_error("Failed to move conformed audio into place");
					ret = false;
				}
			}
		} else {
			// Clean up any partial working files
			for (const std::string &name : working_names) {
				std::error_code ec;
				std::filesystem::remove(name, ec);
			}

			if (result == OAKCODEC_E_CANCELLED) {
				set_error("Audio conform was cancelled");
			} else {
				set_error("Audio conform failed");
			}
		}
	}

	oakcodec_decoder_free(&decoder);
	return ret;
}

}
