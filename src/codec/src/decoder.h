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

#ifndef OAK_DECODER_H
#define OAK_DECODER_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/loopmode.h"
#include "common/videoparams.h"
#include "footagedescription.h"
#include "frame.h"
#include "node/block.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/pixelformat.h"
#include "olive/core/render/samplebuffer.h"
#include "olive/core/util/rational.h"
#include "olive/core/util/timerange.h"
#include "render/cancelatom.h"
#include "render/renderer.h"

namespace olive
{

using core::AudioParams;
using core::PixelFormat;
using core::Rational;
using core::SampleBuffer;
using core::TimeRange;

/**
 * @brief Local replacement for render/rendermodes.h
 *
 * oakrender's C API has no render-mode counterpart. Values mirror
 * engine/render/rendermodes.h (k_offline = 0, k_online = 1).
 */
class RenderMode {
public:
	enum Mode { k_offline, k_online };
};

/**
 * @brief "Don't force a color range" sentinel for
 *        Decoder::RetrieveVideoParams::force_range (the actual ranges are
 *        the OAKCOMMON_COLOR_RANGE_* values).
 */
inline constexpr int k_color_range_default = -1;

class Decoder;
using DecoderPtr = std::shared_ptr<Decoder>;

#define DECODER_DEFAULT_DESTRUCTOR(x) \
	virtual ~x() override             \
	{                                 \
		close_internal();              \
	}

/**
 * @brief A decoder's is the main class for bringing external media into Olive
 *
 * Its responsibilities are to serve as
 * abstraction from codecs/decoders and provide complete frames. These frames can be video or audio data and are
 * provided as Frame objects in shared pointers to alleviate the responsibility of memory handling.
 *
 * The main function in a decoder is Retrieve() which should return complete image/audio data. A decoder should
 * alleviate all the complexities of codec compression from the rest of the application (i.e. a decoder should never
 * return a partial frame or require other parts of the system to interface directly with the codec). Often this will
 * necessitate pre-emptively caching, indexing, or even fully transcoding media before using it which can be implemented
 * through the Analyze() function.
 *
 * A decoder does NOT perform any pixel/sample format conversion. Frames should pass through the PixelService
 * to be utilized in the rest of the rendering pipeline.
 */
class Decoder {
public:
	enum RetrieveState { k_ready, k_failed_to_open, k_index_unavailable };

	Decoder();

	virtual ~Decoder();

	/**
   * @brief Unique decoder ID
   */
	virtual std::string id() const = 0;

	virtual bool supports_video()
	{
		return false;
	}
	virtual bool supports_audio()
	{
		return false;
	}

	void increment_access_time(int64_t t);

	class CodecStream {
	public:
		CodecStream()
			: stream_(-1)
			, block_(nullptr)
		{
		}

		CodecStream(const std::string &filename, int stream,
					const OakNodeBlock *block)
			: filename_(filename)
			, stream_(stream)
			, block_(block)
		{
		}

		bool is_valid() const
		{
			return !filename_.empty() && stream_ >= 0;
		}

		bool exists() const
		{
			std::error_code ec;
			return std::filesystem::exists(filename_, ec);
		}

		void reset()
		{
			*this = CodecStream();
		}

		bool operator==(const CodecStream &rhs) const
		{
			return filename_ == rhs.filename_ && stream_ == rhs.stream_;
		}

		const std::string &filename() const
		{
			return filename_;
		}

		int stream() const
		{
			return stream_;
		}

		/**
		 * @brief Associated timeline block (opaque oaknode handle)
		 *
		 * Borrowed pointer: codec only stores/compares it, never
		 * dereferences, retains, or frees it.
		 */
		const OakNodeBlock *block() const
		{
			return block_;
		}

	private:
		std::string filename_;

		int stream_;

		const OakNodeBlock *block_;
	};

	/**
   * @brief Open stream for decoding
   *
   * This function is thread safe.
   *
   * Returns TRUE if stream could be opened successfully. Also returns TRUE if the decoder is
   * already open and the stream == the stream provided. Returns FALSE if the stream couldn't
   * be opened OR if already open and the stream is NOT the same.
   */
	bool open(const CodecStream &stream);

	static const Rational k_any_timecode;

	struct RetrieveVideoParams {
		OakRenderRenderer renderer = {};
		Rational time;
		int divider = 1;
		PixelFormat maximum_format = PixelFormat::invalid;
		OakCancelAtom *cancelled = nullptr;
		int force_range = k_color_range_default;
		int src_interlacing = OAKCOMMON_VIDEO_INTERLACE_NONE;
	};

	/**
   * @brief Retrieves a video frame from footage
   *
   * This function will always return a valid frame unless a fatal error occurs (in such case,
   * nullptr will return). If the timecode is before the start of the footage, this function should
   * return the first frame. Likewise, if it is after the timecode, this function should return the
   * last frame.
   *
   * This function is thread safe and can only run while the decoder is open. \see Open()
   *
   * The returned texture handle is owned by the caller and must be
   * released with oakrender_display_texture_free().
   */
	OakRenderTexture retrieve_video(const RetrieveVideoParams &p);

