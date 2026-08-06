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

#include "oiiodecoder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "common/oiioutils.h"

namespace olive
{

std::vector<std::string> OIIODecoder::supported_formats;

namespace
{

// Thin wrappers over the oakcommon_oiioutils_* C API (replacing the former
// OIIOUtils C++ class). The handle is stateless, so it is created and
// released per call.

int pix_format_from_oiio_basetype(int base_type)
{
	int out = OAKCOMMON_PIXEL_FORMAT_INVALID;
	OakOIIOUtils utils = oakcommon_oiioutils_init();
	oakcommon_oiioutils_get_format_from_oiio_basetype(utils, base_type, &out);
	oakcommon_oiioutils_free(&utils);
	return out;
}

int oiio_base_type_from_pix_format(int pixel_format)
{
	int out = 0; // OIIO::TypeDesc::UNKNOWN
	OakOIIOUtils utils = oakcommon_oiioutils_init();
	oakcommon_oiioutils_get_oiio_base_type_from_format(utils, pixel_format,
													 &out);
	oakcommon_oiioutils_free(&utils);
	return out;
}

/**
 * @brief Flatten an OakVideoParams handle into the oakrender POD
 *        (needed at every texture creation point)
 */
void fill_render_params(const OakVideoParams &params,
						oakrender_video_params *out)
{
	*out = oakrender_video_params{};
	oakcommon_videoparams_get_width(params, &out->width);
	oakcommon_videoparams_get_height(params, &out->height);
	oakcommon_videoparams_get_time_base(params, &out->time_base_num,
									  &out->time_base_den);
	oakcommon_videoparams_get_format(params, &out->format);
	oakcommon_videoparams_get_pixel_aspect_ratio(
		params, &out->pixel_aspect_num, &out->pixel_aspect_den);
	oakcommon_videoparams_get_interlacing(params, &out->interlacing);
	oakcommon_videoparams_get_color_range(params, &out->color_range);
	oakcommon_videoparams_get_divider(params, &out->divider);
	oakcommon_videoparams_get_video_type(params, &out->video_type);
	oakcommon_videoparams_get_premultiplied_alpha(params,
												&out->premultiplied_alpha);
}

std::vector<std::string> split_string(const std::string &s, char delimiter)
{
	std::vector<std::string> out;
	std::string::size_type start = 0;
	while (true) {
		std::string::size_type pos = s.find(delimiter, start);
		if (pos == std::string::npos) {
			out.push_back(s.substr(start));
			break;
		}
		out.push_back(s.substr(start, pos - start));
		start = pos + 1;
	}
	return out;
}

std::string to_lower(const std::string &s)
{
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(),
				   [](unsigned char c) { return char(std::tolower(c)); });
	return out;
}

} // namespace

OIIODecoder::OIIODecoder()
	: image_(nullptr)
{
}

std::string OIIODecoder::id() const
{
	return "oiio";
}

FootageDescription OIIODecoder::probe(const std::string &filename,
									  OakCancelAtom *cancelled) const
{
	(void) cancelled;

	FootageDescription desc(id());

	// Filter out any file extensions that aren't expected to work - sometimes OIIO will crash trying
	// to open a file that it can't if it's given one
	if (!file_type_is_supported(filename)) {
		return desc;
	}

	auto in = OIIO::ImageInput::open(filename);

	if (!in) {
		return desc;
	}

	// Filter out OIIO detecting an "FFmpeg movie", we have a native FFmpeg decoder that can handle
	// it better
	if (!strcmp(in->format_name(), "FFmpeg movie")) {
		return desc;
	}

	bool stream_enabled = true;

	int i;
	for (i = 0; in->seek_subimage(i, 0); i++) {
		OIIO::ImageSpec spec = in->spec();

		OakVideoParams video_params = get_video_params_from_image_spec(spec);

		oakcommon_videoparams_set_stream_index(video_params, i);

		if (i > 1) {
			// This is a multilayer image and this image might have an offset
			OIIO::ImageSpec root_spec = in->spec(0);

			float norm_x = spec.x + float(spec.width) * 0.5f -
						   float(root_spec.width) * 0.5f;
			float norm_y = spec.y + float(spec.height) * 0.5f -
						   float(root_spec.height) * 0.5f;

			oakcommon_videoparams_set_x(video_params, norm_x);
			oakcommon_videoparams_set_y(video_params, norm_y);
		}

		// By default, only enable the first subimage (presumably the combined image). Later we will
		// ask the user if they want to enable the layers instead.
		oakcommon_videoparams_set_enabled(video_params, stream_enabled ? 1 : 0);
		stream_enabled = false;

		// OIIO automatically premultiplies alpha
		// FIXME: We usually disassociate the alpha for the color management later, for 8-bit images this
		//        likely reduces the fidelity?
		oakcommon_videoparams_set_premultiplied_alpha(video_params, 1);

		desc.add_video_stream(video_params);
		oakcommon_videoparams_free(&video_params);
	}

	desc.set_stream_count(i);

	// If we're here, we have a successful image open
	in->close();

	return desc;
}

