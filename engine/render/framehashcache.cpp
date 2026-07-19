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
#include <QDir>
#include <QFileInfo>

#include "codec/frame.h"
#include "common/filefunctions.h"
#include "common/oiioutils.h"
#include "render/diskmanager.h"

namespace olive
{

#define super PlaybackCache

FrameHashCache::FrameHashCache(QObject *parent)
	: super(parent)
{
	if (DiskManager::instance()) {
		connect(DiskManager::instance(), &DiskManager::deleted_frame, this,
				&FrameHashCache::hash_deleted);
		connect(DiskManager::instance(), &DiskManager::invalidate_project, this,
				&FrameHashCache::project_invalidated);
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

QString FrameHashCache::get_valid_cache_filename(const Rational &time) const
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

	return QString();
}

bool FrameHashCache::save_cache_frame(const int64_t &time, FramePtr frame) const
{
	return save_cache_frame(get_cache_directory(), get_uuid(), time, frame);
}

bool FrameHashCache::save_cache_frame(const QString &cache_path,
									const QUuid &uuid, const int64_t &time,
									FramePtr frame)
{
	if (cache_path.isEmpty()) {
		qWarning() << "Failed to save cache frame with empty path";
		return false;
	}

	QString fn = cache_path_name(cache_path, uuid, time);

	bool ret = save_cache_frame(fn, frame);

	// Register frame with the disk manager
	if (ret) {
		QMetaObject::invokeMethod(DiskManager::instance(), "created_file",
								  Q_ARG(QString, cache_path),
								  Q_ARG(QString, fn));
	}

	return ret;
}

bool FrameHashCache::save_cache_frame(const QString &cache_path,
									const QUuid &uuid, const Rational &time,
									const Rational &tb, FramePtr frame)
{
	if (cache_path.isEmpty()) {
		qWarning() << "Failed to save cache frame with empty path";
		return false;
	}

	QString fn = cache_path_name(cache_path, uuid, time, tb);

	bool ret = save_cache_frame(fn, frame);

	// Register frame with the disk manager
	if (ret) {
		QMetaObject::invokeMethod(DiskManager::instance(), "created_file",
								  Q_ARG(QString, cache_path),
								  Q_ARG(QString, fn));
	}

	return ret;
}

FramePtr FrameHashCache::load_cache_frame(const QString &cache_path,
										const QUuid &uuid, const int64_t &time)
{
	// Minor optimization, we store frames currently being saved just in case something tries to load
	// while we're saving. This should *occasionally* optimize and also prevent scenarios where
	// we try to load a frame that's half way through being saved.
	QString filename = cache_path_name(cache_path, uuid, time);

	if (cache_path.isEmpty()) {
		qWarning() << "Failed to load cache frame with empty path";
		return nullptr;
	}

	return load_cache_frame(filename);
}

FramePtr FrameHashCache::load_cache_frame(const int64_t &hash) const
{
	return load_cache_frame(get_cache_directory(), get_uuid(), hash);
}

FramePtr FrameHashCache::load_cache_frame(const QString &fn)
{
	FramePtr frame = nullptr;

	if (!fn.isEmpty() && QFileInfo::exists(fn)) {
		try {
			Imf::InputFile file(fn.toUtf8(), 0);

			Imath::Box2i dw = file.header().dataWindow();
			Imf::PixelType pix_type =
				file.header().channels().begin().channel().type;
			int width = dw.max.x - dw.min.x + 1;
			int height = dw.max.y - dw.min.y + 1;
			bool has_alpha = file.header().channels().findChannel("A");

			int div = qMax(1, static_cast<const Imf::IntAttribute &>(
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
			QImage img;

			if (img.load(fn, "jpg")) {
				// FIXME: Hardcoded
				const int div = 1;
				const PixelFormat image_format = PixelFormat::u8;
				const int channel_count = 4;
				const Rational par(1, 1);

				// Convert to frame (FIXME: might be slow? may be a better way to do this on the GPU)
				img.convertTo(QImage::Format_RGBA8888_Premultiplied);

				frame = Frame::create();
				frame->set_video_params(VideoParams(
					img.width() * div, img.height() * div, image_format,
					channel_count, par, VideoParams::k_interlace_none, div));

				frame->allocate();

				for (int i = 0; i < img.height(); i++) {
					memcpy(frame->data() + frame->linesize_bytes() * i,
						   img.bits() + img.bytesPerLine() * i,
						   frame->width() *
							   frame->video_params().get_bytes_per_pixel());
				}

			} else {
				qCritical() << "Failed to read cache frame:" << e.what();

				// Clear frame to signal that nothing was loaded
				frame = nullptr;

				// Assume this frame is corrupt in some way and delete it
				QMetaObject::invokeMethod(DiskManager::instance(),
										  "delete_specific_file",
										  Q_ARG(QString, fn));
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

void FrameHashCache::LoadStateEvent(QDataStream &stream)
{
	uint32_t version;
	int num, den;

	stream >> version;

	switch (version) {
	case 1:
		stream >> num;
		stream >> den;
		timebase_ = Rational(num, den);
		break;
	}
}

void FrameHashCache::SaveStateEvent(QDataStream &stream)
{
	uint32_t version = 1;

	stream << version;

	stream << timebase_.numerator();
	stream << timebase_.denominator();
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

void FrameHashCache::hash_deleted(const QString &path, const QString &filename)
{
	QString cache_dir = get_cache_directory();
	if (cache_dir.isEmpty() || path != cache_dir) {
		return;
	}

	QFileInfo info(filename);
	if (get_uuid().toString() != info.dir().dirName()) {
		return;
	}

	int64_t timestamp = info.fileName().toLongLong();
	invalidate(TimeRange(to_time(timestamp), to_time(timestamp + 1)));
}

void FrameHashCache::project_invalidated(Project *p)
{
	if (get_project() == p) {
		invalidate_all();
	}
}

QString FrameHashCache::cache_path_name(const int64_t &time) const
{
	return cache_path_name(get_cache_directory(), get_uuid(), time);
}

QString FrameHashCache::cache_path_name(const Rational &time) const
{
	return cache_path_name(get_cache_directory(), get_uuid(), time, timebase_);
}

QString FrameHashCache::cache_path_name(const QString &cache_path,
									  const QUuid &cache_id,
									  const int64_t &time)
{
	QString filename = get_this_cache_directory(cache_path, cache_id)
						   .filePath(QString::number(time));

	// Register that in some way this hash has been accessed
	if (DiskManager::instance()) {
		QMetaObject::invokeMethod(DiskManager::instance(), "accessed",
								  Q_ARG(QString, cache_path),
								  Q_ARG(QString, filename));
	}

	return filename;
}

QString FrameHashCache::cache_path_name(const QString &cache_path,
									  const QUuid &cache_id,
									  const Rational &time, const Rational &tb)
{
	return cache_path_name(cache_path, cache_id,
						 Timecode::time_to_timestamp(time, tb,
													 Timecode::k_round));
}

bool FrameHashCache::save_cache_frame(const QString &filename,
									const FramePtr frame)
{
	// Ensure directory is created
	QDir cache_dir = QFileInfo(filename).dir();
	if (!FileFunctions::directory_is_valid(cache_dir)) {
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
			Imf::OutputFile out(filename.toUtf8(), header, 0);

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
			qCritical() << "Failed to write cache frame:" << e.what();

			return false;
		}
	} else {
		QImage::Format fmt = QImage::Format_Invalid;

		switch (frame->format()) {
		case PixelFormat::u8:
			if (frame->channel_count() == VideoParams::k_rgba_channel_count) {
				fmt = QImage::Format_RGBA8888_Premultiplied;
			} else if (frame->channel_count() ==
					   VideoParams::k_rgb_channel_count) {
				fmt = QImage::Format_RGB888;
			}
			break;
		case PixelFormat::u10:
			break;
		case PixelFormat::u16:
			if (frame->channel_count() == VideoParams::k_rgba_channel_count) {
				fmt = QImage::Format_RGBA64_Premultiplied;
			}
			break;
		case PixelFormat::f16:
		case PixelFormat::f32:
		case PixelFormat::count:
		case PixelFormat::invalid:
			break;
		}

		if (fmt == QImage::Format_Invalid) {
			return false;
		}

		QImage img(reinterpret_cast<const uchar *>(frame->data()),
				   frame->width(), frame->height(), frame->linesize_bytes(),
				   fmt);

		return img.save(filename, "jpg");
	}
}

}