	/**
   * @brief Retrieves a decoded video frame in CPU memory.
   *
   * Used by render-process isolation to decode media in the main process and pass packed pixel
   * data to workers through shared memory.
   */
	FramePtr retrieve_video_frame(const RetrieveVideoParams &p);

	enum RetrieveAudioStatus {
		k_invalid = -1,
		k_ok,
		k_waiting_for_conform,
		k_unknown_error
	};

	/**
   * @brief Retrieve audio data from footage
   *
   * This function will always return a sample buffer unless a fatal error occurs (in such case,
   * nullptr will return). The SampleBuffer should always have enough audio for the range provided.
   *
   * This function is thread safe and can only run while the decoder is open. \see Open()
   */
	RetrieveAudioStatus retrieve_audio(SampleBuffer &dest, const TimeRange &range,
									   const AudioParams &params,
									   const std::string &cache_path,
									   OakLoopMode loop_mode,
									   RenderMode::Mode mode);

	/**
   * @brief Determine the last time this decoder instance was used in any way
   */
	int64_t get_last_accessed_time();

	/**
   * @brief Generate a Footage object from a file
   *
   * If this decoder is able to parse this file, it will return a valid FootagePtr. Otherwise, it
   * will return nullptr.
   *
   * For sub-classes, this function should be effectively static. We can't do virtual static
   * functions in C++, but it should hold and access no state during its run.
   *
   * This function is re-entrant.
   */
	virtual FootageDescription probe(const std::string &filename,
									 OakCancelAtom *cancelled) const = 0;

	/**
   * @brief Closes media/deallocates memory
   *
   * This function is thread safe and can only run while the decoder is open. \see Open()
   */
	void close();

	/**
   * @brief Conform audio stream
   */
	bool conform_audio(const std::vector<std::string> &output_filenames,
					   const AudioParams &params,
					   OakCancelAtom *cancelled = nullptr);

	/**
   * @brief Create a Decoder instance using a Decoder ID
   *
   * @return
   *
   * A Decoder instance or nullptr if a Decoder with this ID does not exist
   */
	static DecoderPtr create_from_id(const std::string &id);

	static std::string
	transform_image_sequence_file_name(const std::string &filename,
									   const int64_t &number);

	static int get_image_sequence_digit_count(const std::string &filename);

	static int64_t get_image_sequence_index(const std::string &filename);

	static std::vector<DecoderPtr> receive_list_of_all_decoders();

	/**
   * @brief Set a callback receiving indexing progress (0-1)
   *
   * Replaces the former index_progress Qt signal.
   */
	void set_index_progress_callback(std::function<void(double)> callback)
	{
		index_progress_callback_ = std::move(callback);
	}

protected:
	/**
   * @brief Internal open function
   *
   * Sub-classes must override this function. Function will already be mutexed, so there is no need
   * to worry about thread safety. Also many other sanity checks will be done before this, so
   * sub-classes only need to worry about their own opening functions. It is guaranteed that the
   * decoder is not open yet and that the footage stream was from that sub-classes probe function.
   *
   * Return TRUE if everything opened successfully and the decoder is ready to work. Otherwise,
   * return FALSE. If this function returns false, Decoder will call close_internal to clean any
   * memory allocated during OpenInternal.
   */
	virtual bool open_internal() = 0;

	/**
   * @brief Internal close function
   *
   * Sub-classes must override this function. Function should be able to safely clear all allocated
   * memory. It may be called even if Open() didn't complete or RetrieveVideo() was never called.
   */
	virtual void close_internal() = 0;

	/**
   * @brief Internal frame retrieval function
   *
   * Sub-classes must override this function IF they support video. Function is already mutexed
   * so sub-classes don't need to worry about thread safety.
   *
   * The returned texture handle is owned by the caller and must be
   * released with oakrender_display_texture_free().
   */
	virtual OakRenderTexture
	retrieve_video_internal(const RetrieveVideoParams &p);

	virtual FramePtr retrieve_video_frame_internal(const RetrieveVideoParams &p);

	virtual bool
	conform_audio_internal(const std::vector<std::string> &filenames,
						   const AudioParams &params, OakCancelAtom *cancelled);

	void signal_processing_progress(int64_t ts, int64_t duration);

	/**
   * @brief Return currently open stream
   *
   * This function is NOT thread safe and should therefore only be called by thread safe functions.
   */
	const CodecStream &stream() const
	{
		return stream_;
	}

	virtual Rational get_audio_start_offset() const
	{
		return 0;
	}

private:
	void update_last_accessed();

	bool retrieve_audio_from_conform(
		SampleBuffer &sample_buffer,
		const std::vector<std::string> &conform_filenames, TimeRange range,
		OakLoopMode loop_mode, const AudioParams &params);

	CodecStream stream_;

	std::mutex mutex_;

	std::atomic_int64_t last_accessed_;

	OakRenderTexture cached_texture_ = {};
	Rational cached_time_;
	int cached_divider_ = 0;

	std::function<void(double)> index_progress_callback_;
};

}

#endif // OAK_DECODER_H