bool OIIODecoder::open_internal()
{
	// If we can open the filename provided, assume everything is working
	return open_image_handler(stream().filename(), stream().stream());
}

OakRenderTexture *
OIIODecoder::retrieve_video_internal(const RetrieveVideoParams &p)
{
	FramePtr frame = retrieve_video_frame_internal(p);
	if (!frame) {
		return nullptr;
	}

	OakVideoParams frame_params = frame->video_params(); // addref'd copy
	oakrender_video_params rvp;
	fill_render_params(frame_params, &rvp);
	oakcommon_videoparams_free(&frame_params);

	// Frame linesize is already in bytes, which is what the C API expects
	return oakrender_display_texture_create(p.renderer, &rvp, frame->data(),
											frame->linesize_bytes());
}

FramePtr OIIODecoder::retrieve_video_frame_internal(const RetrieveVideoParams &p)
{
	OakVideoParams vp = get_video_params_from_image_spec(image_->spec());
	oakcommon_videoparams_set_divider(vp, p.divider);

	if (!buffer_.is_allocated() || last_params_.divider != p.divider) {
		last_params_ = p;

		buffer_.destroy();
		buffer_.set_video_params(vp); // Frame addrefs the handle
		buffer_.allocate();

		if (p.divider == 1) {
			// Just upload straight to the buffer
			image_->read_image(0, 0, 0, -1, oiio_pix_fmt_, buffer_.data());
		} else {
			OIIO::ImageBuf buf(image_->spec());
			image_->read_image(0, 0, 0, -1, image_->spec().format,
							   buf.localpixels(), buf.pixel_stride(),
							   buf.scanline_stride(), buf.z_stride());

			// Roughly downsample image for divider (for some reason OIIO::ImageBufAlgo::resample failed here)
			int px_sz = 0;
			oakcommon_videoparams_get_bytes_per_pixel(vp, &px_sz);
			for (int dst_y = 0; dst_y < buffer_.height(); dst_y++) {
				int src_y = dst_y * buf.spec().height / buffer_.height();

				for (int dst_x = 0; dst_x < buffer_.width(); dst_x++) {
					int src_x = dst_x * buf.spec().width / buffer_.width();
					memcpy(buffer_.data() + buffer_.linesize_bytes() * dst_y +
							   px_sz * dst_x,
						   static_cast<uint8_t *>(buf.localpixels()) +
							   buf.scanline_stride() * src_y + px_sz * src_x,
						   px_sz);
				}
			}
		}
	}

	int format = OAKCOMMON_PIXEL_FORMAT_INVALID;
	oakcommon_videoparams_get_format(vp, &format);
	oakcommon_videoparams_free(&vp);

	// Force F32 output for all still images
	if (format != PixelFormat::f32) {
		FramePtr f32_frame = buffer_.convert(PixelFormat::f32);
		if (f32_frame) {
			f32_frame->set_timestamp(p.time);
			return f32_frame;
		}
	}

	FramePtr frame = Frame::create();
	OakVideoParams buffer_params = buffer_.video_params(); // addref'd copy
	frame->set_video_params(buffer_params);
	oakcommon_videoparams_free(&buffer_params);
	frame->set_timestamp(p.time);
	if (!frame->allocate()) {
		return nullptr;
	}
	memcpy(frame->data(), buffer_.const_data(),
		   size_t(buffer_.allocated_size()));
	return frame;
}

