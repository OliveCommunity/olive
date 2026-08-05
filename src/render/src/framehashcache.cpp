/*** Olive - Non-Linear Video Editor Copyright (C) 2022 Olive Team
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

#include "framehashcache.h"

#include <OpenEXR/ImfFloatAttribute.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenImageIO/imageio.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "codec/frame.h"
#include "diskmanager.h"
#include "filefunctions.h"
#include "oiioutils.h"

namespace olive
{

#define super PlaybackCache

FrameHashCache::FrameHashCache(Node *parent)
	: super(parent)
	, deleted_frame_handler_id_(0)
	, invalidate_project_handler_id_(0)
{
	if (DiskManager::instance()) {
		deleted_frame_handler_id_ = DiskManager::instance()->add_deleted_frame_handler(
			[this](const std::string &path, const std::string &filename) {
				hash_deleted(path, filename);
			});
		invalidate_project_handler_id_ =
			DiskManager::instance()->add_invalidate_project_handler(
				[this](Project *p) { project_invalidated(p); });
	}
}

FrameHashCache::~FrameHashCache()
{
	// QObject used to auto-disconnect on destruction; unregister explicitly.
	if (DiskManager::instance()) {
		if (deleted_frame_handler_id_) {
			DiskManager::instance()->remove_deleted_frame_handler(
				deleted_frame_handler_id_);
		}
		if (invalidate_project_handler_id_) {
			DiskManager::instance()->remove_invalidate_project_handler(
				invalidate_project_handler_id_);
		}
	}
}

void FrameHashCache::set_timebase(const Rational &tb)
{
	timebase_ = tb;
}

void FrameHashCache::validate_timestamp(const int64_t &ts)
{
	TimeRange frame_range(to_time(ts), to_time(ts + 1));
	validate(frame_range);
}

void FrameHashCache::validate_time(const Rational &time)
{
	validate(TimeRange(time, time + timebase_));
}

std::string FrameHashCache::get_valid_cache_filename(const Rational &time) const
{
	if (is_frame_cached(time)) {
		return cache_path_name(time);
	} else if (!get_passthroughs().empty()) {
		for (const Passthrough &p : get_passthroughs()) {
			if (p.contains(time)) {
				return cache_path_name(get_cache_directory(), p.cache, time,
									   timebase_);
			}
		}
	}

	return std::string();
}

bool FrameHashCache::save_cache_frame(const int64_t &time, FramePtr frame) const
{
	return save_cache_frame(get_cache_directory(), get_uuid(), time, frame);
}

bool FrameHashCache::save_cache_frame(const std::string &cache_path,
									  const std::string &uuid,
									  const int64_t &time, FramePtr frame)
{
	if (cache_path.empty()) {
		fprintf(stderr, "Failed to save cache frame with empty path\n");
		return false;
	}

	std::string fn = cache_path_name(cache_path, uuid, time);

	bool ret = save_cache_frame(fn, frame);

	// Register frame with the disk manager
	if (ret) {
		// Was a queued cross-thread QMetaObject::invokeMethod; now a direct call
		DiskManager::instance()->created_file(cache_path, fn);
	}

	return ret;
}

bool FrameHashCache::save_cache_frame(const std::string &cache_path,
									  const std::string &uuid,
									  const Rational &time, const Rational &tb,
									  FramePtr frame)
{
	if (cache_path.empty()) {
		fprintf(stderr, "Failed to save cache frame with empty path\n");
		return false;
	}

	std::string fn = cache_path_name(cache_path, uuid, time, tb);

	bool ret = save_cache_frame(fn, frame);

	// Register frame with the disk manager
	if (ret) {
		// Was a queued cross-thread QMetaObject::invokeMethod; now a direct call
		DiskManager::instance()->created_file(cache_path, fn);
	}

	return ret;
}

FramePtr FrameHashCache::load_cache_frame(const std::string &cache_path,
										  const std::string &uuid,
										  const int64_t &time)
{
	// Minor optimization, we store frames currently being saved just in case something tries to load
	// while we're saving. This should *occasionally* optimize and also prevent scenarios where
	// we try to load a frame that's half way through being saved.
	std::string filename = cache_path_name(cache_path, uuid, time);

	if (cache_path.empty()) {
		fprintf(stderr, "Failed to load cache frame with empty path\n");
		return nullptr;
	}

	return load_cache_frame(filename);
}

FramePtr FrameHashCache::load_cache_frame(const int64_t &hash) const
{
	return load_cache_frame(get_cache_directory(), get_uuid(), hash);
}

FramePtr FrameHashCache::load_cache_frame(const std::string &fn)
{
	FramePtr frame = nullptr;

	std::error_code ec;
	if (!fn.empty() && std::filesystem::exists(fn, ec)) {
		try {
			Imf::InputFile file(fn.c_str(), 0);

			Imath::Box2i dw = file.header().dataWindow();
			Imf::PixelType pix_type =
				file.header().channels().begin().channel().type;
			int width = dw.max.x - dw.min.x + 1;
			int height = dw.max.y - dw.min.y + 1;
			bool has_alpha = file.header().channels().findChannel("A");

			int div = std::max(1, static_cast<const Imf::IntAttribute &>(
									file.header()["oliveDivider"])
									.value());

			PixelFormat image_format;
			if (pix_type == Imf::HALF) {
				image_format = PixelFormat::f16;
			} else {
				image_format = PixelFormat::f32;
			}

			int channel_count = has_alpha ? VideoParams::k_rgba_channel_count :
											VideoParams::k_rgb_channel_count;

			frame = Frame::create();
			frame->set_video_params(VideoParams(
				width * div, height * div, image_format, channel_count,
				Rational::from_double(file.header().pixelAspectRatio()),
				VideoParams::k_interlace_none, div));

			frame->allocate();

			int bpc = VideoParams::get_bytes_per_channel(image_format);

			size_t xs = channel_count * bpc;
			size_t ys = frame->linesize_bytes();

			Imf::FrameBuffer framebuffer;
			framebuffer.insert("R",
							   Imf::Slice(pix_type, frame->data(), xs, ys));
			framebuffer.insert("G", Imf::Slice(pix_type, frame->data() + bpc,
											   xs, ys));
			framebuffer.insert(
				"B", Imf::Slice(pix_type, frame->data() + 2 * bpc, xs, ys));
			if (has_alpha) {
				framebuffer.insert(
					"A", Imf::Slice(pix_type, frame->data() + 3 * bpc, xs, ys));
			}

			file.setFrameBuffer(framebuffer);

			file.readPixels(dw.min.y, dw.max.y);
		} catch (const std::exception &e) {
			// Not an EXR, maybe it's a JPEG?
			std::unique_ptr<OIIO::ImageInput> in = OIIO::ImageInput::open(fn);

			if (in) {
				// FIXME: Hardcoded
				const int div = 1;
				const PixelFormat image_format = PixelFormat::u8;
				const int channel_count = 4;
				const Rational par(1, 1);

				const OIIO::ImageSpec &spec = in->spec();
				const int src_channels = spec.nchannels;

				// Read native channels as u8, then expand to RGBA with opaque
				// alpha (what QImage::convertTo(Format_RGBA8888_Premultiplied)
				// did; with alpha=255 premultiplication is the identity).
				std::vector<unsigned char> src(spec.width * spec.height *
											   src_channels);
				if (in->read_image(0, 0, 0, src_channels, OIIO::TypeDesc::UINT8,
								   src.data())) {
					frame = Frame::create();
					frame->set_video_params(VideoParams(
						spec.width * div, spec.height * div, image_format,
						channel_count, par, VideoParams::k_interlace_none,
						div));

					frame->allocate();

					size_t src_linesize = size_t(spec.width) * src_channels;
					for (int i = 0; i < spec.height; i++) {
						const unsigned char *src_row =
							src.data() + src_linesize * i;
						char *dst_row =
							frame->data() + frame->linesize_bytes() * i;
						for (int x = 0; x < spec.width; x++) {
							char *dst_px = dst_row + x * channel_count;
							const unsigned char *src_px =
								src_row + x * src_channels;
							for (int c = 0; c < 3; c++) {
								dst_px[c] = char(c < src_channels ? src_px[c] : 0);
							}
							dst_px[3] = char(0xFF);
						}
					}
				}

				in->close();
			}

			if (!frame) {
				fprintf(stderr, "Failed to read cache frame: %s\n", e.what());

				// Clear frame to signal that nothing was loaded
				frame = nullptr;

				// Assume this frame is corrupt in some way and delete it
				// (was a queued QMetaObject::invokeMethod; now a direct call)
				DiskManager::instance()->delete_specific_file(fn);
			}
		}
	}

	return frame;
}

void FrameHashCache::set_passthrough(PlaybackCache *cache)
{
	super::set_passthrough(cache);
	set_timebase(static_cast<FrameHashCache *>(cache)->get_timebase());
}

void FrameHashCache::LoadStateEvent(BinaryStreamReader &stream)
{
	uint32_t version;
	int32_t num, den;

	stream >> version;

	switch (version) {
	case 1:
		stream >> num;
		stream >> den;
		timebase_ = Rational(num, den);
		break;
	}
}

void FrameHashCache::SaveStateEvent(BinaryStreamWriter &stream)
{
	uint32_t version = 1;

	stream << version;

	stream << int32_t(timebase_.numerator());
	stream << int32_t(timebase_.denominator());
}

Rational FrameHashCache::to_time(const int64_t &ts) const
{
	return Timecode::timestamp_to_time(ts, timebase_);
}

int64_t FrameHashCache::to_timestamp(const Rational &ts,
									 Timecode::Rounding rounding) const
{
	return Timecode::time_to_timestamp(ts, timebase_, rounding);
}

void FrameHashCache::hash_deleted(const std::string &path,
								  const std::string &filename)
{
	std::string cache_dir = get_cache_directory();
	if (cache_dir.empty() || path != cache_dir) {
		return;
	}

	std::filesystem::path info(filename);
	if (get_uuid() != info.parent_path().filename().string()) {
		return;
	}

	int64_t timestamp = strtoll(info.filename().string().c_str(), nullptr, 10);
	invalidate(TimeRange(to_time(timestamp), to_time(timestamp + 1)));
}

void FrameHashCache::project_invalidated(Project *p)
{
	if (get_project() == p) {
		invalidate_all();
	}
}

std::string FrameHashCache::cache_path_name(const int64_t &time) const
{
	return cache_path_name(get_cache_directory(), get_uuid(), time);
}

std::string FrameHashCache::cache_path_name(const Rational &time) const
{
	return cache_path_name(get_cache_directory(), get_uuid(), time, timebase_);
}

std::string FrameHashCache::cache_path_name(const std::string &cache_path,
											const std::string &cache_id,
											const int64_t &time)
{
	std::string filename =
		(std::filesystem::path(get_this_cache_directory(cache_path, cache_id)) /
		 std::to_string(time))
			.string();

	// Register that in some way this hash has been accessed
	if (DiskManager::instance()) {
		// Was a queued cross-thread QMetaObject::invokeMethod; now a direct call
		DiskManager::instance()->accessed(cache_path, filename);
	}

	return filename;
}

std::string FrameHashCache::cache_path_name(const std::string &cache_path,
											const std::string &cache_id,
											const Rational &time,
											const Rational &tb)
{
	return cache_path_name(cache_path, cache_id,
						 Timecode::time_to_timestamp(time, tb,
													 Timecode::k_round));
}

bool FrameHashCache::save_cache_frame(const std::string &filename,
									  const FramePtr frame)
{
	// Ensure directory is created
	std::filesystem::path cache_dir =
		std::filesystem::path(filename).parent_path();
	if (!FileFunctions::directory_is_valid(cache_dir.string())) {
		return false;
	}

	if (VideoParams::format_is_float(frame->format())) {
		// Floating point types are stored in EXR
		Imf::PixelType pix_type;

		if (frame->format() == PixelFormat::f16) {
			pix_type = Imf::HALF;
		} else {
			pix_type = Imf::FLOAT;
		}

		Imf::Header header(frame->width(), frame->height());
		header.channels().insert("R", Imf::Channel(pix_type));
		header.channels().insert("G", Imf::Channel(pix_type));
		header.channels().insert("B", Imf::Channel(pix_type));
		if (frame->channel_count() == VideoParams::k_rgba_channel_count) {
			header.channels().insert("A", Imf::Channel(pix_type));
		}

		header.compression() = Imf::DWAA_COMPRESSION;
		header.insert("dwaCompressionLevel", Imf::FloatAttribute(200.0f));
		header.pixelAspectRatio() =
			frame->video_params().pixel_aspect_ratio().to_double();

		header.insert("oliveDivider",
					  Imf::IntAttribute(frame->video_params().divider()));

		try {
			Imf::OutputFile out(filename.c_str(), header, 0);

			int bpc = VideoParams::get_bytes_per_channel(frame->format());

			size_t xs = frame->channel_count() * bpc;
			size_t ys = frame->linesize_bytes();

			Imf::FrameBuffer framebuffer;
			framebuffer.insert("R",
							   Imf::Slice(pix_type, frame->data(), xs, ys));
			framebuffer.insert("G", Imf::Slice(pix_type, frame->data() + bpc,
											   xs, ys));
			framebuffer.insert(
				"B", Imf::Slice(pix_type, frame->data() + 2 * bpc, xs, ys));
			if (frame->channel_count() == VideoParams::k_rgba_channel_count) {
				framebuffer.insert(
					"A", Imf::Slice(pix_type, frame->data() + 3 * bpc, xs, ys));
			}
			out.setFrameBuffer(framebuffer);

			out.writePixels(frame->height());

			return true;
		} catch (const std::exception &e) {
			fprintf(stderr, "Failed to write cache frame: %s\n", e.what());

			return false;
		}
	} else {
		// Integer types are stored as JPEG via OIIO (was QImage). The JPEG
		// writer drops the alpha channel, as Qt's JPEG handler did.
		OIIO::TypeDesc base_type = OIIO::TypeDesc::UNKNOWN;

		switch (frame->format()) {
		case PixelFormat::u8:
			if (frame->channel_count() == VideoParams::k_rgba_channel_count ||
				frame->channel_count() == VideoParams::k_rgb_channel_count) {
				base_type = OIIO::TypeDesc::UINT8;
			}
			break;
		case PixelFormat::u10:
			break;
		case PixelFormat::u16:
			if (frame->channel_count() == VideoParams::k_rgba_channel_count) {
				base_type = OIIO::TypeDesc::UINT16;
			}
			break;
		case PixelFormat::f16:
		case PixelFormat::f32:
		case PixelFormat::count:
		case PixelFormat::invalid:
			break;
		}

		if (base_type == OIIO::TypeDesc::UNKNOWN) {
			return false;
		}

		std::unique_ptr<OIIO::ImageOutput> out =
			OIIO::ImageOutput::create(filename);
		if (!out) {
			return false;
		}

		int bpc = VideoParams::get_bytes_per_channel(frame->format());
		OIIO::ImageSpec spec(frame->width(), frame->height(),
							 frame->channel_count(), base_type);

		if (!out->open(filename, spec)) {
			return false;
		}

		bool ok = out->write_image(
			base_type, frame->const_data(), frame->channel_count() * bpc,
			frame->linesize_bytes());
		ok = out->close() && ok;

		return ok;
	}
}

}