void OIIODecoder::close_internal()
{
	close_image_handle();
}

bool OIIODecoder::file_type_is_supported(const std::string &fn)
{
	// We prioritize OIIO over FFmpeg to pick up still images more effectively, but some OIIO decoders (notably OpenJPEG)
	// will segfault entirely if given unexpected data (an MPEG-4 for instance). To workaround this issue, we use OIIO's
	// "extension_list" attribute and match it with the extension of the file.

	// Check if we've created the supported formats list, create it if not
	if (supported_formats.empty()) {
		std::vector<std::string> extension_list =
			split_string(OIIO::get_string_attribute("extension_list"), ';');

		// The format of "extension_list" is "format:ext", we want to separate it into a simple list of extensions
		for (const std::string &ext : extension_list) {
			std::vector<std::string> format_and_ext = split_string(ext, ':');

			if (format_and_ext.size() >= 2) {
				std::vector<std::string> exts =
					split_string(format_and_ext.at(1), ',');
				supported_formats.insert(supported_formats.end(),
										 exts.begin(), exts.end());
			}
		}
	}

	// QFileInfo::suffix(): extension after the last '.', case-insensitive match
	std::string suffix = std::filesystem::path(fn).extension().string();
	if (!suffix.empty() && suffix.front() == '.') {
		suffix.erase(0, 1);
	}
	suffix = to_lower(suffix);

	for (const std::string &supported : supported_formats) {
		if (to_lower(supported) == suffix) {
			return true;
		}
	}

	return false;
}

bool OIIODecoder::open_image_handler(const std::string &fn, int subimage)
{
	image_ = OIIO::ImageInput::open(fn);

	if (!image_) {
		return false;
	}

	if (!image_->seek_subimage(subimage, 0)) {
		return false;
	}

	// Check if we can work with this pixel format
	const OIIO::ImageSpec &spec = image_->spec();

	// We use RGBA frames because that tends to be the native format of GPUs
	pix_fmt_ = static_cast<PixelFormat::Format>(
		pix_format_from_oiio_basetype(spec.format.basetype));

	if (pix_fmt_ == PixelFormat::invalid) {
		fprintf(stderr, "Failed to convert OIIO::ImageDesc to native pixel format\n");
		return false;
	}

	oiio_pix_fmt_ =
		static_cast<OIIO::TypeDesc::BASETYPE>(
			oiio_base_type_from_pix_format(pix_fmt_));

	if (oiio_pix_fmt_ == OIIO::TypeDesc::UNKNOWN) {
		fprintf(stderr, "Failed to determine appropriate OIIO basetype from native format\n");
		return false;
	}

	return true;
}

void OIIODecoder::close_image_handle()
{
	if (image_) {
		image_->close();
		image_ = nullptr;
	}

	buffer_.destroy();
}

OakVideoParams
OIIODecoder::get_video_params_from_image_spec(const OIIO::ImageSpec &spec)
{
	OakVideoParams video_params = oakcommon_videoparams_init();

	oakcommon_videoparams_set_width(video_params, spec.width);
	oakcommon_videoparams_set_height(video_params, spec.height);
	oakcommon_videoparams_set_format(
		video_params, pix_format_from_oiio_basetype(spec.format.basetype));
	oakcommon_videoparams_set_channel_count(video_params, spec.nchannels);

	int par_num = 1, par_den = 1;
	{
		OakOIIOUtils utils = oakcommon_oiioutils_init();
		oakcommon_oiioutils_get_pixel_aspect_ratio(
			utils, spec.get_float_attribute("PixelAspectRatio", 1.0f),
			&par_num, &par_den);
		oakcommon_oiioutils_free(&utils);
	}
	oakcommon_videoparams_set_pixel_aspect_ratio(video_params, par_num,
											   par_den);

	oakcommon_videoparams_set_video_type(video_params,
									   OAKCOMMON_VIDEO_TYPE_STILL);

	return video_params;
}

}
